// SPDX-License-Identifier: GPL-3.0-or-later
#include "saver.h"
#include <stdatomic.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "rom_store.h"
#include "tama_core.h"

static const char *TAG = "saver";

static uint8_t s_sha1[20];
static uint32_t s_seq;
static char s_next_slot = 'a';
static bool (*s_now)(int64_t *);
static ps_policy_t s_policy;
static uint8_t s_prev_buttons;
static volatile bool s_requested;
static volatile bool s_last_ok;
static SemaphoreHandle_t s_done;
static bool s_policy_ready;
static bool s_loaded;              // s_sha1 names the running pet
static volatile bool s_frozen;     // an import for the running pet landed: no more saves this boot
static SemaphoreHandle_t s_mtx;    // serialises every NVS write to the saves, and the import's read-then-write

static uint8_t s_state[1024];
static uint8_t s_rec[1024 + PS_OVERHEAD];

static esp_err_t open_ns(nvs_open_mode_t mode, nvs_handle_t *h)
{
    return nvs_open_from_partition(ROM_STORE_PARTITION, "saves", mode, h);
}

// Created on first use from whichever task gets here first; the others wait for it.
static void lock(void)
{
    static StaticSemaphore_t buf;
    static atomic_int state;  // 0 none, 1 creating, 2 ready
    int expect = 0;
    if (atomic_compare_exchange_strong(&state, &expect, 1)) {
        s_mtx = xSemaphoreCreateMutexStatic(&buf);
        atomic_store(&state, 2);
    }
    while (atomic_load(&state) != 2) vTaskDelay(1);
    xSemaphoreTake(s_mtx, portMAX_DELAY);
}

static void unlock(void) { xSemaphoreGive(s_mtx); }

static bool read_slot_of(nvs_handle_t h, const uint8_t sha1[20], char slot, ps_meta_t *m, const uint8_t **payload,
                         size_t *len, uint8_t *buf, size_t cap, size_t *rec_len)
{
    char key[16];
    ps_key(sha1, 0, slot, key);
    size_t n = cap;
    if (nvs_get_blob(h, key, buf, &n) != ESP_OK || !ps_record_decode(buf, n, m, payload, len) ||
        memcmp(m->rom_sha1, sha1, 20) != 0)
        return false;
    if (rec_len) *rec_len = n;
    return true;
}

static bool read_slot(nvs_handle_t h, char slot, ps_meta_t *m, const uint8_t **payload, size_t *len, uint8_t *buf, size_t cap)
{
    return read_slot_of(h, s_sha1, slot, m, payload, len, buf, cap, NULL);
}

esp_err_t saver_load(const uint8_t sha1[20], ps_meta_t *meta)
{
    lock();
    memcpy(s_sha1, sha1, 20);
    s_loaded = true;
    unlock();
    if (!s_done) s_done = xSemaphoreCreateBinary();
    nvs_handle_t h;
    if (open_ns(NVS_READONLY, &h) != ESP_OK) return ESP_ERR_NOT_FOUND;
    static uint8_t buf_b[sizeof s_rec];
    ps_meta_t ma, mb;
    const uint8_t *pa, *pb;
    size_t la, lb;
    bool a = read_slot(h, 'a', &ma, &pa, &la, s_rec, sizeof s_rec);
    bool b = read_slot(h, 'b', &mb, &pb, &lb, buf_b, sizeof buf_b);
    nvs_close(h);
    int pick = ps_pick_newest(a, &ma, b, &mb);
    if (pick < 0) return ESP_ERR_NOT_FOUND;
    // Keep counting from the highest sequence on flash whatever happens below.
    s_seq = (a && b) ? ((int32_t)(mb.seq - ma.seq) > 0 ? mb.seq : ma.seq) : (pick ? mb.seq : ma.seq);
    for (int attempt = 0; attempt < 2; attempt++) {
        int slot = attempt == 0 ? pick : !pick;
        bool ok = slot ? b : a;
        const ps_meta_t *m = slot ? &mb : &ma;
        if (!ok) continue;
        if (tc_state_load(slot ? pb : pa, slot ? lb : la)) {
            *meta = *m;
            s_next_slot = slot ? 'a' : 'b';  // overwrite the other slot next
            ESP_LOGI(TAG, "loaded save seq %lu from slot %c (time %lld, %s)", (unsigned long)m->seq, slot ? 'b' : 'a',
                     (long long)m->unix_time, m->time_trusted ? "trusted" : "untrusted");
            return ESP_OK;
        }
        ESP_LOGW(TAG, "save seq %lu in slot %c does not fit this build", (unsigned long)m->seq, slot ? 'b' : 'a');
    }
    // Neither record loads: start fresh, but write into the older slot first so the newest record survives.
    s_next_slot = pick ? 'a' : 'b';
    return ESP_ERR_INVALID_VERSION;
}

