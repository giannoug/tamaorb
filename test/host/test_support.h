// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Reads <TAMAORB_ROM_DIR>/<name> (default build-host/roms) into buf.
// Returns false when the file is missing or not exactly `size` bytes.
bool test_load_file(const char *name, uint8_t *buf, size_t size);

// The ROM a ROM-gated test runs: `real` ("tama.bin"), or the homebrew test ROM when TAMAORB_TEST_ROM names it
// (CTest's *_testrom entries; tools/gen_testrom.py builds it). With TAMAORB_TEST_ROM set, a missing ROM must fail
// the run rather than skip it: test_require_rom.
const char *test_rom_name(const char *real);
bool test_homebrew_rom(void);
// Fails (exit 1) when TAMAORB_TEST_ROM is set but loaded is false; returns loaded otherwise.
bool test_require_rom(bool loaded);

// Parses 40 hex characters into 20 bytes.
void test_hex20(const char *hex, uint8_t out[20]);
