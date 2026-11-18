// SPDX-License-Identifier: GPL-3.0-or-later
// Emulator task: runs tamalib in real time on core 1.
#include "emu_task.h"
#include "board.h"
#include "press_task.h"
#include "pet_clock.h"
#include "saver.h"
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

static esp_timer_handle_t s_wake;
static TaskHandle_t s_sleeper;
static void wake_sleeper(void *arg) { xTaskNotifyGive(s_sleeper); }

// To the microsecond, never early: a beep waits here for its moment, and vTaskDelay's 1 ms ticks could end up to
// ~2 ms before it. A one-shot timer wakes the task; light sleep still happens until that timer is due.
static void sleep_until_us(uint64_t ts)
{
    int64_t t0 = esp_timer_get_time();
    if (ts <= (uint64_t)t0) return;
    if (!s_wake) {
        const esp_timer_create_args_t a = { .callback = wake_sleeper, .name = "emu_wake" };
        if (esp_timer_create(&a, &s_wake) != ESP_OK) { vTaskDelay(pdMS_TO_TICKS((ts - t0 + 999) / 1000)); return; }
    }
    s_sleeper = xTaskGetCurrentTaskHandle();
    for (;;) {
        int64_t left = (int64_t)ts - esp_timer_get_time();
        if (left <= 0) break;
        esp_timer_stop(s_wake);  // not running: harmless
        if (esp_timer_start_once(s_wake, (uint64_t)left) != ESP_OK) { vTaskDelay(1); continue; }
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    s_slept_us += (uint64_t)(esp_timer_get_time() - t0);
}

static void lock(void) { taskENTER_CRITICAL(&s_mux); }
static void unlock(void) { taskEXIT_CRITICAL(&s_mux); }

// Buzzer changes go straight to the audio driver: polling from the UI task missed short key clicks.
// agent_feedback = silent mutes the speaker during agent press sequences (one flag read).
static void tone(uint32_t freq_dhz, bool on) { board_tone(freq_dhz / 10, on && !g_agent_mute); }

// A silent agent press makes no sound, so it doesn't start the speaker either (it would hold off light sleep).
static void tone_prepare(void) { if (!g_agent_mute) board_audio_prepare(); }

static const tc_platform_t s_plat = { .now_us = now_us, .sleep_until_us = sleep_until_us, .tone = tone,
                                      .lock = lock, .unlock = unlock, .tone_prepare = tone_prepare };

#define PROGRESS_SAVE_US (120 * 1000000LL)

static volatile emu_phase_t s_phase = EMU_BOOT;
static uint64_t s_catchup_s;
static bool s_ask;
static TaskHandle_t s_task;
static volatile int s_answer = -1;      // emu_answer_t, -1 none yet
static volatile bool s_skip;
static int64_t s_saved_unix;
static bool s_saved_trusted;
static volatile uint32_t s_done_s, s_total_s;  // 32-bit: read from the other core without tearing
static volatile uint32_t s_eta_s;
static int64_t s_catchup_t0, s_last_progress_save;
static volatile float s_catchup_speed;  // x real time of this boot's catch-up; 0 until one has run

emu_phase_t emu_phase(void) { return s_phase; }

// Until this boot has caught up, report the speed measured on the ball with tamalib 223ba64 (2026-11-17).
float emu_catchup_speed(void) { return s_catchup_speed > 0 ? s_catchup_speed : 75.0f; }

void emu_catchup_progress(uint64_t *done_s, uint64_t *total_s, uint32_t *eta_s)
{
    *done_s = s_done_s;
    *total_s = s_total_s ? s_total_s : s_catchup_s;
    *eta_s = s_eta_s;
}

static bool catchup_cb(uint64_t done_ticks, uint64_t total_ticks, void *ctx)
{
    int64_t now = esp_timer_get_time();
    s_done_s = (uint32_t)(done_ticks / TC_TICK_HZ);
    s_total_s = (uint32_t)(total_ticks / TC_TICK_HZ);
    saver_serve_request_at(tc_progress_unix(s_saved_unix, done_ticks), s_saved_trusted);  // low battery / reboot
    double elapsed = (double)(now - s_catchup_t0) / 1e6;
    double rate = elapsed > 0 ? (double)done_ticks / elapsed : 0;  // ticks per wall second
    s_eta_s = rate > 0 ? (uint32_t)((double)(total_ticks - done_ticks) / rate) : 0;
    if (now - s_last_progress_save >= PROGRESS_SAVE_US) {
        saver_save_at(tc_progress_unix(s_saved_unix, done_ticks), s_saved_trusted);
        s_last_progress_save = now;
    }
    vTaskDelay(1);  // let core 1's idle task run (task watchdog) during the full-speed replay
    return !s_skip;
}

bool emu_answer(emu_answer_t a)
{
    if (s_phase != EMU_WAIT || !s_task) return false;
    s_answer = (int)a;
    xTaskNotifyGive(s_task);
    return true;
}

bool emu_skip(void)
{
    if (s_phase != EMU_CATCHUP) return false;
    s_skip = true;
    return true;
}

uint64_t emu_wait_gap_s(void) { return s_phase == EMU_WAIT ? s_catchup_s : 0; }

// The question: the pet doesn't run and nothing is saved until an answer comes.
static void wait_for_answer(void)
{
    s_phase = EMU_WAIT;
    ESP_LOGI(TAG, "away %llu s: waiting for Resume or Fast-forward", (unsigned long long)s_catchup_s);
    while (s_answer < 0) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "answer: %s", s_answer == EMU_ANSWER_RESUME ? "resume" : "fast-forward");
    if (s_answer == EMU_ANSWER_RESUME) {
        s_catchup_s = 0;
        saver_request();  // stamped with the current time at the first loop: the gap isn't counted again
        pet_clock_auto("resume");
    }
}