void saver_set_time_source(bool (*now)(int64_t *unix_s)) { s_now = now; }

static esp_err_t save_at_locked(int64_t unix_s, bool trusted)
{
    if (s_frozen) return ESP_ERR_INVALID_STATE;  // an imported record waits for the restart
    size_t len = tc_state_save(s_state, sizeof s_state);
    if (!len) return ESP_FAIL;
    ps_meta_t m = { .pet_id = 0, .time_trusted = trusted, .unix_time = unix_s, .seq = s_seq + 1 };
    memcpy(m.rom_sha1, s_sha1, 20);
    size_t n = ps_record_encode(&m, s_state, len, s_rec, sizeof s_rec);
    if (!n) return ESP_FAIL;
    char key[16];
    ps_key(s_sha1, 0, s_next_slot, key);
    nvs_handle_t h = 0;
    esp_err_t err = open_ns(NVS_READWRITE, &h);
    if (err == ESP_OK) err = nvs_set_blob(h, key, s_rec, n);
    if (err == ESP_OK) err = nvs_commit(h);
    if (err == ESP_OK) {
        s_seq = m.seq;
        s_next_slot = s_next_slot == 'a' ? 'b' : 'a';
    } else {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
    }
    if (h) nvs_close(h);
    return err;
}

esp_err_t saver_save_at(int64_t unix_s, bool trusted)
{
    lock();
    esp_err_t err = save_at_locked(unix_s, trusted);
    unlock();
    return err;
}

static esp_err_t save_now(void)
{
    int64_t t = 0;
    bool trusted = s_now ? s_now(&t) : false;
    esp_err_t err = saver_save_at(t, trusted);
    if (err == ESP_OK) ESP_LOGI(TAG, "saved seq %lu (%s time)", (unsigned long)s_seq, trusted ? "trusted" : "untrusted");
    return err;
}

void saver_request(void) { s_requested = true; }

bool saver_request_and_wait(uint32_t timeout_ms)
{
    if (!s_done || s_frozen) return false;
    xSemaphoreTake(s_done, 0);
    s_requested = true;
    return xSemaphoreTake(s_done, pdMS_TO_TICKS(timeout_ms)) == pdTRUE && s_last_ok;
}

void saver_poll(uint32_t now_ms)
{
    if (!s_policy_ready) {
        ps_policy_init(&s_policy, now_ms);
        s_policy_ready = true;
    }
    if (s_frozen) {  // nothing more is saved this boot; release a waiter at once
        if (s_requested) {
            s_requested = false;
            s_last_ok = false;
            if (s_done) xSemaphoreGive(s_done);
        }
        return;
    }
    uint8_t b = tc_applied_buttons();
    ps_policy_on_buttons(&s_policy, now_ms, s_prev_buttons, b);
    s_prev_buttons = b;
    if (s_requested || ps_policy_due(&s_policy, now_ms)) {
        bool requested = s_requested;
        s_requested = false;
        s_last_ok = save_now() == ESP_OK;
        if (s_last_ok) ps_policy_saved(&s_policy, now_ms);
        else ps_policy_failed(&s_policy, now_ms);  // don't hammer a failing NVS every loop
        if (requested && s_done) xSemaphoreGive(s_done);
    }
}

void saver_serve_request_at(int64_t unix_s, bool trusted)
{
    if (!s_requested) return;
    s_requested = false;
    s_last_ok = saver_save_at(unix_s, trusted) == ESP_OK;
    if (s_done) xSemaphoreGive(s_done);
}

