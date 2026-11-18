// SPDX-License-Identifier: GPL-3.0-or-later
// ROM decoding and identification. Portable: no ESP-IDF headers.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { TC_MODEL_UNKNOWN = 0, TC_MODEL_P1, TC_MODEL_P2, TC_MODEL_TEST_ROM, TC_MODEL_ANGEL, TC_MODEL_MOTHRA } tc_model_t;

#define TC_ROM_BYTES_S46 12288u  // E0C6S46 dumps: P1, P2 (and the generated test ROM)
#define TC_ROM_BYTES_S48 16384u  // E0C6S48 dumps: Angel, Mothra
#define TC_ROM_MAX_BYTES 16384u
#define TC_ROM_MAX_WORDS 8192u

bool        tc_rom_size_ok(size_t len);         // 12,288 or 16,384
bool        tc_model_playable(tc_model_t m);    // P1, P2, Angel, Mothra
size_t      tc_model_rom_bytes(tc_model_t m);   // the dump size of a known model; 0 for UNKNOWN

void        tc_sha1(const uint8_t *data, size_t len, uint8_t out[20]);
// sha1_out may be NULL. A size that doesn't match the known model's dump size is TC_MODEL_UNKNOWN.
tc_model_t  tc_rom_identify(const uint8_t *data, size_t len, uint8_t sha1_out[20]);
// len must pass tc_rom_size_ok; writes len/2 words (words_out holds TC_ROM_MAX_WORDS); each word is big-endian
// 16-bit masked to 12 bits.
bool        tc_rom_decode(const uint8_t *data, size_t len, uint16_t *words_out);
// A model from the known SHA-1 table, else UNKNOWN.
tc_model_t  tc_model_from_sha1(const uint8_t sha1[20]);
const char *tc_model_name(tc_model_t m);       // "P1", "P2", "Angel", "Mothra", "test ROM", "unknown"
const char *tc_model_long_name(tc_model_t m);  // "Gen 1", "Gen 2", "Angel", "Mothra"; NULL otherwise

// ---- Active ROM selection ----
typedef struct { char name[64]; uint8_t sha1[20]; tc_model_t model; } tc_rom_entry_t;
typedef enum { TC_SEL_USE_CACHED, TC_SEL_USE_SD, TC_SEL_PICKER, TC_SEL_NO_ROM } tc_sel_action_t;
// The cached (remembered) ROM always wins. Otherwise one distinct valid SD ROM is used, several
// open the picker, none means NO_ROM. Valid = tc_model_playable; duplicate SHA-1s count once.
// *sd_index_out is set for TC_SEL_USE_SD.
// Indices of the distinct playable entries, first occurrence of each SHA-1. Returns the count.
int tc_rom_distinct_valid(const tc_rom_entry_t *sd, int n_sd, int *idx_out, int max);
tc_sel_action_t tc_rom_select(bool have_cached, const tc_rom_entry_t *sd, int n_sd, int *sd_index_out);
