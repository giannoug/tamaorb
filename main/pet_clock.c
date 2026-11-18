// SPDX-License-Identifier: GPL-3.0-or-later
#include "pet_clock.h"
#include <sys/time.h>
#include <time.h>
#include "emu_task.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net.h"
#include "saver.h"
#include "tama_core.h"
#include "tc_pet.h"
#include <string.h>
#include "press_task.h"

static const char *TAG = "pet_clock";
static tc_model_t s_model;
static volatile uint32_t s_saved_id;  // the last request a save was asked for (pollers may be on several tasks)

void pet_clock_init(tc_model_t model) { s_model = model; }

static bool supported(void) { return tc_pet_has_clock(s_model); }

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
    int64_t unix_s;
    if (!net_now(&unix_s)) return PET_CLOCK_NO_TIME;
    struct timeval tv;  // to the millisecond: the core rounds the pet's second to the nearest real one
    gettimeofday(&tv, NULL);
    time_t tt = tv.tv_sec;
    struct tm tm;
    localtime_r(&tt, &tm);
    uint32_t ms = (uint32_t)(tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec) * 1000u + (uint32_t)(tv.tv_usec / 1000);
    *id = tc_clock_request_ms(ms);
    ESP_LOGI(TAG, "request %lu: %02lu:%02lu:%02lu.%03lu", (unsigned long)*id, (unsigned long)(ms / 3600000),
             (unsigned long)(ms / 60000 % 60), (unsigned long)(ms / 1000 % 60), (unsigned long)(ms % 1000));
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

static void auto_task(void *arg)
{
    const char *why = arg;
    while (emu_phase() != EMU_RUN) vTaskDelay(pdMS_TO_TICKS(200));
    vTaskDelay(pdMS_TO_TICKS(1000));  // let the ROM settle after a restore or a replay
    const char *spec = tc_pet_setup_exit(s_model);
    if (!strcmp(why, "new pet") && spec) {
        rm_press_req_t r;
        rm_press_defaults(&r);
        r.gap_ms = TC_PET_SETUP_GAP_MS;
        rm_err_t e = rm_press_parse(&r, spec) ? press_run(&r) : RM_ERR_BAD;
        ESP_LOGI(TAG, "new pet: setup exit \"%s\": %s", spec, e == RM_OK ? "pressed" : "not pressed");
    }
    pet_clock_status_t st = PET_CLOCK_BUSY;
    int32_t w = -1;
    for (int i = 0; i < 5 && st != PET_CLOCK_OK && st != PET_CLOCK_UNSUPPORTED && st != PET_CLOCK_NO_TIME; i++) {
        if (i) vTaskDelay(pdMS_TO_TICKS(2000));
        st = pet_clock_sync(&w);
    }
    if (st == PET_CLOCK_OK)
        ESP_LOGI(TAG, "%s: pet clock set to %02d:%02d:%02d", why, (int)(w / 3600), (int)(w / 60 % 60), (int)(w % 60));
    else
        ESP_LOGW(TAG, "%s: pet clock not set (%d)", why, (int)st);
    vTaskDelete(NULL);
}

void pet_clock_auto(const char *why)
{
    if (xTaskCreatePinnedToCore(auto_task, "pet_clock", 4096, (void *)why, 3, NULL, 0) != pdPASS)
        ESP_LOGW(TAG, "%s: no task for the pet clock", why);
}
