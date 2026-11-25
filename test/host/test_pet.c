// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "unity.h"
#include "test_support.h"
#include "tc_pet.h"
#include "tama_core.h"
#include "remote.h"

void setUp(void) {}
void tearDown(void) {}

// A living P1 at the "saves as they are" values: hunger 11, happy 8, weight 22 lb, age 2, light on.
static void base_ram(uint8_t *r)
{
    memset(r, 0, TC_RAM_NIBBLES);
    r[0x40] = 11; r[0x41] = 8; r[0x43] = 0;
    r[0x54] = 2; r[0x55] = 0; r[0x46] = 2; r[0x47] = 2;
    r[0x48] = 7; r[0x4B] = 15; r[0x4D] = 0; r[0x5D] = 2; r[0x5E] = 1;
}

static void test_p1_hearts_from_counter(void)
{
    static const uint8_t v[] = {0, 3, 4, 15}, hearts[] = {0, 1, 1, 4};
    uint8_t ram[TC_RAM_NIBBLES];
    for (int i = 0; i < 4; i++) {
        base_ram(ram);
        ram[0x40] = v[i]; ram[0x41] = v[i]; ram[0x43] = v[i];
        tc_pet_t p;
        tc_pet_decode(TC_MODEL_P1, ram, sizeof ram, &p);
        TEST_ASSERT_TRUE(p.known);
        TEST_ASSERT_EQUAL_INT(hearts[i], p.hunger);
        TEST_ASSERT_EQUAL_INT(hearts[i], p.happy);
        TEST_ASSERT_EQUAL_INT(hearts[i], p.discipline);
    }
}

static void test_weight_bcd(void)
{
    uint8_t ram[TC_RAM_NIBBLES];
    base_ram(ram);
    ram[0x47] = 9; ram[0x46] = 9; ram[0x55] = 1; ram[0x54] = 0;
    tc_pet_t p;
    tc_pet_decode(TC_MODEL_P2, ram, sizeof ram, &p);
    TEST_ASSERT_EQUAL_INT(99, p.weight);
    TEST_ASSERT_EQUAL_INT(10, p.age_years);
    TEST_ASSERT_EQUAL_STRING("lb", tc_weight_unit(TC_MODEL_P1));
    TEST_ASSERT_EQUAL_STRING("g", tc_weight_unit(TC_MODEL_P2));
    TEST_ASSERT_NULL(tc_weight_unit(TC_MODEL_UNKNOWN));
    TEST_ASSERT_NULL(tc_weight_unit(TC_MODEL_TEST_ROM));
}

static void test_flags_and_droppings(void)
{
    uint8_t ram[TC_RAM_NIBBLES];
    base_ram(ram);
    ram[0x48] = 8; ram[0x5E] = 3; ram[0x4B] = 0; ram[0x4D] = 5;
    tc_pet_t p;
    tc_pet_decode(TC_MODEL_P1, ram, sizeof ram, &p);
    TEST_ASSERT_EQUAL_INT(1, p.sick);
    TEST_ASSERT_EQUAL_INT(1, p.asleep);
    TEST_ASSERT_EQUAL_INT(0, p.light_on);
    TEST_ASSERT_EQUAL_INT(4, p.droppings);  // RAM was seen at 5 with 4 piles on screen
    ram[0x48] = 7; ram[0x5E] = 9;
    tc_pet_decode(TC_MODEL_P1, ram, sizeof ram, &p);
    TEST_ASSERT_EQUAL_INT(0, p.sick);
    TEST_ASSERT_EQUAL_INT(0, p.asleep);
}

static tc_need_t need_of(const uint8_t *ram)
{
    tc_pet_t p;
    tc_pet_decode(TC_MODEL_P1, ram, TC_RAM_NIBBLES, &p);
    return tc_pet_need(&p);
}

