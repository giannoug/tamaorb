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

// ---- Active ROM selection (spec §7.3) ----
typedef struct { char name[64]; uint8_t sha1[20]; tc_model_t model; } tc_rom_entry_t;
typedef enum { TC_SEL_USE_CACHED, TC_SEL_USE_SD, TC_SEL_PICKER, TC_SEL_NO_ROM } tc_sel_action_t;
// The cached (remembered) ROM always wins. Otherwise one distinct valid SD ROM is used, several
// open the picker, none means NO_ROM. Valid = P1 or P2; duplicate SHA-1s count once.
// *sd_index_out is set for TC_SEL_USE_SD.
// Indices of the distinct valid (P1/P2) entries, first occurrence of each SHA-1. Returns the count.
int tc_rom_distinct_valid(const tc_rom_entry_t *sd, int n_sd, int *idx_out, int max);
tc_sel_action_t tc_rom_select(bool have_cached, const tc_rom_entry_t *sd, int n_sd, int *sd_index_out);
