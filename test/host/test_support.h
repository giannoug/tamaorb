// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Reads <TAMAORB_ROM_DIR>/<name> (default build-host/roms) into buf.
// Returns false when the file is missing or not exactly `size` bytes.
bool test_load_file(const char *name, uint8_t *buf, size_t size);

// Parses 40 hex characters into 20 bytes.
void test_hex20(const char *hex, uint8_t out[20]);
