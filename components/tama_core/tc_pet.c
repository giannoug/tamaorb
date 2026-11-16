// SPDX-License-Identifier: GPL-3.0-or-later
// Pet stats from RAM. The addresses and encodings are in docs/pet-ram.md, where each was checked against the
// ROM's own screens. The P1 starting points (hunger, happy, discipline, weight, light, droppings, character)
// come from the tamagotchi-carebot README (https://github.com/Sagestum/tamagotchi-carebot); facts only, no code.
#include "tc_pet.h"

enum { A_HUNGER = 0x40, A_HAPPY = 0x41, A_DISC = 0x43, A_WEIGHT_LO = 0x46, A_WEIGHT_HI = 0x47, A_SICK = 0x48,
       A_LIGHT = 0x4B, A_DROP = 0x4D, A_AGE_LO = 0x54, A_AGE_HI = 0x55, A_CHAR = 0x5D, A_STATE = 0x5E,
       A_LAST = A_STATE };
_Static_assert(A_LAST + 1 == TC_PET_RAM_NIBBLES, "TC_PET_RAM_NIBBLES must cover the decoder's addresses");

// P1 and P2 keep everything at the same addresses; a new model with different ones would get its own row.
typedef struct { tc_model_t model; const char *weight_unit; } model_row_t;
static const model_row_t MODELS[] = {
    { TC_MODEL_P1, "lb" },
    { TC_MODEL_P2, "g" },
};

static const model_row_t *row_of(tc_model_t m)
{
    for (size_t i = 0; i < sizeof MODELS / sizeof MODELS[0]; i++)
        if (MODELS[i].model == m) return &MODELS[i];
    return NULL;
}

const char *tc_weight_unit(tc_model_t m)
{
    const model_row_t *r = row_of(m);
    return r ? r->weight_unit : NULL;
}

static int8_t hearts(uint8_t v) { return (int8_t)((v + 1) / 4); }  // 0-2 -> 0, 3-6 -> 1, ... 15 -> 4

void tc_pet_decode(tc_model_t model, const uint8_t *ram, size_t n, tc_pet_t *out)
{
    *out = (tc_pet_t){ .known = false, .hunger = -1, .happy = -1, .discipline = -1, .age_years = -1, .weight = -1,
                       .droppings = -1, .sick = -1, .asleep = -1, .light_on = -1 };
    if (!row_of(model) || !ram || n <= A_LAST || ram[A_CHAR] == 0) return;  // egg or dead: nothing to report
    out->known = true;
    out->hunger = hearts(ram[A_HUNGER] & 15);
    out->happy = hearts(ram[A_HAPPY] & 15);
    out->discipline = hearts(ram[A_DISC] & 15);
    out->age_years = (int16_t)(10 * (ram[A_AGE_HI] & 15) + (ram[A_AGE_LO] & 15));
    out->weight = (int16_t)(10 * (ram[A_WEIGHT_HI] & 15) + (ram[A_WEIGHT_LO] & 15));
    out->droppings = ram[A_DROP] > 4 ? 4 : (int8_t)ram[A_DROP];  // RAM reached 5 with 4 piles on screen
    out->sick = (ram[A_SICK] & 15) >= 8;
    out->asleep = (ram[A_STATE] & 15) == 3;
    out->light_on = (ram[A_LIGHT] & 15) != 0;
}

tc_need_t tc_pet_need(const tc_pet_t *p)
{
    if (!p->known) return TC_NEED_NONE;
    if (p->sick > 0) return TC_NEED_SICK;
    if (p->hunger == 0) return TC_NEED_HUNGRY;
    if (p->droppings > 0) return TC_NEED_DROPPINGS;
    if (p->asleep > 0 && p->light_on > 0) return TC_NEED_ASLEEP_LIGHT_ON;
    if (p->happy == 0) return TC_NEED_UNHAPPY;
    return TC_NEED_ALL_GOOD;
}

const char *tc_need_word(tc_need_t n)
{
    switch (n) {
    case TC_NEED_ALL_GOOD: return "All good";
    case TC_NEED_SICK: return "Sick";
    case TC_NEED_HUNGRY: return "Hungry";
    case TC_NEED_DROPPINGS: return "Droppings";
    case TC_NEED_ASLEEP_LIGHT_ON: return "Asleep";
    case TC_NEED_UNHAPPY: return "Unhappy";
    default: return NULL;
    }
}