static void test_need_priority(void)
{
    uint8_t ram[TC_RAM_NIBBLES];
    base_ram(ram);
    TEST_ASSERT_EQUAL(TC_NEED_ALL_GOOD, need_of(ram));
    ram[0x41] = 0;
    TEST_ASSERT_EQUAL(TC_NEED_UNHAPPY, need_of(ram));
    ram[0x5E] = 1; ram[0x4B] = 15;  // awake with the light on: not "asleep"
    TEST_ASSERT_EQUAL(TC_NEED_UNHAPPY, need_of(ram));
    ram[0x5E] = 3;
    TEST_ASSERT_EQUAL(TC_NEED_ASLEEP_LIGHT_ON, need_of(ram));
    ram[0x4D] = 1;
    TEST_ASSERT_EQUAL(TC_NEED_DROPPINGS, need_of(ram));
    ram[0x40] = 0;
    TEST_ASSERT_EQUAL(TC_NEED_HUNGRY, need_of(ram));
    ram[0x48] = 15;
    TEST_ASSERT_EQUAL(TC_NEED_SICK, need_of(ram));
    // asleep with the light off and nothing else wrong is all good
    base_ram(ram);
    ram[0x5E] = 3; ram[0x4B] = 0;
    TEST_ASSERT_EQUAL(TC_NEED_ALL_GOOD, need_of(ram));
}

static void test_need_words(void)
{
    TEST_ASSERT_NULL(tc_need_word(TC_NEED_NONE));
    TEST_ASSERT_EQUAL_STRING("All good", tc_need_word(TC_NEED_ALL_GOOD));
    TEST_ASSERT_EQUAL_STRING("Sick", tc_need_word(TC_NEED_SICK));
    TEST_ASSERT_EQUAL_STRING("Hungry", tc_need_word(TC_NEED_HUNGRY));
    TEST_ASSERT_EQUAL_STRING("Droppings", tc_need_word(TC_NEED_DROPPINGS));
    TEST_ASSERT_EQUAL_STRING("Asleep", tc_need_word(TC_NEED_ASLEEP_LIGHT_ON));
    TEST_ASSERT_EQUAL_STRING("Unhappy", tc_need_word(TC_NEED_UNHAPPY));
}

static void assert_all_hidden(const tc_pet_t *p)
{
    TEST_ASSERT_FALSE(p->known);
    TEST_ASSERT_EQUAL_INT(-1, p->hunger);
    TEST_ASSERT_EQUAL_INT(-1, p->happy);
    TEST_ASSERT_EQUAL_INT(-1, p->discipline);
    TEST_ASSERT_EQUAL_INT(-1, p->age_years);
    TEST_ASSERT_EQUAL_INT(-1, p->weight);
    TEST_ASSERT_EQUAL_INT(-1, p->droppings);
    TEST_ASSERT_EQUAL_INT(-1, p->sick);
    TEST_ASSERT_EQUAL_INT(-1, p->asleep);
    TEST_ASSERT_EQUAL_INT(-1, p->light_on);
    TEST_ASSERT_EQUAL(TC_NEED_NONE, tc_pet_need(p));
}

static void test_egg_hides_stats(void)
{
    uint8_t ram[TC_RAM_NIBBLES];
    base_ram(ram);
    ram[0x5D] = 0;
    tc_pet_t p;
    tc_pet_decode(TC_MODEL_P2, ram, sizeof ram, &p);
    assert_all_hidden(&p);
}

static void test_unknown_model_hides_stats(void)
{
    uint8_t ram[TC_RAM_NIBBLES];
    base_ram(ram);
    tc_pet_t p;
    tc_pet_decode(TC_MODEL_UNKNOWN, ram, sizeof ram, &p);
    assert_all_hidden(&p);
    tc_pet_decode(TC_MODEL_TEST_ROM, ram, sizeof ram, &p);
    assert_all_hidden(&p);
}

static void test_unverified_fields_minus_one(void)
{
    // No snapshot yet, or one too short to hold the addresses: nothing is guessed.
    uint8_t ram[TC_RAM_NIBBLES];
    base_ram(ram);
    tc_pet_t p;
    tc_pet_decode(TC_MODEL_P1, ram, 0, &p);
    assert_all_hidden(&p);
    tc_pet_decode(TC_MODEL_P1, ram, 0x5E, &p);
    assert_all_hidden(&p);
    tc_pet_decode(TC_MODEL_P1, NULL, 0, &p);
    assert_all_hidden(&p);
}

