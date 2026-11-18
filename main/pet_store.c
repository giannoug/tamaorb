// SPDX-License-Identifier: GPL-3.0-or-later
#include "pet_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "rom_store.h"
#include "saver.h"
#include "tama_core.h"
#include "tc_pet.h"

static const char *TAG = "pets";
static SemaphoreHandle_t s_mtx;

// A name entry: key "n<10 hex of the SHA-1>_<pet digit>".
typedef struct { uint8_t ver; uint8_t sha1[20]; uint8_t id; char name[PS_NAME_MAX + 1]; } entry_t;
typedef struct { uint8_t sha1[20]; uint8_t id; } ref_blob_t;

bool pet_ref_eq(const pet_ref_t *a, const pet_ref_t *b) { return a->id == b->id && !memcmp(a->sha1, b->sha1, 20); }

static void entry_key(const pet_ref_t *p, char out[16])
{
    snprintf(out, 16, "n%02x%02x%02x%02x%02x_%u", p->sha1[0], p->sha1[1], p->sha1[2], p->sha1[3], p->sha1[4],
             p->id % 10u);
}

static esp_err_t open_ns(const char *ns, nvs_open_mode_t mode, nvs_handle_t *h)
{
    return nvs_open_from_partition(ROM_STORE_PARTITION, ns, mode, h);
}

esp_err_t pet_store_init(void)
{
    if (!s_mtx) s_mtx = xSemaphoreCreateMutex();
    return s_mtx ? ESP_OK : ESP_ERR_NO_MEM;
}

static void lock(void) { if (s_mtx) xSemaphoreTake(s_mtx, portMAX_DELAY); }
static void unlock(void) { if (s_mtx) xSemaphoreGive(s_mtx); }

// ---- gathering: every pet's ref and stored name ----
typedef struct { pet_ref_t ref; char name[PS_NAME_MAX + 1]; bool named; } known_t;

static int find_known(const known_t *k, int n, const pet_ref_t *p)
{
    for (int i = 0; i < n; i++)
        if (pet_ref_eq(&k[i].ref, p)) return i;
    return -1;
}

static int add_known(known_t *k, int n, int max, const pet_ref_t *p)
{
    int i = find_known(k, n, p);
    if (i >= 0 || n >= max) return n;
    memset(&k[n], 0, sizeof k[n]);
    k[n].ref = *p;
    return n + 1;
}

static uint8_t s_rec[1024 + PS_OVERHEAD];  // under the lock