// Caller holds the lock. Newest valid record of a pet in buf_a or buf_b; *slot is 'a' or 'b'.
static uint8_t s_imp_a[sizeof s_rec], s_imp_b[sizeof s_rec];

static esp_err_t newest_locked(const uint8_t sha1[20], ps_meta_t *meta, char *slot, const uint8_t **rec, size_t *rec_len)
{
    nvs_handle_t h;
    if (open_ns(NVS_READONLY, &h) != ESP_OK) return ESP_ERR_NOT_FOUND;  // no namespace yet: no saves
    ps_meta_t ma, mb;
    const uint8_t *p;
    size_t pl, na = 0, nb = 0;
    bool a = read_slot_of(h, sha1, 'a', &ma, &p, &pl, s_imp_a, sizeof s_imp_a, &na);
    bool b = read_slot_of(h, sha1, 'b', &mb, &p, &pl, s_imp_b, sizeof s_imp_b, &nb);
    nvs_close(h);
    int pick = ps_pick_newest(a, &ma, b, &mb);
    if (pick < 0) return ESP_ERR_NOT_FOUND;
    *meta = pick ? mb : ma;
    *slot = pick ? 'b' : 'a';
    *rec = pick ? s_imp_b : s_imp_a;
    *rec_len = pick ? nb : na;
    return ESP_OK;
}

esp_err_t saver_read_newest(const uint8_t sha1[20], ps_meta_t *meta, uint8_t *rec, size_t cap, size_t *len)
{
    ps_meta_t m;
    char slot;
    const uint8_t *r;
    size_t n;
    lock();
    esp_err_t err = newest_locked(sha1, &m, &slot, &r, &n);
    if (err == ESP_OK && rec) {
        if (n > cap) err = ESP_ERR_INVALID_SIZE;
        else memcpy(rec, r, n);
    }
    unlock();
    if (err == ESP_OK) {
        if (meta) *meta = m;
        if (len) *len = rec ? n : 0;
    }
    return err;
}

esp_err_t saver_import(const uint8_t sha1[20], const uint8_t *rec, size_t len)
{
    ps_meta_t in, newest;
    const uint8_t *p;
    size_t pl;
    if (!ps_record_decode(rec, len, &in, &p, &pl) || memcmp(in.rom_sha1, sha1, 20) != 0) return ESP_ERR_INVALID_ARG;
    lock();
    bool running = s_loaded && memcmp(sha1, s_sha1, 20) == 0;
    bool was_frozen = s_frozen;
    // Freeze before anything else: holding the lock, no save is in flight, and none can start after this.
    if (running) s_frozen = true;
    char slot;
    const uint8_t *r;
    size_t n;
    esp_err_t err = newest_locked(sha1, &newest, &slot, &r, &n);
    char target;
    if (err == ESP_OK) {
        target = slot == 'a' ? 'b' : 'a';  // the older slot: the newest record stays intact
        err = in.seq == newest.seq + 1 ? ESP_OK : ESP_ERR_INVALID_STATE;
    } else {
        target = 'a';  // no valid record: either slot will do
        err = ESP_OK;
    }
    nvs_handle_t h = 0;
    bool written = false;
    if (err == ESP_OK) {
        char key[16];
        ps_key(sha1, 0, target, key);
        err = open_ns(NVS_READWRITE, &h);
        if (err == ESP_OK) err = nvs_set_blob(h, key, rec, len);
        written = err == ESP_OK;
        if (written) nvs_commit(h);  // NVS writes are already durable; commit is a formality here
        if (h) nvs_close(h);
    }
    // NVS keeps the old entry when a write fails, so an error means nothing landed and the running pet
    // may go on saving. A success keeps the freeze: the caller restarts.
    if (!written && running && !was_frozen) s_frozen = false;
    unlock();
    if (err == ESP_OK)
        ESP_LOGW(TAG, "imported save seq %lu into slot %c%s", (unsigned long)in.seq, target,
                 running ? "; saving stopped until the restart" : "");
    else
        ESP_LOGE(TAG, "import failed: %s", esp_err_to_name(err));
    return err;
}
