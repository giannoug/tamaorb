// SPDX-License-Identifier: GPL-3.0-or-later
#include "pet_clock.h"
#include <time.h>
#include "emu_task.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net.h"
#include "saver.h"
#include "tama_core.h"
#include "tc_pet.h"

static const char *TAG = "pet_clock";
static tc_model_t s_model;
static volatile uint32_t s_saved_id;  // the last request a save was asked for (pollers may be on several tasks)

void pet_clock_init(tc_model_t model) { s_model = model; }

static bool supported(void) { return s_model == TC_MODEL_P1 || s_model == TC_MODEL_P2; }

pet_clock_status_t pet_clock_available(void)
{
    if (!supported()) return PET_CLOCK_UNSUPPORTED;
    if (emu_phase() != EMU_RUN) return PET_CLOCK_CATCHUP;
    int64_t t;
    if (!net_now(&t)) return PET_CLOCK_NO_TIME;
    return PET_CLOCK_OK;
}

int32_t pet_clock_pet_s(void)
{
    uint8_t ram[TC_PET_RAM_NIBBLES];
    uint32_t s;
    return tc_pet_clock(s_model, ram, tc_get_ram(ram, sizeof ram), &s) ? (int32_t)s : -1;
}

int32_t pet_clock_ball_s(void)
{
    int64_t unix_s;
    if (!net_now(&unix_s)) return -1;
    time_t tt = (time_t)unix_s;
    struct tm tm;
    localtime_r(&tt, &tm);  // TZ is the tz setting (settings_store applies it)
    return tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
}

pet_clock_status_t pet_clock_start(uint32_t *id)
{
    pet_clock_status_t a = pet_clock_available();
    if (a != PET_CLOCK_OK) return a;
    int32_t ball = pet_clock_ball_s();
    if (ball < 0) return PET_CLOCK_NO_TIME;
    *id = tc_clock_request((uint32_t)ball);
    ESP_LOGI(TAG, "request %lu: %02ld:%02ld:%02ld", (unsigned long)*id, (long)(ball / 3600), (long)(ball / 60 % 60),
             (long)(ball % 60));
    return PET_CLOCK_OK;
}

pet_clock_status_t pet_clock_poll(uint32_t id, int32_t *written_s)
{
    uint32_t w = 0;
    switch (tc_clock_poll(id, &w)) {
    case TC_CLOCK_PENDING: return PET_CLOCK_PENDING;
    case TC_CLOCK_APPLIED:
        if (written_s) *written_s = (int32_t)w;
        if (s_saved_id != id) {
            s_saved_id = id;
            saver_request();  // a reboot keeps the new clock
            ESP_LOGI(TAG, "request %lu applied", (unsigned long)id);
        }
        return PET_CLOCK_OK;
    default:
        return PET_CLOCK_BUSY;
    }
}

pet_clock_status_t pet_clock_sync(int32_t *written_s)
{
    uint32_t id;
    pet_clock_status_t st = pet_clock_start(&id);
    if (st != PET_CLOCK_OK) return st;
    for (int waited = 0; waited < 3000; waited += 20) {  // the emulator gives up after 2 s of its own time
        st = pet_clock_poll(id, written_s);
        if (st != PET_CLOCK_PENDING) return st;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_LOGW(TAG, "request %lu: no answer in 3 s", (unsigned long)id);
    return PET_CLOCK_BUSY;
}
