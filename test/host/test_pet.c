// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include "unity.h"
#include "test_support.h"
#include "tc_pet.h"
#include "tama_core.h"
#include "persist.h"

static uint64_t now_us(void) { return 0; }
static void sleep_until_us(uint64_t ts) { (void)ts; }

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

// Replays a save in a child process (tc_init is once per process) and returns its RAM; false if the files are absent.
static bool ram_from_save(const char *rom_name, const char *save_path, uint8_t *ram)
{
    static uint8_t rom[TC_ROM_BYTES], rec[4096];
    static uint16_t program[TC_ROM_WORDS];
    if (!test_load_file(rom_name, rom, sizeof rom)) return false;
    FILE *f = fopen(save_path, "rb");
    if (!f) return false;
    size_t rec_len = fread(rec, 1, sizeof rec, f);
    fclose(f);
    int fd[2];
    TEST_ASSERT_EQUAL(0, pipe(fd));
    pid_t pid = fork();
    TEST_ASSERT_TRUE(pid >= 0);
    if (pid == 0) {
        close(fd[0]);
        static const tc_platform_t plat = { .now_us = now_us, .sleep_until_us = sleep_until_us };
        ps_meta_t meta;
        const uint8_t *payload;
        size_t plen;
        if (!tc_rom_decode(rom, sizeof rom, program) || !tc_init(program, &plat) ||
            !ps_record_decode(rec, rec_len, &meta, &payload, &plen) || !tc_state_load(payload, plen)) _exit(2);
        tc_set_realtime(false);
        for (int i = 0; i < 100000 && tc_get_ram(ram, TC_RAM_NIBBLES) == 0; i++) tc_step(64);
        size_t n = tc_get_ram(ram, TC_RAM_NIBBLES);
        if (n != TC_RAM_NIBBLES || write(fd[1], ram, n) != (ssize_t)n) _exit(3);
        _exit(0);
    }
    close(fd[1]);
    size_t got = 0;
    ssize_t r;
    while ((r = read(fd[0], ram + got, TC_RAM_NIBBLES - got)) > 0) got += (size_t)r;
    close(fd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    TEST_ASSERT_EQUAL(0, status);
    TEST_ASSERT_EQUAL(TC_RAM_NIBBLES, got);
    return true;
}

static const char *roms_saves(const char *name)
{
    static char path[512];
    const char *root = getenv("TAMAORB_ROOT");
    snprintf(path, sizeof path, "%s/roms/saves/%s", root ? root : TAMAORB_ROOT_DEFAULT, name);
    return path;
}

static void check_p1(const char *save, int hunger, int weight)
{
    uint8_t ram[TC_RAM_NIBBLES];
    if (!ram_from_save("tama.bin", roms_saves(save), ram)) TEST_IGNORE_MESSAGE("ROM or save not present");
    tc_pet_t p;
    tc_pet_decode(TC_MODEL_P1, ram, sizeof ram, &p);
    TEST_ASSERT_TRUE(p.known);
    TEST_ASSERT_EQUAL_INT(hunger, p.hunger);
    TEST_ASSERT_EQUAL_INT(2, p.happy);
    TEST_ASSERT_EQUAL_INT(0, p.discipline);
    TEST_ASSERT_EQUAL_INT(2, p.age_years);
    TEST_ASSERT_EQUAL_INT(weight, p.weight);
    TEST_ASSERT_EQUAL_INT(1, p.light_on);
    TEST_ASSERT_EQUAL_INT(0, p.droppings);
    TEST_ASSERT_EQUAL_INT(0, p.sick);
    TEST_ASSERT_EQUAL_INT(0, p.asleep);
    TEST_ASSERT_EQUAL(TC_NEED_ALL_GOOD, tc_pet_need(&p));
}

static void test_p1_save_matches_meter(void) { check_p1("p1.sav", 3, 22); }
static void test_p1_now_save_matches_meter(void) { check_p1("now.sav", 4, 23); }

static void test_p2_save_matches_meter(void)
{
    // p2.sav is an egg (character 0): nothing to report until it hatches.
    uint8_t ram[TC_RAM_NIBBLES];
    if (!ram_from_save("tamag2.bin", roms_saves("p2.sav"), ram)) TEST_IGNORE_MESSAGE("ROM or save not present");
    tc_pet_t p;
    tc_pet_decode(TC_MODEL_P2, ram, sizeof ram, &p);
    TEST_ASSERT_FALSE(p.known);
    TEST_ASSERT_EQUAL(TC_NEED_NONE, tc_pet_need(&p));
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
    RUN_TEST(test_p1_save_matches_meter);
    RUN_TEST(test_p1_now_save_matches_meter);
    RUN_TEST(test_p2_save_matches_meter);
    return UNITY_END();
}
