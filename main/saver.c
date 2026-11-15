// SPDX-License-Identifier: GPL-3.0-or-later
#include "saver.h"
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
static SemaphoreHandle_t s_done;
static bool s_policy_ready;

static uint8_t s_state[1024];
static uint8_t s_rec[1024 + PS_OVERHEAD];

static esp_err_t open_ns(nvs_open_mode_t mode, nvs_handle_t *h)
{
    return nvs_open_from_partition(ROM_STORE_PARTITION, "saves", mode, h);
}

static bool read_slot(nvs_handle_t h, char slot, ps_meta_t *m, const uint8_t **payload, size_t *len, uint8_t *buf, size_t cap)
{
    char key[16];
    ps_key(s_sha1, 0, slot, key);
    size_t n = cap;
    return nvs_get_blob(h, key, buf, &n) == ESP_OK && ps_record_decode(buf, n, m, payload, len) &&
           memcmp(m->rom_sha1, s_sha1, 20) == 0;
}

esp_err_t saver_load(const uint8_t sha1[20], ps_meta_t *meta)
{
    memcpy(s_sha1, sha1, 20);
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

esp_err_t saver_save_at(int64_t unix_s, bool trusted)
{
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
    if (!s_done) return false;
    xSemaphoreTake(s_done, 0);
    s_requested = true;
    return xSemaphoreTake(s_done, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void saver_poll(uint32_t now_ms)
{
    if (!s_policy_ready) {
        ps_policy_init(&s_policy, now_ms);
        s_policy_ready = true;
    }
    uint8_t b = tc_applied_buttons();
    ps_policy_on_buttons(&s_policy, now_ms, s_prev_buttons, b);
    s_prev_buttons = b;
    if (s_requested || ps_policy_due(&s_policy, now_ms)) {
        bool requested = s_requested;
        s_requested = false;
        if (save_now() == ESP_OK) ps_policy_saved(&s_policy, now_ms);
        if (requested && s_done) xSemaphoreGive(s_done);
    }
}
