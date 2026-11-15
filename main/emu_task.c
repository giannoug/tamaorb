// SPDX-License-Identifier: GPL-3.0-or-later
// Emulator task: runs tamalib in real time on core 1 (spec §8).
#include "emu_task.h"
#include "board.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tama_core.h"
#include "tc_time.h"

static const char *TAG = "emu";
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static uint64_t s_slept_us;

static uint64_t now_us(void) { return (uint64_t)esp_timer_get_time(); }

static void sleep_until_us(uint64_t ts)
{
    uint64_t now = now_us();
    if (ts <= now) return;
    uint64_t d = ts - now;
    int64_t t0 = esp_timer_get_time();
    vTaskDelay(pdMS_TO_TICKS(d / 1000));
    s_slept_us += (uint64_t)(esp_timer_get_time() - t0);
}

static void lock(void) { taskENTER_CRITICAL(&s_mux); }
static void unlock(void) { taskEXIT_CRITICAL(&s_mux); }

// Buzzer changes go straight to the audio driver: polling from the UI task missed short key clicks.
static void tone(uint32_t freq_dhz, bool on) { board_tone(freq_dhz / 10, on); }

static const tc_platform_t s_plat = { .now_us = now_us, .sleep_until_us = sleep_until_us, .tone = tone,
                                      .lock = lock, .unlock = unlock };

static void emu_task(void *arg)
{
    tc_step(1);  // first step after init; start measuring from here
    int64_t stat_t0 = esp_timer_get_time();
    uint32_t stat_ticks0 = tc_tick_counter();
    uint64_t stat_slept0 = s_slept_us;
    for (;;) {
        tc_step(256);
        int64_t now = esp_timer_get_time();
        if (now - stat_t0 >= 60 * 1000000LL) {  // once a minute: pace and load
            double wall = (double)(now - stat_t0) / 1e6;
            double emu = (double)tc_ticks_since(stat_ticks0, tc_tick_counter()) / TC_TICK_HZ;
            double busy = 100.0 * (1.0 - (double)(s_slept_us - stat_slept0) / 1e6 / wall);
            ESP_LOGI(TAG, "pace: %.3f emulated s in %.3f wall s (%+.3f s), core 1 busy ~%.1f%%", emu, wall, emu - wall, busy);
            stat_t0 = now;
            stat_ticks0 = tc_tick_counter();
            stat_slept0 = s_slept_us;
        }
    }
}

void emu_task_start(const uint16_t *program)
{
    if (!tc_init(program, &s_plat)) {
        ESP_LOGE(TAG, "tc_init failed");
        return;
    }
    xTaskCreatePinnedToCore(emu_task, "emu", 8192, NULL, 10, NULL, 1);
}
