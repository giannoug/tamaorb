// SPDX-License-Identifier: GPL-3.0-or-later
// Fakes for the ESP-IDF calls and TamaOrb modules main/pet_store.c and main/rom_switch.c use (see fake_idf.h).
#include "fake_idf.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "fake_nvs.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "persist.h"
#include "rom_store.h"
#include "saver.h"

// ---- ESP-IDF odds and ends ----
const char *esp_err_to_name(esp_err_t err) { return err == ESP_OK ? "ESP_OK" : "error"; }
void fake_log(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
void *heap_caps_malloc(size_t size, uint32_t caps) { (void)caps; return malloc(size); }

static int s_restarts;
void esp_restart(void) { s_restarts++; }
int fake_restarts(void) { return s_restarts; }

struct fake_timer { esp_timer_create_args_t args; };
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out)
{
    static struct fake_timer t;  // rom_switch creates one, ever
    t.args = *args;
    *out = &t;
    return ESP_OK;
}
esp_err_t esp_timer_start_once(esp_timer_handle_t t, uint64_t timeout_us)
{
    (void)timeout_us;
    t->args.callback(t->args.arg);  // fires at once
    return ESP_OK;
}
int64_t esp_timer_get_time(void) { return 0; }

static int s_mutex_token;
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return &s_mutex_token; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t wait) { (void)s; (void)wait; return pdTRUE; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t s) { (void)s; return pdTRUE; }

// ---- emulator phase ----
static emu_phase_t s_phase = EMU_RUN;
emu_phase_t emu_phase(void) { return s_phase; }
void fake_emu_set_phase(emu_phase_t p) { s_phase = p; }

// ---- rom_store: an SD card list and a cached ROM ----
static tc_rom_entry_t s_sd[8];
static int s_nsd;
static uint8_t s_cached[20];
static bool s_have_cached;

void fake_rom_add_sd(const char *file, const uint8_t sha1[20])
{
    tc_rom_entry_t *r = &s_sd[s_nsd++];
    snprintf(r->name, sizeof r->name, "%s", file);
    memcpy(r->sha1, sha1, 20);
    r->model = tc_model_from_sha1(sha1);
}

void fake_rom_set_cached(const uint8_t sha1[20])
{
    memcpy(s_cached, sha1, 20);
    s_have_cached = true;
}

int rom_store_scan(tc_rom_entry_t *out, int max)
{
    int n = s_nsd < max ? s_nsd : max;
    memcpy(out, s_sd, sizeof *out * (size_t)n);
    return n;
}

bool rom_store_find_sd(const uint8_t sha1[20], char name_out[64])
{
    for (int i = 0; i < s_nsd; i++)
        if (!memcmp(s_sd[i].sha1, sha1, 20)) {
            snprintf(name_out, 64, "%s", s_sd[i].name);
            return true;
        }
    return false;
}

bool rom_store_cached(uint8_t *rom, size_t *len, uint8_t sha1[20])
{
    (void)rom;
    if (!s_have_cached) return false;
    memcpy(sha1, s_cached, 20);
    *len = tc_model_rom_bytes(tc_model_from_sha1(s_cached));
    return true;
}

bool rom_store_read_sd(const char *name, uint8_t *rom, size_t *len) { (void)name; (void)rom; (void)len; return false; }
esp_err_t rom_store_set_active(const uint8_t *rom, size_t len) { (void)rom; (void)len; return ESP_OK; }

// ---- saver: records in the fake NVS, namespace "saves" ----
static uint8_t s_run_sha[20], s_run_id;
static bool s_running, s_fail_save;

void fake_saver_set_running(const uint8_t sha1[20], uint8_t pet)
{
    memcpy(s_run_sha, sha1, 20);
    s_run_id = pet;
    s_running = true;
}
void fake_saver_fail_save(bool fail) { s_fail_save = fail; }

void fake_saver_put(const uint8_t sha1[20], uint8_t pet, uint32_t seq, int64_t unix_s, bool trusted)
{
    ps_meta_t m = { .pet_id = pet, .time_trusted = trusted, .unix_time = unix_s, .seq = seq };
    memcpy(m.rom_sha1, sha1, 20);
    const uint8_t payload[4] = { 0 };
    uint8_t rec[64];
    size_t len = ps_record_encode(&m, payload, sizeof payload, rec, sizeof rec);
    char key[16];
    ps_key(sha1, pet, 'a', key);
    nvs_handle_t h;
    if (nvs_open_from_partition(ROM_STORE_PARTITION, "saves", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, key, rec, len);
    nvs_commit(h);
    nvs_close(h);
}

bool saver_running(uint8_t sha1_out[20], uint8_t *pet_id)
{
    if (!s_running) return false;
    memcpy(sha1_out, s_run_sha, 20);
    *pet_id = s_run_id;
    return true;
}

bool saver_request_and_wait(uint32_t timeout_ms) { (void)timeout_ms; return !s_fail_save; }

esp_err_t saver_read_newest(const uint8_t sha1[20], uint8_t pet_id, ps_meta_t *meta, uint8_t *rec, size_t cap,
                            size_t *len)
{
    nvs_handle_t h;
    if (nvs_open_from_partition(ROM_STORE_PARTITION, "saves", NVS_READONLY, &h) != ESP_OK) return ESP_ERR_NOT_FOUND;
    uint8_t buf[2][256];
    size_t blen[2] = { sizeof buf[0], sizeof buf[1] };
    ps_meta_t m[2];
    bool ok[2];
    for (int s = 0; s < 2; s++) {
        char key[16];
        ps_key(sha1, pet_id, s ? 'b' : 'a', key);
        const uint8_t *payload;
        size_t plen;
        ok[s] = nvs_get_blob(h, key, buf[s], &blen[s]) == ESP_OK &&
                ps_record_decode(buf[s], blen[s], &m[s], &payload, &plen) && !memcmp(m[s].rom_sha1, sha1, 20);
    }
    nvs_close(h);
    int pick = ps_pick_newest(ok[0], &m[0], ok[1], &m[1]);
    if (pick < 0) return ESP_ERR_NOT_FOUND;
    if (meta) *meta = m[pick];
    if (rec) {
        if (cap < blen[pick]) return ESP_ERR_INVALID_SIZE;
        memcpy(rec, buf[pick], blen[pick]);
    }
    if (len) *len = blen[pick];
    return ESP_OK;
}

esp_err_t saver_erase(const uint8_t sha1[20], uint8_t pet_id)
{
    if (s_running && s_run_id == pet_id && !memcmp(s_run_sha, sha1, 20)) return ESP_ERR_INVALID_STATE;
    nvs_handle_t h;
    if (nvs_open_from_partition(ROM_STORE_PARTITION, "saves", NVS_READWRITE, &h) != ESP_OK) return ESP_FAIL;
    for (int s = 0; s < 2; s++) {
        char key[16];
        ps_key(sha1, pet_id, s ? 'b' : 'a', key);
        nvs_erase_key(h, key);  // a slot never written is fine
    }
    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

void fake_idf_reset(void)
{
    fake_nvs_reset();
    s_restarts = 0;
    s_phase = EMU_RUN;
    s_nsd = 0;
    s_have_cached = false;
    s_running = false;
    s_fail_save = false;
}
