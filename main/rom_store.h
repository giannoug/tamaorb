// SPDX-License-Identifier: GPL-3.0-or-later
// ROMs on the SD card (/sdcard/tamaorb/roms) and the active-ROM cache in the "tamaorb" NVS partition.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "tc_rom.h"

#define ROM_STORE_PARTITION "tamaorb"
#define ROM_STORE_DIR       "/sdcard/tamaorb/roms"

esp_err_t rom_store_init(void);                                  // NVS partition + SD mount (missing card is fine)
bool      rom_store_sd_present(void);
int       rom_store_scan(tc_rom_entry_t *out, int max);          // 12,288-byte *.bin files, identified (unknown kept)
bool      rom_store_read_sd(const char *name, uint8_t *rom12288);
bool      rom_store_cached(uint8_t *rom12288, uint8_t sha1[20]);  // verifies the cached copy's SHA-1
esp_err_t rom_store_set_active(const uint8_t *rom12288);
esp_err_t rom_store_write_sd(const char *name, const uint8_t *data, size_t len);
esp_err_t rom_store_remove_sd(const char *name);
