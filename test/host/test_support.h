// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Reads <TAMAORB_ROM_DIR>/<name> (default build-host/roms) into buf.
// Returns false when the file is missing or not exactly `size` bytes.
bool test_load_file(const char *name, uint8_t *buf, size_t size);

// The ROM a ROM-gated test runs: TAMAORB_TEST_ROM when set (CTest's *_testrom entries); else `real` ("tama.bin")
// when that dump is in the ROM directory; else the homebrew test ROM, testclock.bin, which the build generates
// (tools/gen_testrom.py). So CI, which has no dumps, runs every one of these tests on the test ROM.
const char *test_rom_name(const char *real);
bool test_homebrew_rom(void);  // the ROM test_rom_name("tama.bin") picks is the test ROM
// With TAMAORB_TEST_ROM set, a ROM that didn't load fails the run (exit 1) instead of skipping it. Returns loaded.
bool test_require_rom(bool loaded);

// Parses 40 hex characters into 20 bytes.
void test_hex20(const char *hex, uint8_t out[20]);
