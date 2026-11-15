// SPDX-License-Identifier: GPL-3.0-or-later
#include "press_task.h"
#include <stdatomic.h>
#include "emu_task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "settings_store.h"
#include "tama_core.h"

#define TICK_MS  5
#define WAIT_MS  11000  // longest sequence (10 s) plus slack

static const char *TAG = "press";

volatile bool g_agent_mute;
static volatile bool s_window;
static atomic_bool s_busy;             // claimed by press_run, released by the task when the sequence ends
static atomic_bool s_abandon;          // the caller gave up waiting
static rm_press_req_t s_req;           // handed over under s_busy
static TaskHandle_t s_task;
static SemaphoreHandle_t s_done;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void press_task(void *arg)
{
    rm_press_t p = { 0 };
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // idle: no ticking, light sleep stays possible
        atomic_store(&s_abandon, false);
        st_settings_t cfg;
        settings_get(&cfg);
        uint32_t seen = settings_seq();
        TickType_t wake = xTaskGetTickCount();
        rm_press_begin(&p, &s_req, now_ms());
        uint8_t posted = 0xFF;
        for (;;) {
            uint32_t now = now_ms();
            if (settings_seq() != seen) {
                seen = settings_seq();
                settings_get(&cfg);
            }
            bool over = rm_press_done(&p, now) || atomic_load(&s_abandon);
            uint8_t mask = over ? 0 : rm_press_mask(&p, now);
            if (mask != posted) {
                tc_post_remote_buttons(mask);  // ORed with touch; not touch activity for the screen
                posted = mask;
            }
            s_window = !over;
            g_agent_mute = !over && cfg.agent_feedback == ST_AGENT_SILENT;
            if (over) break;
            vTaskDelayUntil(&wake, pdMS_TO_TICKS(TICK_MS));
        }
        tc_post_remote_buttons(0);
        p.active = false;
        xSemaphoreGive(s_done);  // before releasing s_busy: the next caller drains a give left by an abandoned run
        atomic_store(&s_busy, false);
    }
}

void press_task_start(void)
{
    if (s_task) return;
    s_done = xSemaphoreCreateBinary();
    if (!s_done || xTaskCreatePinnedToCore(press_task, "press", 3072, NULL, 6, &s_task, 0) != pdPASS)
        ESP_LOGE(TAG, "press task not started");
}

rm_err_t press_run(const rm_press_req_t *req)
{
    if (!req || rm_press_validate(req)) return RM_ERR_BAD;  // rm_press_begin trusts its input
    if (!s_task || !s_done) return RM_ERR_IO;
    if (emu_phase() != EMU_RUN) return RM_ERR_CATCHUP;
    if (atomic_exchange(&s_busy, true)) return RM_ERR_BUSY;
    s_req = *req;
    xSemaphoreTake(s_done, 0);  // drop the give of an abandoned run
    xTaskNotifyGive(s_task);
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(WAIT_MS)) == pdTRUE) return RM_OK;
    atomic_store(&s_abandon, true);  // the task releases the buttons and s_busy
    ESP_LOGW(TAG, "sequence overran; abandoned");
    return RM_ERR_IO;
}

bool press_window_active(void) { return s_window; }
