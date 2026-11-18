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
bool      rom_store_nvs_ok(void);                                // false: saves and the ROM cache are unavailable
int       rom_store_scan(tc_rom_entry_t *out, int max);          // 12 or 16 KB *.bin files, identified (unknown kept)
// rom holds TC_ROM_MAX_BYTES; *len is the file's size (12,288 or 16,384). False for any other size.
bool      rom_store_read_sd(const char *name, uint8_t *rom, size_t *len);
bool      rom_store_find_sd(const uint8_t sha1[20], char name_out[64]);  // the first scanned file with that SHA-1
bool      rom_store_cached(uint8_t *rom, size_t *len, uint8_t sha1[20]);  // verifies the cached copy's SHA-1
esp_err_t rom_store_set_active(const uint8_t *rom, size_t len);
esp_err_t rom_store_write_sd(const char *name, const uint8_t *data, size_t len);
esp_err_t rom_store_remove_sd(const char *name);
