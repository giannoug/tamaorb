// SPDX-License-Identifier: GPL-3.0-or-later
// microSD over SDMMC 1-bit (pins from TamaPoke PR #44, tested on this device).
#include "board.h"
#include "ball_pins.h"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

esp_err_t board_sd_mount(const char *mount_point)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = PIN_SD_CLK;
    slot.cmd = PIN_SD_CMD;
    slot.d0 = PIN_SD_D0;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_sdmmc_mount_config_t cfg = { .format_if_mount_failed = false, .max_files = 4, .allocation_unit_size = 16 * 1024 };
    sdmmc_card_t *card;
    return esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot, &cfg, &card);
}
