// SPDX-License-Identifier: GPL-3.0-or-later
// Test controls for the fakes main/pet_store.c and main/rom_switch.c link against on the host: the SD card and
// ROM cache (rom_store), the saver (records in the fake NVS, namespace "saves"), the emulator phase, restarts.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "emu_task.h"

void fake_idf_reset(void);  // also resets the fake NVS
void fake_rom_add_sd(const char *file, const uint8_t sha1[20]);
// rom_store_cached reports this ROM (activate_rom's "already cached" path); rom_store_read_sd always fails.
void fake_rom_set_cached(const uint8_t sha1[20]);
// An encoded save record (tiny payload) in slot 'a' of that pet.
void fake_saver_put(const uint8_t sha1[20], uint8_t pet, uint32_t seq, int64_t unix_s, bool trusted);
void fake_saver_set_running(const uint8_t sha1[20], uint8_t pet);
void fake_saver_fail_save(bool fail);  // saver_request_and_wait returns false
void fake_emu_set_phase(emu_phase_t p);
int  fake_restarts(void);             // esp_restart calls
