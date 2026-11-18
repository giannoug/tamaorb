// SPDX-License-Identifier: GPL-3.0-or-later
// A fresh egg of each real ROM (TAMAORB_SETUP_ROM: tama.bin, tamag2.bin, tamaang.bin, tamamot.bin) boots in its own
// clock-setting mode with the clock stopped. tc_pet_setup_exit's presses must get it ticking at the model's clock
// address (docs/pet-ram.md). Skipped when the dump isn't in the ROM folder (CI has none).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "test_support.h"
#include "remote.h"
#include "tama_core.h"
#include "tc_pet.h"
#include "tc_rom.h"
#include "tc_time.h"

static uint64_t now_us(void) { return 0; }
static void sleep_until_us(uint64_t ts) { (void)ts; }

static bool have;
static tc_model_t model;

void setUp(void) {}
void tearDown(void) {}

static void run_ms(uint32_t ms)
{
    uint32_t start = tc_tick_counter(), want = (uint32_t)((uint64_t)ms * TC_TICK_HZ / 1000);
    while (tc_ticks_since(start, tc_tick_counter()) < want) tc_step(1);
}

static bool clock_now(uint32_t *s)
{
    uint8_t ram[TC_PET_RAM_NIBBLES];
    return tc_pet_clock(model, ram, tc_get_ram(ram, sizeof ram), s);
}

static void play(const char *spec)
{
    rm_press_req_t r;
    rm_press_defaults(&r);
    r.gap_ms = TC_PET_SETUP_GAP_MS;
    TEST_ASSERT_TRUE(rm_press_parse(&r, spec));
    for (int i = 0; i < r.n; i++) {
        tc_post_buttons((uint8_t)r.steps[i]);
        run_ms(r.hold_ms);
        tc_post_buttons(0);
        run_ms(r.gap_ms);
    }
}

static void test_setup_exit_starts_the_clock(void)
{
    if (!have) TEST_IGNORE_MESSAGE("ROM not present");
    run_ms(3000);
    uint32_t s0 = 0, s1 = 0;
    bool valid = clock_now(&s0);  // P1, P2, Angel: an unset hour (32); Mothra: 00:00:00, standing still
    run_ms(2000);
    TEST_ASSERT_FALSE_MESSAGE(valid && clock_now(&s1) && s1 != s0, "a fresh egg's clock should stand still");
    play(tc_pet_setup_exit(model));
    run_ms(1500);
    TEST_ASSERT_TRUE(clock_now(&s0));
    run_ms(3000);
    TEST_ASSERT_TRUE(clock_now(&s1));
    TEST_ASSERT_UINT32_WITHIN(1, 3, s1 - s0);
}

// One press of A selects the first icon: exactly one of icons 0-6 is lit (Mothra needs its own pins for that).
static void test_first_icon_after_a(void)
{
    if (!have) TEST_IGNORE_MESSAGE("ROM not present");
    run_ms(1800 * 1000);  // the egg hatches; the menus answer from then on
    tc_post_buttons(TC_BTN_MASK(TC_BTN_A));
    run_ms(200);
    tc_post_buttons(0);
    run_ms(600);
    tc_frame_t f;
    tc_get_frame(&f);
    TEST_ASSERT_EQUAL_HEX8(0x01, f.icons & 0x7F);
}

int main(void)
{
    const char *name = getenv("TAMAORB_SETUP_ROM");
    static uint8_t rom[TC_ROM_MAX_BYTES];
    static uint16_t program[TC_ROM_MAX_WORDS];
    static const tc_platform_t plat = { .now_us = now_us, .sleep_until_us = sleep_until_us };
    size_t len = 0;
    if (name) {
        for (size_t sz = TC_ROM_BYTES_S46; !len && sz <= TC_ROM_BYTES_S48; sz += TC_ROM_BYTES_S48 - TC_ROM_BYTES_S46)
            if (test_load_file(name, rom, sz)) len = sz;
    }
    model = len ? tc_rom_identify(rom, len, NULL) : TC_MODEL_UNKNOWN;
    have = tc_model_playable(model) && tc_rom_decode(rom, len, program) && tc_init(program, &plat);
    if (have) {
        tc_set_realtime(false);
        tc_set_icon_pins(tc_model_icon_pins(model));
        tc_set_clock_addr((uint16_t)tc_pet_clock_addr(model));
    }
    UNITY_BEGIN();
    RUN_TEST(test_setup_exit_starts_the_clock);
    RUN_TEST(test_first_icon_after_a);
    return UNITY_END();
}
