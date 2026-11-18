// SPDX-License-Identifier: GPL-3.0-or-later
// Pet stats from RAM. The addresses and encodings are in docs/pet-ram.md, where each was checked against the
// ROM's own screens. The P1 starting points (hunger, happy, discipline, weight, light, droppings, character)
// come from the tamagotchi-carebot README (https://github.com/Sagestum/tamagotchi-carebot); facts only, no code.
#include "tc_pet.h"

// One row per model. An address of -1: not found for that model (the field reads -1, "not available").
// hunger_steps: the hunger counter's top value per heart shown; 0 = P1's (v+1)/4 hearts from a 0-15 counter.
typedef struct {
    tc_model_t model;
    const char *weight_unit;
    int16_t hunger, happy, disc, weight_lo, weight_hi, sick, light, drop, age_lo, age_hi, chr, state, clock;
    uint8_t hunger_per_heart;  // 0: hearts = (v + 1) / 4 (P1); 1: hearts = v, at most 4 (Mothra)
    const char *setup_exit;
} model_row_t;

#define P1_LAYOUT 0x40, 0x41, 0x43, 0x46, 0x47, 0x48, 0x4B, 0x4D, 0x54, 0x55, 0x5D, 0x5E, 0x10
static const model_row_t MODELS[] = {
    { TC_MODEL_P1, "lb", P1_LAYOUT, 0, "B A C" },
    { TC_MODEL_P2, "g", P1_LAYOUT, 0, "B A C" },
    // Angel keeps P1's addresses; droppings never appeared in the research runs, and its weight unit is a glyph
    // that wasn't identified.
    { TC_MODEL_ANGEL, NULL, 0x40, 0x41, 0x43, 0x46, 0x47, 0x48, 0x4B, -1, 0x54, 0x55, 0x5D, 0x5E, 0x10, 0, "B A C" },
    // Mothra: only hunger and the clock were found (the clock 10 nibbles later than P1's).
    { TC_MODEL_MOTHRA, NULL, 0x3E, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0x1A, 1, "B A C" },
};

_Static_assert(0x5E + 1 == TC_PET_RAM_NIBBLES, "TC_PET_RAM_NIBBLES must cover the decoder's addresses");

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

int tc_pet_clock_addr(tc_model_t m)
{
    const model_row_t *r = row_of(m);
    return r ? r->clock : -1;
}

bool tc_pet_has_clock(tc_model_t m) { return tc_pet_clock_addr(m) >= 0; }

const char *tc_pet_setup_exit(tc_model_t m)
{
    const model_row_t *r = row_of(m);
    return r ? r->setup_exit : NULL;
}

bool tc_pet_clock(tc_model_t model, const uint8_t *ram, size_t n, uint32_t *sec_of_day)
{
    // s ones, s tens, min ones, min tens, hour low, hour high from the model's clock address
    int a = tc_pet_clock_addr(model);
    if (a < 0 || !ram || n < (size_t)a + 6) return false;
    const uint8_t *c = ram + a;
    unsigned hour = 16u * (c[5] & 15) + (c[4] & 15);
    if (c[0] > 9 || c[1] > 5 || c[2] > 9 || c[3] > 5 || hour > 23) return false;
    *sec_of_day = hour * 3600u + (c[3] * 10u + c[2]) * 60u + c[1] * 10u + c[0];
    return true;
}

static int8_t hearts(uint8_t v) { return (int8_t)((v + 1) / 4); }  // 0-2 -> 0, 3-6 -> 1, ... 15 -> 4

static uint8_t nib(const uint8_t *ram, int16_t a) { return ram[a] & 15; }

void tc_pet_decode(tc_model_t model, const uint8_t *ram, size_t n, tc_pet_t *out)
{
    *out = (tc_pet_t){ .known = false, .hunger = -1, .happy = -1, .discipline = -1, .age_years = -1, .weight = -1,
                       .droppings = -1, .sick = -1, .asleep = -1, .light_on = -1 };
    const model_row_t *r = row_of(model);
    if (!r || !ram || n < TC_PET_RAM_NIBBLES) return;
    if (r->chr >= 0 && nib(ram, r->chr) == 0) return;  // egg or dead: nothing to report
    out->known = true;
    out->need_hidden = r->chr < 0;
    if (r->hunger >= 0) {
        uint8_t v = nib(ram, r->hunger);
        out->hunger = r->hunger_per_heart ? (int8_t)(v > 4 ? 4 : v) : hearts(v);
    }
    if (r->happy >= 0) out->happy = hearts(nib(ram, r->happy));
    if (r->disc >= 0) out->discipline = hearts(nib(ram, r->disc));
    if (r->age_lo >= 0) out->age_years = (int16_t)(10 * nib(ram, r->age_hi) + nib(ram, r->age_lo));
    if (r->weight_lo >= 0) out->weight = (int16_t)(10 * nib(ram, r->weight_hi) + nib(ram, r->weight_lo));
    if (r->drop >= 0) out->droppings = nib(ram, r->drop) > 4 ? 4 : (int8_t)nib(ram, r->drop);  // 5 seen with 4 piles
    if (r->sick >= 0) out->sick = nib(ram, r->sick) >= 8;
    if (r->state >= 0) out->asleep = nib(ram, r->state) == 3;
    if (r->light >= 0) out->light_on = nib(ram, r->light) != 0;
}

tc_need_t tc_pet_need(const tc_pet_t *p)
{
    if (!p->known || p->need_hidden) return TC_NEED_NONE;
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
