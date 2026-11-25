// SPDX-License-Identifier: GPL-3.0-or-later
// Pet stats from RAM. The addresses and encodings are in docs/pet-ram.md, where each was checked against the
// ROM's own screens. The P1 starting points (hunger, happy, discipline, weight, light, droppings, character)
// come from the tamagotchi-carebot README (https://github.com/Sagestum/tamagotchi-carebot); facts only, no code.
#include "tc_pet.h"

typedef void (*decode_fn)(const uint8_t *ram, tc_pet_t *out);
static void decode_mothra(const uint8_t *ram, tc_pet_t *out);

// One row per model. P1's layout: nibble addresses, -1 for a field not found (it reads -1, "not available").
// A model laid out otherwise has its own decoder (decode set; its addresses unused).
typedef struct {
    tc_model_t model;
    const char *weight_unit, *age_unit, *bar_label;
    int16_t hunger, happy, disc, weight_lo, weight_hi, sick, light, drop, age_lo, age_hi, chr, state, clock;
    decode_fn decode;
    const char *setup_exit;
    bool clock_12h;  // hour nibble 1-12 and a PM bit (tc_pet_clock_12h)
} model_row_t;

#define P1_LAYOUT 0x40, 0x41, 0x43, 0x46, 0x47, 0x48, 0x4B, 0x4D, 0x54, 0x55, 0x5D, 0x5E
#define NO_LAYOUT -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
static const model_row_t MODELS[] = {
    { TC_MODEL_P1, "lb", "yr", "disc", P1_LAYOUT, 0x10, NULL, "B A C", false },
    { TC_MODEL_P2, "g", "yr", "disc", P1_LAYOUT, 0x10, NULL, "B A C", false },
    // Angel keeps P1's addresses (droppings only come once happy is high). Its meter ends the weight with a heart,
    // which the font lacks (""), and the age with 日 (days).
    { TC_MODEL_ANGEL, "", "d", "disc", P1_LAYOUT, 0x10, NULL, "B A C", false },
    // Mothra: its own layout (decode_mothra); the clock 10 nibbles later than P1's, 12-hour.
    { TC_MODEL_MOTHRA, "t", "d", "justice", NO_LAYOUT, 0x1A, decode_mothra, "B A C", true },
};

_Static_assert(0x5F + 1 == TC_PET_RAM_NIBBLES, "TC_PET_RAM_NIBBLES must cover the decoders' addresses");

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

const char *tc_age_unit(tc_model_t m)
{
    const model_row_t *r = row_of(m);
    return r ? r->age_unit : NULL;
}

const char *tc_bar_label(tc_model_t m)
{
    const model_row_t *r = row_of(m);
    return r ? r->bar_label : NULL;
}

int tc_pet_clock_addr(tc_model_t m)
{
    const model_row_t *r = row_of(m);
    return r ? r->clock : -1;
}

bool tc_pet_has_clock(tc_model_t m) { return tc_pet_clock_addr(m) >= 0; }

bool tc_pet_clock_12h(tc_model_t m)
{
    const model_row_t *r = row_of(m);
    return r && r->clock_12h;
}

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
    if (tc_pet_clock_12h(model)) {  // 1-12, PM in bit 0 of the next nibble (its other bits are the ROM's own)
        unsigned h12 = c[4] & 15;
        hour = h12 >= 1 && h12 <= 12 ? h12 % 12 + 12u * (c[5] & 1) : 99;
    }
    if (c[0] > 9 || c[1] > 5 || c[2] > 9 || c[3] > 5 || hour > 23) return false;
    *sec_of_day = hour * 3600u + (c[3] * 10u + c[2]) * 60u + c[1] * 10u + c[0];
    return true;
}

static int8_t hearts(uint8_t v) { return (int8_t)((v + 1) / 4); }  // 0-2 -> 0, 3-6 -> 1, ... 15 -> 4

static uint8_t nib(const uint8_t *ram, int16_t a) { return ram[a] & 15; }
static int8_t cap4(uint8_t v) { return (int8_t)(v > 4 ? 4 : v); }

