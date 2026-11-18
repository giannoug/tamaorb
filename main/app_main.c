// SPDX-License-Identifier: GPL-3.0-or-later
// Boot sequence. Later tasks add saves, time, catch-up, the picker and power handling.
#include <stdio.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board.h"
#include "emu_task.h"
#include "rom_store.h"
#include "saver.h"
#include "power.h"
#include "press_task.h"
#include "remote_ops.h"
#include "hostlink_task.h"
#include "net.h"
#include "pet_clock.h"
#include "nvs_flash.h"
#include "tama_core.h"
#include "tc_pet.h"
#include "tc_rom.h"
#include "tc_time.h"
#include "settings_store.h"
#include "ui_task.h"
#include "web.h"

static const char *TAG = "tamaorb";

static uint8_t s_rom[TC_ROM_MAX_BYTES];
static size_t s_rom_len;
static uint16_t s_program[TC_ROM_MAX_WORDS];

static void show_no_rom(const tc_rom_entry_t *roms, int n)
{
    for (int i = 0; i < n; i++) {
        if (roms[i].model != TC_MODEL_UNKNOWN) continue;
        char body[96], hex[41];
        for (int k = 0; k < 20; k++) sprintf(hex + 2 * k, "%02x", roms[i].sha1[k]);
        snprintf(body, sizeof body, "%.19s\nis not a known dump\nSHA-1 %.4s...%.4s", roms[i].name, hex, hex + 36);
        ui_message("Unknown ROM", body);
        return;
    }
    ui_message("No ROM found", "Put a P1, P2, Angel\nor Mothra dump in\n/tamaorb/roms/ on the\nSD card, or push one\nover USB.");
}

// Picks the ROM to run. Returns its model, or TC_MODEL_UNKNOWN if there is none.
static tc_model_t choose_rom(void)
{
    static tc_rom_entry_t roms[16];
    uint8_t sha1[20];
    int n = rom_store_scan(roms, 16), idx = -1;
    bool cached = rom_store_cached(s_rom, &s_rom_len, sha1);
    switch (tc_rom_select(cached, roms, n, &idx)) {
    case TC_SEL_USE_CACHED: {
        tc_model_t m = tc_rom_identify(s_rom, s_rom_len, NULL);
        if (!rom_store_sd_present()) {
            char body[80];
            snprintf(body, sizeof body, "Using the saved copy\nof %s.\nYour pet is fine.", tc_model_name(m));
            ui_message("No SD card", body);
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
        return m;
    }
    case TC_SEL_PICKER:
        idx = ui_pick_rom(roms, n);
        /* fall through */
    case TC_SEL_USE_SD:
        if (idx >= 0 && rom_store_read_sd(roms[idx].name, s_rom, &s_rom_len) && rom_store_set_active(s_rom, s_rom_len) == ESP_OK) {
            ESP_LOGI(TAG, "active ROM: %s", roms[idx].name);
            return roms[idx].model;
        }
        break;
    default:
        break;
    }
    show_no_rom(roms, n);
    return TC_MODEL_UNKNOWN;
}

static void save_for_power(void) { saver_request_and_wait(3000); }

void app_main(void)
{
    power_early_boot_check();  // asleep for low battery and USB still absent: back to sleep
    ESP_LOGI(TAG, "TamaOrb %s", esp_app_get_description()->version);
    ESP_ERROR_CHECK(board_init());
    esp_err_t nvs = nvs_flash_init();  // default partition: Wi-Fi calibration and credentials
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);
    ESP_ERROR_CHECK(settings_init());
    st_settings_t st;
    settings_get(&st);  // name and ntp apply from boot to boot; everything else is read live
    board_volume_set(st_effective_volume(&st));
    ESP_ERROR_CHECK(ui_init());
    ESP_ERROR_CHECK(rom_store_init());
    if (!rom_store_nvs_ok()) {
        ui_message("Storage problem", "Saving is off.\nYour saved pets are\nkept; update the\nfirmware.");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    saver_set_time_source(net_now);
    net_start(st.name, st.ntp);

    tc_model_t model = choose_rom();
    hostlink_task_start(model);  // also serves the no-ROM case, so a ROM can be pushed
    if (model == TC_MODEL_UNKNOWN) {
        ESP_LOGW(TAG, "no usable ROM; waiting for one over USB");
        return;
    }
    ESP_LOGI(TAG, "running %s", tc_model_name(model));
    tc_rom_decode(s_rom, s_rom_len, s_program);
    pet_clock_init(model);
    ui_task_start(model);  // shows "Starting" until the emulator runs
    if (!emu_init(s_program)) return;
    tc_set_icon_pins(tc_model_icon_pins(model));  // Mothra: icons tamalib can't wire
    if (tc_pet_clock_addr(model) >= 0) tc_set_clock_addr((uint16_t)tc_pet_clock_addr(model));
    uint8_t sha1[20];
    tc_rom_identify(s_rom, s_rom_len, sha1);
    ps_meta_t meta;
    uint64_t gap_s = 0;
    if (saver_load(sha1, &meta) == ESP_OK) {
        int64_t now = 0;
        // After deep sleep the RTC ran on its RC oscillator (several % drift): prefer NTP if it comes.
        if (esp_reset_reason() == ESP_RST_DEEPSLEEP && net_configured()) net_wait_fresh_sync(15000);
        bool trusted = net_now(&now);
        if (!trusted && net_configured()) trusted = net_wait_time(15000) && net_now(&now);
        if (!tc_gap_seconds(meta.unix_time, meta.time_trusted, now, trusted, &gap_s)) {
            gap_s = 0;
            ESP_LOGI(TAG, "time unknown; resuming where the save left off");
        }
    } else {
        ESP_LOGI(TAG, "no save for this ROM; starting fresh");
        meta.unix_time = 0;
        meta.time_trusted = false;
    }
    press_task_start();
    power_set_awake_hook(net_set_low_latency);  // an open debugger: no light sleep, no Wi-Fi power save
    web_start(remote_ops_init(model, sha1, s_rom, s_rom_len), st.name);  // the same SHA-1 the saver loaded
    emu_start(gap_s, meta.unix_time, meta.time_trusted);
    power_start(save_for_power, ui_task_show_low_battery);
}