// ---- The pet's clock (0x10-0x15) ----
static void set_clock(uint8_t *r, int s0, int s1, int m0, int m1, int h0, int h1)
{
    r[0x10] = s0; r[0x11] = s1; r[0x12] = m0; r[0x13] = m1; r[0x14] = h0; r[0x15] = h1;
}

static void test_clock_decodes_bcd(void)
{
    uint8_t ram[TC_RAM_NIBBLES];
    base_ram(ram);
    set_clock(ram, 0, 0, 8, 5, 9, 0);  // 09:58:00
    uint32_t s = 1;
    TEST_ASSERT_TRUE(tc_pet_clock(TC_MODEL_P1, ram, sizeof ram, &s));
    TEST_ASSERT_EQUAL_UINT32(9 * 3600 + 58 * 60, s);
    set_clock(ram, 4, 4, 9, 0, 2, 1);  // 18:09:44 (P2's save)
    TEST_ASSERT_TRUE(tc_pet_clock(TC_MODEL_P2, ram, sizeof ram, &s));
    TEST_ASSERT_EQUAL_UINT32(18 * 3600 + 9 * 60 + 44, s);
}

static void test_clock_hour_high_nibble(void)
{
    uint8_t ram[TC_RAM_NIBBLES];
    base_ram(ram);
    set_clock(ram, 9, 5, 9, 5, 4, 1);  // hour 16 * 1 + 4 = 20: 20:59:59
    uint32_t s = 0;
    TEST_ASSERT_TRUE(tc_pet_clock(TC_MODEL_P1, ram, sizeof ram, &s));
    TEST_ASSERT_EQUAL_UINT32(20 * 3600 + 59 * 60 + 59, s);
    set_clock(ram, 9, 5, 9, 5, 7, 1);  // 23:59:59, the last valid second
    TEST_ASSERT_TRUE(tc_pet_clock(TC_MODEL_P1, ram, sizeof ram, &s));
    TEST_ASSERT_EQUAL_UINT32(86399, s);
}

static void test_clock_rejects_bad_digits(void)
{
    uint8_t ram[TC_RAM_NIBBLES];
    uint32_t s = 0;
    static const int bad[][6] = {
        {0, 6, 0, 0, 0, 0},   // seconds tens 6 (the ROM's carry passes through it for a few instructions)
        {10, 0, 0, 0, 0, 0},  // seconds ones 10
        {0, 0, 10, 0, 0, 0},  // minutes ones 10
        {0, 0, 0, 6, 0, 0},   // minutes tens 6
        {0, 0, 0, 0, 8, 1},   // hour 24
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        base_ram(ram);
        set_clock(ram, bad[i][0], bad[i][1], bad[i][2], bad[i][3], bad[i][4], bad[i][5]);
        TEST_ASSERT_FALSE_MESSAGE(tc_pet_clock(TC_MODEL_P1, ram, sizeof ram, &s), "bad digit accepted");
    }
}

static void test_clock_unknown_model_and_short_ram(void)
{
    uint8_t ram[TC_RAM_NIBBLES];
    base_ram(ram);
    set_clock(ram, 0, 0, 8, 5, 9, 0);
    uint32_t s = 0;
    TEST_ASSERT_FALSE(tc_pet_clock(TC_MODEL_UNKNOWN, ram, sizeof ram, &s));
    TEST_ASSERT_FALSE(tc_pet_clock(TC_MODEL_P1, ram, 0x15, &s));  // 0x15 missing
    TEST_ASSERT_TRUE(tc_pet_clock(TC_MODEL_P1, ram, 0x16, &s));
    TEST_ASSERT_FALSE(tc_pet_clock(TC_MODEL_P1, NULL, 0, &s));
}

