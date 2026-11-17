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

// The decoder reads RAM nibbles 0x00..0x5E only: fetching this many with tc_get_ram is enough.
#define TC_PET_RAM_NIBBLES 0x5F
// ram: n nibbles, one per byte, address order (tc_get_ram). Always fills *out (every field -1 when !known).
void tc_pet_decode(tc_model_t model, const uint8_t *ram, size_t n, tc_pet_t *out);
tc_need_t tc_pet_need(const tc_pet_t *p);  // TC_NEED_NONE when !known
// "All good", "Hungry", "Unhappy", "Sick", "Droppings", "Asleep"; NULL for TC_NEED_NONE.
const char *tc_need_word(tc_need_t n);
// The pet's clock as seconds of the day, from RAM nibbles (tc_get_ram): 0x10 s ones, 0x11 s tens, 0x12 min ones,
// 0x13 min tens (BCD), hour = 16 * 0x15 + 0x14. False for models other than P1/P2, fewer than 0x16 nibbles, or a
// digit out of range (tens > 5, ones > 9, hour > 23: the ROM's own carry shows such values for a few instructions).
bool tc_pet_clock(tc_model_t model, const uint8_t *ram, size_t n, uint32_t *sec_of_day);
// "lb" for P1, "g" for P2, NULL otherwise.
const char *tc_weight_unit(tc_model_t m);
