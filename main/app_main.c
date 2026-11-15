// SPDX-License-Identifier: GPL-3.0-or-later
// Boot sequence (spec §6.4). Later tasks add saves, time, catch-up, the picker and power handling.
#include <string.h>
#include "esp_app_desc.h"
#include "esp_log.h"
#include "board.h"
#include "emu_task.h"
#include "rom_store.h"
#include "saver.h"
#include "net.h"
#include "nvs_flash.h"
#include "tc_rom.h"
#include "tc_time.h"
#include "ui_task.h"

static const char *TAG = "tamaorb";

static uint8_t s_rom[TC_ROM_BYTES];
static uint16_t s_program[TC_ROM_WORDS];

// Picks the ROM to run. Returns its model, or TC_MODEL_UNKNOWN if there is none.
static tc_model_t choose_rom(void)
{
    static tc_rom_entry_t roms[8];
    uint8_t sha1[20];
    int n = rom_store_scan(roms, 8), idx = -1;
    bool cached = rom_store_cached(s_rom, sha1);
    switch (tc_rom_select(cached, roms, n, &idx)) {
    case TC_SEL_USE_CACHED:
        return tc_rom_identify(s_rom, sizeof s_rom, NULL);
    case TC_SEL_PICKER:
        // Task 20 adds the on-screen picker; until then take the first valid entry.
        for (idx = 0; idx < n && roms[idx].model != TC_MODEL_P1 && roms[idx].model != TC_MODEL_P2; idx++) {}
        /* fall through */
    case TC_SEL_USE_SD:
        if (idx < n && rom_store_read_sd(roms[idx].name, s_rom) && rom_store_set_active(s_rom) == ESP_OK) {
            ESP_LOGI(TAG, "active ROM: %s", roms[idx].name);
            return roms[idx].model;
        }
        return TC_MODEL_UNKNOWN;
    default:
        return TC_MODEL_UNKNOWN;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "TamaOrb %s", esp_app_get_description()->version);
    ESP_ERROR_CHECK(board_init());
    esp_err_t nvs = nvs_flash_init();  // default partition: Wi-Fi calibration and credentials
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);
    ESP_ERROR_CHECK(rom_store_init());
    saver_set_time_source(net_now);
    net_start();

    tc_model_t model = choose_rom();
    if (model == TC_MODEL_UNKNOWN) {
        ESP_LOGE(TAG, "no usable ROM");  // Task 20 shows a message screen
        return;
    }
    ESP_LOGI(TAG, "running %s", tc_model_name(model));
    tc_rom_decode(s_rom, sizeof s_rom, s_program);
    ui_task_start(model);  // shows "Starting" until the emulator runs
    if (!emu_init(s_program)) return;
    uint8_t sha1[20];
    tc_rom_identify(s_rom, sizeof s_rom, sha1);
    ps_meta_t meta;
    uint64_t gap_s = 0;
    if (saver_load(sha1, &meta) == ESP_OK) {
        int64_t now = 0;
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
    emu_start(gap_s, meta.unix_time, meta.time_trusted);
}
