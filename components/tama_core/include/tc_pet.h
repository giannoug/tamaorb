// SPDX-License-Identifier: GPL-3.0-or-later
// Pet stats read from the emulator's RAM (tc_get_ram), and the one-word "what does it need" summary.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tc_rom.h"

typedef enum { TC_NEED_NONE, TC_NEED_ALL_GOOD, TC_NEED_SICK, TC_NEED_HUNGRY, TC_NEED_DROPPINGS,
               TC_NEED_ASLEEP_LIGHT_ON, TC_NEED_UNHAPPY } tc_need_t;

typedef struct {
    bool known;                    // false: unknown model, egg/dead (character 0), or no snapshot yet
    int8_t hunger, happy;          // hearts 0-4, -1 = not available
    int8_t discipline;             // quarters 0-4, -1 = n/a
    int16_t age_years, weight;     // weight in the ROM's own unit (tc_weight_unit); -1 = n/a
    int8_t droppings;              // piles 0-4 (clamped), -1 = n/a
    int8_t sick, asleep, light_on; // 0 or 1, -1 = n/a
} tc_pet_t;

// The decoders read RAM nibbles 0x00..0x5F only: fetching this many with tc_get_ram is enough.
#define TC_PET_RAM_NIBBLES 0x60
// ram: n nibbles, one per byte, address order (tc_get_ram). Always fills *out (every field -1 when !known).
void tc_pet_decode(tc_model_t model, const uint8_t *ram, size_t n, tc_pet_t *out);
// TC_NEED_NONE when !known. A problem is named when its input is known; "All good" only when
// sick, hunger, droppings, asleep, light_on and happy are all known (>= 0), else TC_NEED_NONE.
tc_need_t tc_pet_need(const tc_pet_t *p);
// "All good", "Hungry", "Unhappy", "Sick", "Droppings", "Asleep"; NULL for TC_NEED_NONE.
const char *tc_need_word(tc_need_t n);
// The pet's clock as seconds of the day, from RAM nibbles (tc_get_ram): 0x10 s ones, 0x11 s tens, 0x12 min ones,
// 0x13 min tens (BCD), hour = 16 * 0x15 + 0x14, counted from the model's clock address (tc_pet_clock_addr; P1's
// shown). False for a model without a known clock, too few nibbles, or a digit out of range (tens > 5, ones > 9,
// hour > 23: the ROM's own carry shows such values for a few instructions, and a fresh egg's unset clock reads 32 h).
bool tc_pet_clock(tc_model_t model, const uint8_t *ram, size_t n, uint32_t *sec_of_day);
int  tc_pet_clock_addr(tc_model_t m);  // first clock nibble: 0x10 (P1, P2, Angel), 0x1A (Mothra); -1 none
bool tc_pet_has_clock(tc_model_t m);
// The clock's hour is 12-hour: the hour nibble holds 1-12 and the next nibble's bit 0 is PM (Mothra); otherwise
// it is 16 * high + low, 0-23 (P1, P2, Angel). tama_core's clock write follows it (tc_set_clock_12h).
bool tc_pet_clock_12h(tc_model_t m);
// "lb" for P1, "g" for P2, "t" for Mothra, "" for Angel (its meter draws a heart the font lacks: show the bare
// number), NULL for unknown models.
const char *tc_weight_unit(tc_model_t m);
// The age's unit as the meter shows it: "yr" for P1 and P2, "d" (days, 日) for Angel and Mothra.
const char *tc_age_unit(tc_model_t m);
// The glance's label for the meter's bar page (tc_pet_t.discipline): "disc" (P1, P2, Angel), "justice" (Mothra).
const char *tc_bar_label(tc_model_t m);
// Presses (rm_press_parse spec) that take a fresh egg out of the ROM's clock-setting mode, played with
// TC_PET_SETUP_GAP_MS between presses; NULL for models without one. Found on the PC (docs/pet-ram.md).
#define TC_PET_SETUP_GAP_MS 1000u
const char *tc_pet_setup_exit(tc_model_t m);