// Pets from name entries and from save keys. Caller holds the lock.
static int gather(known_t *k, int max)
{
    int n = 0;
    nvs_iterator_t it = NULL;
    esp_err_t e = nvs_entry_find(ROM_STORE_PARTITION, "pets", NVS_TYPE_BLOB, &it);
    nvs_handle_t h = 0;
    bool open = e == ESP_OK && open_ns("pets", NVS_READONLY, &h) == ESP_OK;
    while (e == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        entry_t en;
        size_t len = sizeof en;
        if (open && info.key[0] == 'n' && nvs_get_blob(h, info.key, &en, &len) == ESP_OK && len == sizeof en &&
            en.ver == 1 && en.id < PS_PETS_PER_ROM) {
            pet_ref_t r = { .id = en.id };
            memcpy(r.sha1, en.sha1, 20);
            int before = n;
            n = add_known(k, n, max, &r);
            int i = find_known(k, n, &r);
            if (i >= 0 && n > before) {
                en.name[PS_NAME_MAX] = 0;
                snprintf(k[i].name, sizeof k[i].name, "%s", en.name);
                k[i].named = ps_name_valid(k[i].name);
            }
        }
        e = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    if (open) nvs_close(h);

    it = NULL;
    e = nvs_entry_find(ROM_STORE_PARTITION, "saves", NVS_TYPE_BLOB, &it);
    open = e == ESP_OK && open_ns("saves", NVS_READONLY, &h) == ESP_OK;
    while (e == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        uint8_t pre[5], id;
        char slot;
        size_t len = sizeof s_rec;
        ps_meta_t m;
        const uint8_t *payload;
        size_t plen;
        if (open && ps_key_parse(info.key, pre, &id, &slot) && nvs_get_blob(h, info.key, s_rec, &len) == ESP_OK &&
            ps_record_decode(s_rec, len, &m, &payload, &plen) && !memcmp(m.rom_sha1, pre, 5) && m.pet_id == id) {
            pet_ref_t r = { .id = id };
            memcpy(r.sha1, m.rom_sha1, 20);
            if (find_known(k, n, &r) < 0) n = add_known(k, n, max, &r);
        }
        e = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    if (open) nvs_close(h);
    return n;
}

static esp_err_t write_entry(const pet_ref_t *p, const char *name)
{
    entry_t en = { .ver = 1, .id = p->id };
    memcpy(en.sha1, p->sha1, 20);
    snprintf(en.name, sizeof en.name, "%s", name);
    char key[16];
    entry_key(p, key);
    nvs_handle_t h;
    esp_err_t err = open_ns("pets", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, key, &en, sizeof en);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

// Gives every unnamed pet an automatic name, and stores it when store is set. Caller holds the lock.
static void name_all(known_t *k, int n, bool store)
{
    const char *names[PET_STORE_MAX];
    int nn = 0;
    for (int i = 0; i < n; i++)
        if (k[i].named) names[nn++] = k[i].name;
    for (int i = 0; i < n; i++) {
        if (k[i].named) continue;
        ps_name_auto(tc_model_name(tc_model_from_sha1(k[i].ref.sha1)), names, nn, k[i].name);
        k[i].named = true;
        names[nn++] = k[i].name;
        if (store && write_entry(&k[i].ref, k[i].name) != ESP_OK) ESP_LOGW(TAG, "could not store the name %s", k[i].name);
    }
}

static known_t s_known[PET_STORE_MAX];  // under the lock

// pet_store_name's answers: status polls ask for the running (and Back) pet every few seconds, and gather()
// reads every save. Under the lock; cleared by anything that changes a name or the set of pets.
#define NAME_CACHE 4
static struct { pet_ref_t ref; char name[PS_NAME_MAX + 1]; bool used; } s_cache[NAME_CACHE];
static int s_cache_next;
static void cache_clear(void) { memset(s_cache, 0, sizeof s_cache); }

int pet_store_list(pet_info_t *out, int max)
{
    tc_rom_entry_t *roms = malloc(sizeof *roms * 16);  // per call: the UI task and httpd list at the same time
    int nroms = roms ? rom_store_scan(roms, 16) : 0;
    lock();
    int n = gather(s_known, PET_STORE_MAX);
    name_all(s_known, n, true);
    int k = 0;
    for (int i = 0; i < n && k < max; i++) {
        pet_info_t *p = &out[k++];
        memset(p, 0, sizeof *p);
        p->ref = s_known[i].ref;
        snprintf(p->name, sizeof p->name, "%s", s_known[i].name);
        p->model = tc_model_from_sha1(p->ref.sha1);
        p->age_years = -1;
        p->egg = true;
        for (int r = 0; r < nroms; r++)
            if (!memcmp(roms[r].sha1, p->ref.sha1, 20)) p->rom_on_card = true;
        ps_meta_t m;
        size_t len = 0;
        if (saver_read_newest(p->ref.sha1, p->ref.id, &m, s_rec, sizeof s_rec, &len) == ESP_OK) {
            p->has_save = true;
            p->save_trusted = m.time_trusted;
            p->save_unix = m.unix_time;
            p->seq = m.seq;
            const uint8_t *payload;
            size_t plen;
            uint8_t ram[TC_PET_RAM_NIBBLES];
            if (ps_record_decode(s_rec, len, &m, &payload, &plen)) {
                size_t got = tc_state_ram(payload, plen, ram, sizeof ram);
                tc_pet_t pet;
                tc_pet_decode(p->model, ram, got, &pet);
                p->age_years = pet.age_years;
                p->egg = !pet.known;
            }
        }
    }
    unlock();
    free(roms);
    // By model, then by number.
    for (int i = 1; i < k; i++)
        for (int j = i; j > 0 && (out[j].model < out[j - 1].model ||
                                  (out[j].model == out[j - 1].model && out[j].ref.id < out[j - 1].ref.id)); j--) {
            pet_info_t t = out[j];
            out[j] = out[j - 1];
            out[j - 1] = t;
        }
    return k;
}

static bool get_ref(const char *key, pet_ref_t *out)
{
    nvs_handle_t h;
    if (open_ns("pets", NVS_READONLY, &h) != ESP_OK) return false;
    ref_blob_t b;
    size_t len = sizeof b;
    bool ok = nvs_get_blob(h, key, &b, &len) == ESP_OK && len == sizeof b && b.id < PS_PETS_PER_ROM;
    nvs_close(h);
    if (ok) { memcpy(out->sha1, b.sha1, 20); out->id = b.id; }
    return ok;
}

static esp_err_t set_ref(const char *key, const pet_ref_t *p)
{
    ref_blob_t b = { .id = p->id };
    memcpy(b.sha1, p->sha1, 20);
    nvs_handle_t h;
    esp_err_t err = open_ns("pets", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, key, &b, sizeof b);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

bool pet_store_active(pet_ref_t *out) { return get_ref("active", out); }
esp_err_t pet_store_set_active(const pet_ref_t *p) { return set_ref("active", p); }
esp_err_t pet_store_set_prev(const pet_ref_t *p) { return set_ref("prev", p); }

bool pet_store_prev(pet_ref_t *out) { return get_ref("prev", out); }

void pet_store_clear_prev(void)
{
    nvs_handle_t h;
    if (open_ns("pets", NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_erase_key(h, "prev") == ESP_OK) nvs_commit(h);
        nvs_close(h);
    }
}

esp_err_t pet_store_name(const pet_ref_t *p, char out[PS_NAME_MAX + 1])
{
    lock();
    for (int c = 0; c < NAME_CACHE; c++)
        if (s_cache[c].used && pet_ref_eq(&s_cache[c].ref, p)) {
            snprintf(out, PS_NAME_MAX + 1, "%s", s_cache[c].name);
            unlock();
            return ESP_OK;
        }
    int n = gather(s_known, PET_STORE_MAX);
    int i = find_known(s_known, n, p);  // a pet that doesn't exist (deleted) gets no name, so it isn't created again
    if (i >= 0 && !s_known[i].named) name_all(s_known, n, true);
    if (i >= 0) {
        snprintf(out, PS_NAME_MAX + 1, "%s", s_known[i].name);
        s_cache[s_cache_next].ref = *p;
        snprintf(s_cache[s_cache_next].name, sizeof s_cache[0].name, "%s", s_known[i].name);
        s_cache[s_cache_next].used = true;
        s_cache_next = (s_cache_next + 1) % NAME_CACHE;
    }
    unlock();
    return i >= 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t pet_store_create(const uint8_t sha1[20], tc_model_t m, pet_ref_t *out)
{
    lock();
    int n = gather(s_known, PET_STORE_MAX);
    name_all(s_known, n, true);
    bool used[PS_PETS_PER_ROM] = { false };
    const char *names[PET_STORE_MAX];
    for (int i = 0; i < n; i++) {
        names[i] = s_known[i].name;
        if (!memcmp(s_known[i].ref.sha1, sha1, 20)) used[s_known[i].ref.id] = true;
    }
    int id = ps_free_pet_id(used);
    esp_err_t err = ESP_ERR_NO_MEM;
    if (id >= 0 && n < PET_STORE_MAX) {
        pet_ref_t r = { .id = (uint8_t)id };
        memcpy(r.sha1, sha1, 20);
        char name[PS_NAME_MAX + 1];
        ps_name_auto(tc_model_name(m), names, n, name);
        err = write_entry(&r, name);
        if (err == ESP_ERR_NVS_NOT_ENOUGH_SPACE) err = ESP_ERR_NO_MEM;
        if (err == ESP_OK) {
            cache_clear();
            *out = r;
            ESP_LOGI(TAG, "new pet %s (%s, number %d)", name, tc_model_name(m), id);
        }
    }
    unlock();
    return err;
}

esp_err_t pet_store_rename(const pet_ref_t *p, const char *name)
{
    if (!ps_name_valid(name)) return ESP_ERR_INVALID_ARG;
    lock();
    int n = gather(s_known, PET_STORE_MAX);
    name_all(s_known, n, false);  // the others' names, to check against; stored only by a list
    int me = find_known(s_known, n, p);
    esp_err_t err = ESP_ERR_NOT_FOUND;
    if (me >= 0) {
        const char *others[PET_STORE_MAX];
        int no = 0;
        for (int i = 0; i < n; i++)
            if (i != me) others[no++] = s_known[i].name;
        err = ps_name_taken(name, others, no) ? ESP_ERR_INVALID_STATE : write_entry(p, name);
    }
    if (err == ESP_OK) cache_clear();
    unlock();
    return err;
}

esp_err_t pet_store_delete(const pet_ref_t *p)
{
    lock();
    int n = gather(s_known, PET_STORE_MAX);
    esp_err_t err = find_known(s_known, n, p) >= 0 ? saver_erase(p->sha1, p->id) : ESP_ERR_NOT_FOUND;
    if (err == ESP_OK) {
        char key[16];
        entry_key(p, key);
        nvs_handle_t h;
        if (open_ns("pets", NVS_READWRITE, &h) == ESP_OK) {
            esp_err_t e = nvs_erase_key(h, key);
            if (e == ESP_OK) nvs_commit(h);
            nvs_close(h);
        }
        cache_clear();
        ESP_LOGI(TAG, "deleted pet %d of %02x%02x", p->id, p->sha1[0], p->sha1[1]);
    }
    unlock();
    return err;
}
