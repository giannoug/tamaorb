// SPDX-License-Identifier: GPL-3.0-or-later
#include "rom_switch.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "emu_task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "rom_store.h"
#include "saver.h"
#include "tc_rom.h"

static const char *TAG = "switch";
static atomic_bool s_restart, s_switching;

static void restart_cb(void *arg) { esp_restart(); }

void app_restart_after(uint32_t ms)
{
    if (atomic_exchange(&s_restart, true)) return;
    ESP_LOGI(TAG, "restarting in %lu ms", (unsigned long)ms);
    const esp_timer_create_args_t args = { .callback = restart_cb, .name = "restart" };
    esp_timer_handle_t t;
    if (esp_timer_create(&args, &t) != ESP_OK || esp_timer_start_once(t, (uint64_t)ms * 1000) != ESP_OK)
        esp_restart();  // no timer: restarting now beats never restarting
}

bool app_restart_pending(void) { return atomic_load(&s_restart); }

static esp_err_t switch_locked(const char *sd_name)
{
    // Only names the picker would offer: a P1/P2 dump in the ROM folder.
    static tc_rom_entry_t roms[16];
    int n = rom_store_scan(roms, 16), found = -1;
    for (int i = 0; i < n; i++)
        if (!strcmp(roms[i].name, sd_name) && (roms[i].model == TC_MODEL_P1 || roms[i].model == TC_MODEL_P2)) found = i;
    if (found < 0) return ESP_ERR_NOT_FOUND;
    uint8_t want[20];
    memcpy(want, roms[found].sha1, 20);

    if (!saver_request_and_wait(3000)) {
        ESP_LOGW(TAG, "not switching: the save failed");
        return ESP_FAIL;
    }
    // Internal RAM, like the boot path's buffer: it is the source of a flash write.
    uint8_t *rom = heap_caps_malloc(TC_ROM_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!rom) return ESP_ERR_NO_MEM;
    uint8_t sha1[20];
    esp_err_t err = ESP_ERR_NOT_FOUND;
    if (rom_store_read_sd(sd_name, rom) && tc_rom_identify(rom, TC_ROM_BYTES, sha1) != TC_MODEL_UNKNOWN &&
        !memcmp(sha1, want, 20))
        err = rom_store_set_active(rom);
    free(rom);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "switch to %s failed: %s", sd_name, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "active ROM is now %s", sd_name);
    app_restart_after(1000);
    return ESP_OK;
}

esp_err_t rom_switch_to(const char *sd_name)
{
    if (emu_phase() != EMU_RUN) return ESP_ERR_INVALID_STATE;
    if (app_restart_pending() || atomic_exchange(&s_switching, true)) return ESP_ERR_NOT_ALLOWED;
    esp_err_t err = switch_locked(sd_name);
    if (err != ESP_OK) atomic_store(&s_switching, false);  // on success it stays set until the restart
    return err;
}
