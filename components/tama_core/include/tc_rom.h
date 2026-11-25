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
// Catch-up uses halt fast-forward (tc_set_halt_ff) for this model: true where test/host/test_halt_ff.c proved it
// exact (all four playable models; only Mothra gains much, docs/architecture.md).
bool        tc_model_halt_ff(tc_model_t m);
// Catch-up speed on the ball (x real time, with tc_model_halt_ff's setting): what the "about N min" estimates
// assume until a boot has measured its own. 75 for models not measured.
float       tc_model_catchup_speed(tc_model_t m);
// Its unused K13 input is pulled high on the real toy and the ROM reads it (tc_set_k13_pullup): Mothra only;
// P1, P2 and Angel keep tamalib's default.
bool        tc_model_k13_pullup(tc_model_t m);

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

// LCD icons. tamalib knows the P1's wiring (icons 0-3 on segment 8, COM 0-3; icons 4-7 on segment 28, COM 12-15),
// which P2 and Angel share. A model wired otherwise has a pin per icon (bit i = pins[i]), read from display memory.
typedef struct { uint8_t seg, com; } tc_icon_pin_t;
const tc_icon_pin_t *tc_model_icon_pins(tc_model_t m);  // 8 pins, or NULL: tamalib's icons are right
// nib[n] is the display nibble at 0xE00 + n (n 0x00-0xFF; 0x80 and up is display memory 2). Nibble n holds COM
// com0..com0+3 of segment (n & 0x7F) >> 1, com0 = 8 * (n >> 7) + 4 * (n & 1), as tamalib's set_lcd writes them.
uint8_t tc_icons_from_lcd(const uint8_t nib[256], const tc_icon_pin_t pins[8]);

// ---- Active ROM selection ----
typedef struct { char name[64]; uint8_t sha1[20]; tc_model_t model; } tc_rom_entry_t;
typedef enum { TC_SEL_USE_CACHED, TC_SEL_USE_SD, TC_SEL_PICKER, TC_SEL_NO_ROM } tc_sel_action_t;
// Indices of the distinct playable entries, first occurrence of each SHA-1. Returns the count.
int tc_rom_distinct_valid(const tc_rom_entry_t *sd, int n_sd, int *idx_out, int max);
// The cached (remembered) ROM always wins. Otherwise one distinct valid SD ROM is used, several
// open the picker, none means NO_ROM. Valid = tc_model_playable; duplicate SHA-1s count once.
// *sd_index_out is set for TC_SEL_USE_SD.
tc_sel_action_t tc_rom_select(bool have_cached, const tc_rom_entry_t *sd, int n_sd, int *sd_index_out);