static void run_catchup(void)
{
    s_phase = EMU_CATCHUP;
    s_catchup_t0 = s_last_progress_save = esp_timer_get_time();
    ESP_LOGI(TAG, "catching up %llu s", (unsigned long long)s_catchup_s);
    // Converges: the wall time spent replaying is replayed too, so the pet ends at "now".
    uint64_t ran = tc_catchup_converge(s_catchup_s * TC_TICK_HZ, now_us, catchup_cb, NULL, TC_TICK_HZ * 10);
    double wall = (double)(esp_timer_get_time() - s_catchup_t0) / 1e6;
    double speed = wall > 0 ? (double)ran / TC_TICK_HZ / wall : 0;
    ESP_LOGI(TAG, "caught up %llu s in %.1f s (%.1fx)", (unsigned long long)(ran / TC_TICK_HZ), wall, speed);
    if (speed > 0) s_catchup_speed = (float)speed;
    saver_request();  // stamp the caught-up (or skipped) state with the current time
    if (s_skip) {
        ESP_LOGI(TAG, "skipped after %llu s", (unsigned long long)(ran / TC_TICK_HZ));
        pet_clock_auto("skip");
    }
}

static void emu_task(void *arg)
{
    if (s_ask) wait_for_answer();
    if (s_catchup_s) run_catchup();
    s_phase = EMU_RUN;
    tc_step(1);  // first step after init; start measuring from here
    int64_t stat_t0 = esp_timer_get_time();
    uint32_t stat_ticks0 = tc_tick_counter();
    uint64_t stat_slept0 = s_slept_us;
    for (;;) {
        tc_step(256);
        int64_t now = esp_timer_get_time();
        saver_poll((uint32_t)(now / 1000));
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

bool emu_init(const uint16_t *program)
{
    if (tc_init(program, &s_plat)) return true;
    ESP_LOGE(TAG, "tc_init failed");
    return false;
}

void emu_start(uint64_t catchup_s, int64_t saved_unix, bool saved_trusted, bool ask)
{
    s_catchup_s = catchup_s;
    s_saved_unix = saved_unix;
    s_saved_trusted = saved_trusted;
    s_ask = ask && catchup_s > 0;
    xTaskCreatePinnedToCore(emu_task, "emu", 8192, NULL, 10, &s_task, 1);
}