// ---- Angel and Mothra (docs/pet-ram.md, "Angel" and "Mothra") ----
static void test_angel_decode(void)
{
    uint8_t ram[TC_RAM_NIBBLES];
    base_ram(ram);  // Angel keeps P1's addresses
    ram[0x43] = 4;
    tc_pet_t p;
    tc_pet_decode(TC_MODEL_ANGEL, ram, sizeof ram, &p);
    TEST_ASSERT_TRUE(p.known);
    TEST_ASSERT_EQUAL(3, p.hunger);
    TEST_ASSERT_EQUAL(2, p.happy);
    TEST_ASSERT_EQUAL(1, p.discipline);
    TEST_ASSERT_EQUAL(2, p.age_years);
    TEST_ASSERT_EQUAL(22, p.weight);
    TEST_ASSERT_EQUAL(-1, p.droppings);  // never seen on an Angel
    TEST_ASSERT_EQUAL(0, p.sick);
    TEST_ASSERT_EQUAL(0, p.asleep);
    TEST_ASSERT_EQUAL(1, p.light_on);
    ram[0x48] = 15; ram[0x5E] = 3; ram[0x4B] = 0;  // the light switched off by day made it sick
    tc_pet_decode(TC_MODEL_ANGEL, ram, sizeof ram, &p);
    TEST_ASSERT_EQUAL(1, p.sick);
    TEST_ASSERT_EQUAL(1, p.asleep);
    TEST_ASSERT_EQUAL(0, p.light_on);
    ram[0x5D] = 0;
    tc_pet_decode(TC_MODEL_ANGEL, ram, sizeof ram, &p);
    TEST_ASSERT_FALSE(p.known);
}

static void test_mothra_hunger_only(void)
{
    uint8_t ram[TC_RAM_NIBBLES];
    memset(ram, 0, sizeof ram);
    ram[0x3E] = 3;  // one meal adds 1; the meter shows one heart per unit, 4 at most
    tc_pet_t p;
    tc_pet_decode(TC_MODEL_MOTHRA, ram, sizeof ram, &p);
    TEST_ASSERT_TRUE(p.known);
    TEST_ASSERT_EQUAL(3, p.hunger);
    TEST_ASSERT_EQUAL(-1, p.happy);
    TEST_ASSERT_EQUAL(-1, p.discipline);
    TEST_ASSERT_EQUAL(-1, p.age_years);
    TEST_ASSERT_EQUAL(-1, p.weight);
    TEST_ASSERT_EQUAL(-1, p.droppings);
    TEST_ASSERT_EQUAL(-1, p.sick);
    TEST_ASSERT_EQUAL(-1, p.asleep);
    TEST_ASSERT_EQUAL(-1, p.light_on);
    ram[0x3E] = 7;
    tc_pet_decode(TC_MODEL_MOTHRA, ram, sizeof ram, &p);
    TEST_ASSERT_EQUAL(4, p.hunger);
    TEST_ASSERT_NULL(tc_weight_unit(TC_MODEL_MOTHRA));
}

// Mothra's character address isn't known, so an egg or a dead pet can't be told apart from a hungry one:
// no need word at all rather than a wrong "Hungry".
static void test_mothra_names_no_need(void)
{
    uint8_t ram[TC_RAM_NIBBLES];
    memset(ram, 0, sizeof ram);
    tc_pet_t p;
    tc_pet_decode(TC_MODEL_MOTHRA, ram, sizeof ram, &p);
    TEST_ASSERT_EQUAL(0, p.hunger);
    TEST_ASSERT_EQUAL(TC_NEED_NONE, tc_pet_need(&p));
    ram[0x3E] = 2;
    tc_pet_decode(TC_MODEL_MOTHRA, ram, sizeof ram, &p);
    TEST_ASSERT_EQUAL(TC_NEED_NONE, tc_pet_need(&p));
}