// Mothra (docs/pet-ram.md, "Mothra"): stage 0x0B (0 egg, F dead); hunger 0x3E (4 when 0x3F is set), happy
// 0x3D and the justice bar 0x3B count 0-4 directly; age 0x5E/0x5F and weight 0x56/0x57 are BCD (days, tons);
// 0x05 bit 0 sick, bit 1 dying (skull too), bit 2 asleep; 0x08 bit 3 set = light off; 0x07 piles.
static void decode_mothra(const uint8_t *ram, tc_pet_t *out)
{
    uint8_t stage = nib(ram, 0x0B);
    if (stage == 0 || stage == 0xF) return;  // egg or dead: nothing to report
    out->known = true;
    out->hunger = nib(ram, 0x3F) ? 4 : cap4(nib(ram, 0x3E));
    out->happy = cap4(nib(ram, 0x3D));
    out->discipline = cap4(nib(ram, 0x3B));
    out->age_years = (int16_t)(10 * nib(ram, 0x5F) + nib(ram, 0x5E));
    out->weight = (int16_t)(10 * nib(ram, 0x57) + nib(ram, 0x56));
    out->droppings = cap4(nib(ram, 0x07));  // up to 8; shown as at most 4 like P1's
    out->sick = (nib(ram, 0x05) & 3) != 0;
    out->asleep = (nib(ram, 0x05) & 4) != 0;
    out->light_on = (nib(ram, 0x08) & 8) == 0;
}

void tc_pet_decode(tc_model_t model, const uint8_t *ram, size_t n, tc_pet_t *out)
{
    *out = (tc_pet_t){ .known = false, .hunger = -1, .happy = -1, .discipline = -1, .age_years = -1, .weight = -1,
                       .droppings = -1, .sick = -1, .asleep = -1, .light_on = -1 };
    const model_row_t *r = row_of(model);
    if (!r || !ram || n < TC_PET_RAM_NIBBLES) return;
    if (r->decode) {
        r->decode(ram, out);
        return;
    }
    if (r->chr >= 0 && nib(ram, r->chr) == 0) return;  // egg or dead: nothing to report
    out->known = true;
    if (r->hunger >= 0) out->hunger = hearts(nib(ram, r->hunger));
    if (r->happy >= 0) out->happy = hearts(nib(ram, r->happy));
    if (r->disc >= 0) out->discipline = hearts(nib(ram, r->disc));
    if (r->age_lo >= 0) out->age_years = (int16_t)(10 * nib(ram, r->age_hi) + nib(ram, r->age_lo));
    if (r->weight_lo >= 0) out->weight = (int16_t)(10 * nib(ram, r->weight_hi) + nib(ram, r->weight_lo));
    if (r->drop >= 0) out->droppings = cap4(nib(ram, r->drop));  // 5 seen with 4 piles
    if (r->sick >= 0) out->sick = nib(ram, r->sick) >= 8;
    if (r->state >= 0) out->asleep = nib(ram, r->state) == 3;
    if (r->light >= 0) out->light_on = nib(ram, r->light) != 0;
}

tc_need_t tc_pet_need(const tc_pet_t *p)
{
    if (!p->known) return TC_NEED_NONE;
    if (p->sick > 0) return TC_NEED_SICK;
    if (p->hunger == 0) return TC_NEED_HUNGRY;
    if (p->droppings > 0) return TC_NEED_DROPPINGS;
    if (p->asleep > 0 && p->light_on > 0) return TC_NEED_ASLEEP_LIGHT_ON;
    if (p->happy == 0) return TC_NEED_UNHAPPY;
    // "All good" vouches for every input: with one not known for this model, say nothing.
    bool all_known = p->sick >= 0 && p->hunger >= 0 && p->droppings >= 0 && p->asleep >= 0 && p->light_on >= 0 &&
                     p->happy >= 0;
    return all_known ? TC_NEED_ALL_GOOD : TC_NEED_NONE;
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
