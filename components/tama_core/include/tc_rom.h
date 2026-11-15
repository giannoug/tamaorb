// SPDX-License-Identifier: GPL-3.0-or-later
// ROM decoding and identification. Portable: no ESP-IDF headers.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { TC_MODEL_UNKNOWN = 0, TC_MODEL_P1, TC_MODEL_P2, TC_MODEL_TEST_ROM } tc_model_t;

#define TC_ROM_BYTES 12288u
#define TC_ROM_WORDS 6144u

void        tc_sha1(const uint8_t *data, size_t len, uint8_t out[20]);
// sha1_out may be NULL. Wrong sizes are TC_MODEL_UNKNOWN.
tc_model_t  tc_rom_identify(const uint8_t *data, size_t len, uint8_t sha1_out[20]);
// len must be TC_ROM_BYTES; each word is big-endian 16-bit masked to 12 bits.
bool        tc_rom_decode(const uint8_t *data, size_t len, uint16_t *words_out);
const char *tc_model_name(tc_model_t m);