// Mothra's clock is 12-hour: 0x1E holds 1-12, 0x1F bit 0 is PM (docs/pet-ram.md, CLOCK12); other 0x1F bits kept.
static void test_mothra_clock_12h(void)
{
    uint8_t ram[TC_RAM_NIBBLES];
    memset(ram, 0, sizeof ram);
    uint32_t s = 0;
    ram[0x1A] = 4; ram[0x1B] = 3; ram[0x1C] = 5; ram[0x1D] = 0;  // :05:34
    ram[0x1E] = 12; ram[0x1F] = 1;  // noon
    TEST_ASSERT_TRUE(tc_pet_clock(TC_MODEL_MOTHRA, ram, sizeof ram, &s));
    TEST_ASSERT_EQUAL_UINT32(12 * 3600 + 5 * 60 + 34, s);
    ram[0x1E] = 1; ram[0x1F] = 1 | 2;  // 1 PM, with bit 1 set too
    TEST_ASSERT_TRUE(tc_pet_clock(TC_MODEL_MOTHRA, ram, sizeof ram, &s));
    TEST_ASSERT_EQUAL_UINT32(13 * 3600 + 5 * 60 + 34, s);
    ram[0x1E] = 12; ram[0x1F] = 0;  // midnight
    TEST_ASSERT_TRUE(tc_pet_clock(TC_MODEL_MOTHRA, ram, sizeof ram, &s));
    TEST_ASSERT_EQUAL_UINT32(5 * 60 + 34, s);
    ram[0x1E] = 11; ram[0x1F] = 0;  // 11 AM
    TEST_ASSERT_TRUE(tc_pet_clock(TC_MODEL_MOTHRA, ram, sizeof ram, &s));
    TEST_ASSERT_EQUAL_UINT32(11 * 3600 + 5 * 60 + 34, s);
    ram[0x1E] = 0;  // no such hour
    TEST_ASSERT_FALSE(tc_pet_clock(TC_MODEL_MOTHRA, ram, sizeof ram, &s));
    TEST_ASSERT_TRUE(tc_pet_clock_12h(TC_MODEL_MOTHRA));
    TEST_ASSERT_FALSE(tc_pet_clock_12h(TC_MODEL_P1));
}

// "All good" is a claim about every input: it shows only when the model knows them all.
static void test_need_all_good_needs_every_input(void)
{
    tc_pet_t p = { .known = true, .hunger = 4, .happy = 4, .discipline = -1, .age_years = -1, .weight = -1,
                   .droppings = 0, .sick = 0, .asleep = 0, .light_on = 1 };
    TEST_ASSERT_EQUAL(TC_NEED_ALL_GOOD, tc_pet_need(&p));
    p.droppings = -1;
    TEST_ASSERT_EQUAL(TC_NEED_NONE, tc_pet_need(&p));
}

static void test_need_known_problem_still_named(void)
{
    tc_pet_t p = { .known = true, .hunger = 0, .happy = 4, .discipline = -1, .age_years = -1, .weight = -1,
                   .droppings = -1, .sick = 0, .asleep = 0, .light_on = 1 };
    TEST_ASSERT_EQUAL(TC_NEED_HUNGRY, tc_pet_need(&p));
    p.hunger = 4;
    p.sick = 1;
    TEST_ASSERT_EQUAL(TC_NEED_SICK, tc_pet_need(&p));
}

static void test_angel_no_all_good(void)
{
    uint8_t ram[TC_RAM_NIBBLES];
    memset(ram, 0, sizeof ram);
    ram[0x5D] = 1;               // hatched
    ram[0x40] = ram[0x41] = 15;  // full hearts
    ram[0x48] = 0;               // not sick
    ram[0x5E] = 1;               // awake
    ram[0x4B] = 15;              // light on
    tc_pet_t p;
    tc_pet_decode(TC_MODEL_ANGEL, ram, sizeof ram, &p);
    TEST_ASSERT_TRUE(p.known);
    TEST_ASSERT_EQUAL(TC_NEED_NONE, tc_pet_need(&p));  // droppings unknown on Angel
}

static void test_clock_addresses(void)
{
    TEST_ASSERT_EQUAL(0x10, tc_pet_clock_addr(TC_MODEL_P1));
    TEST_ASSERT_EQUAL(0x10, tc_pet_clock_addr(TC_MODEL_P2));
    TEST_ASSERT_EQUAL(0x10, tc_pet_clock_addr(TC_MODEL_ANGEL));
    TEST_ASSERT_EQUAL(0x1A, tc_pet_clock_addr(TC_MODEL_MOTHRA));
    TEST_ASSERT_EQUAL(-1, tc_pet_clock_addr(TC_MODEL_TEST_ROM));
    TEST_ASSERT_TRUE(tc_pet_has_clock(TC_MODEL_ANGEL));
    TEST_ASSERT_TRUE(tc_pet_has_clock(TC_MODEL_MOTHRA));
    TEST_ASSERT_FALSE(tc_pet_has_clock(TC_MODEL_UNKNOWN));
}

