// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings_store.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "settings";
static st_settings_t s_cur;
static st_settings_t s_boot;  // as loaded at boot: name and ntp take effect from these until a restart
static st_writer_t s_writer;
static volatile uint32_t s_seq, s_display_seq;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static bool same(const st_settings_t *a, const st_settings_t *b)
{
    for (int i = 0; i < st_key_count(); i++) {
        char x[80], y[80];
        if (!st_get(a, st_key(i), x, sizeof x) || !st_get(b, st_key(i), y, sizeof y) || strcmp(x, y)) return false;
    }
    return true;
}

static void load(void)
{
    nvs_handle_t h;
    if (nvs_open("settings", NVS_READONLY, &h) != ESP_OK) return;
    for (int i = 0; i < st_key_count(); i++) {
        char v[80];
        size_t n = sizeof v;
        if (nvs_get_str(h, st_key(i), v, &n) == ESP_OK && !st_set(&s_cur, st_key(i), v))
            ESP_LOGW(TAG, "stored %s rejected; default kept", st_key(i));
    }
    nvs_close(h);
}

static void write_all(const st_settings_t *s)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open("settings", NVS_READWRITE, &h);
    if (e != ESP_OK) { ESP_LOGW(TAG, "nvs_open: %s", esp_err_to_name(e)); return; }
    for (int i = 0; i < st_key_count(); i++) {
        char v[80];
        if (st_get(s, st_key(i), v, sizeof v)) e = nvs_set_str(h, st_key(i), v);
    }
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) ESP_LOGW(TAG, "write failed: %s", esp_err_to_name(e));
    else ESP_LOGI(TAG, "saved");
}

static void writer_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(250));
        st_settings_t snap;
        bool due;
        taskENTER_CRITICAL(&s_lock);
        due = st_writer_due(&s_writer, now_ms());
        if (due) { snap = s_cur; st_writer_done(&s_writer); }
        taskEXIT_CRITICAL(&s_lock);
        if (due) write_all(&snap);
    }
}

esp_err_t settings_init(void)
{
    st_defaults(&s_cur, CONFIG_TAMAORB_VOLUME);
    load();
    s_boot = s_cur;
    ESP_LOGI(TAG, "brightness %u dim %u volume %u palette %d", s_cur.brightness, s_cur.dim_level, s_cur.volume, (int)s_cur.palette);
    return xTaskCreatePinnedToCore(writer_task, "settings", 4096, NULL, 3, NULL, 0) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void settings_get(st_settings_t *out)
{
    taskENTER_CRITICAL(&s_lock);
    *out = s_cur;
    taskEXIT_CRITICAL(&s_lock);
}

void settings_put(const st_settings_t *s, bool display_changed)
{
    taskENTER_CRITICAL(&s_lock);
    bool changed = !same(&s_cur, s);
    s_cur = *s;
    if (changed) st_writer_changed(&s_writer, now_ms());
    s_seq++;
    if (display_changed) s_display_seq++;
    taskEXIT_CRITICAL(&s_lock);
}

bool settings_restart_pending(void)
{
    taskENTER_CRITICAL(&s_lock);
    bool p = st_restart_pending(&s_boot, &s_cur);
    taskEXIT_CRITICAL(&s_lock);
    return p;
}

uint32_t settings_seq(void) { return s_seq; }
uint32_t settings_display_seq(void) { return s_display_seq; }