static void test_clock_angel_mothra(void)
{
    uint8_t ram[0x20] = {0};
    uint32_t s = 0;
    ram[0x10] = 7; ram[0x11] = 3; ram[0x12] = 5; ram[0x13] = 4; ram[0x14] = 3; ram[0x15] = 1;  // 19:45:37
    TEST_ASSERT_TRUE(tc_pet_clock(TC_MODEL_ANGEL, ram, sizeof ram, &s));
    TEST_ASSERT_EQUAL_UINT32(19 * 3600 + 45 * 60 + 37, s);
    memset(ram, 0, sizeof ram);
    ram[0x1A] = 2; ram[0x1B] = 1; ram[0x1C] = 0; ram[0x1D] = 3; ram[0x1E] = 9; ram[0x1F] = 0;  // 09:30:12
    TEST_ASSERT_TRUE(tc_pet_clock(TC_MODEL_MOTHRA, ram, sizeof ram, &s));
    TEST_ASSERT_EQUAL_UINT32(9 * 3600 + 30 * 60 + 12, s);
    TEST_ASSERT_FALSE(tc_pet_clock(TC_MODEL_MOTHRA, ram, 0x1F, &s));  // 0x1F missing
}

static void test_setup_exit_specs_parse(void)
{
    const tc_model_t models[] = { TC_MODEL_P1, TC_MODEL_P2, TC_MODEL_ANGEL, TC_MODEL_MOTHRA };
    for (size_t i = 0; i < sizeof models / sizeof models[0]; i++) {
        const char *spec = tc_pet_setup_exit(models[i]);
        TEST_ASSERT_NOT_NULL(spec);
        rm_press_req_t r;
        rm_press_defaults(&r);
        r.gap_ms = TC_PET_SETUP_GAP_MS;
        TEST_ASSERT_TRUE(rm_press_parse(&r, spec));
        TEST_ASSERT_NULL(rm_press_validate(&r));
    }
    TEST_ASSERT_NULL(tc_pet_setup_exit(TC_MODEL_TEST_ROM));
    TEST_ASSERT_NULL(tc_pet_setup_exit(TC_MODEL_UNKNOWN));
}

static void test_decoder_needs_only_pet_ram_nibbles(void)
{
    uint8_t ram[TC_RAM_NIBBLES];
    base_ram(ram);
    tc_pet_t p;
    tc_pet_decode(TC_MODEL_P1, ram, TC_PET_RAM_NIBBLES, &p);  // what the glance asks tc_get_ram for
    TEST_ASSERT_TRUE(p.known);
    TEST_ASSERT_EQUAL_INT(0, p.asleep);
    tc_pet_decode(TC_MODEL_P1, ram, TC_PET_RAM_NIBBLES - 1, &p);  // one short: the last address is missing
    TEST_ASSERT_FALSE(p.known);
    TEST_ASSERT_TRUE(TC_PET_RAM_NIBBLES <= TC_RAM_NIBBLES);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_decoder_needs_only_pet_ram_nibbles);
    RUN_TEST(test_p1_hearts_from_counter);
    RUN_TEST(test_weight_bcd);
    RUN_TEST(test_flags_and_droppings);
    RUN_TEST(test_need_priority);
    RUN_TEST(test_need_words);
    RUN_TEST(test_egg_hides_stats);
    RUN_TEST(test_unknown_model_hides_stats);
    RUN_TEST(test_unverified_fields_minus_one);
    RUN_TEST(test_clock_decodes_bcd);
    RUN_TEST(test_clock_hour_high_nibble);
    RUN_TEST(test_clock_rejects_bad_digits);
    RUN_TEST(test_clock_unknown_model_and_short_ram);
    RUN_TEST(test_angel_decode);
    RUN_TEST(test_mothra_hunger_only);
    RUN_TEST(test_mothra_names_no_need);
    RUN_TEST(test_mothra_clock_12h);
    RUN_TEST(test_need_all_good_needs_every_input);
    RUN_TEST(test_need_known_problem_still_named);
    RUN_TEST(test_angel_no_all_good);
    RUN_TEST(test_clock_addresses);
    RUN_TEST(test_clock_angel_mothra);
    RUN_TEST(test_setup_exit_specs_parse);
    return UNITY_END();
}
