// SPDX-License-Identifier: GPL-3.0-or-later
// A fresh egg of each real ROM (TAMAORB_SETUP_ROM: tama.bin, tamag2.bin, tamaang.bin, tamamot.bin) boots in its own
// clock-setting mode with the clock stopped. tc_pet_setup_exit's presses must get it ticking at the model's clock
// address (docs/pet-ram.md). Skipped (exit 77) when the dump isn't in the ROM folder (CI has none).
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

// The ball's clock set is a time the ROM's own clock carries on from: set a few seconds before an hour turns
// (noon, 1 PM, midnight on a 12-hour clock), let the ROM count past it, read it back.
static void test_clock_set_carries_on(void)
{
    if (!have) TEST_IGNORE_MESSAGE("ROM not present");
    static const uint32_t SET_S[] = { 11 * 3600 + 3590, 12 * 3600 + 3590, 23 * 3600 + 3590, 13 * 3600 + 5 * 60 };
    for (size_t i = 0; i < sizeof SET_S / sizeof SET_S[0]; i++) {
        uint32_t id = tc_clock_request_ms(SET_S[i] * 1000u), w = 0;
        tc_clock_state_t st;
        int guard = 0;
        while ((st = tc_clock_poll(id, &w)) == TC_CLOCK_PENDING && guard++ < 400) run_ms(10);
        TEST_ASSERT_EQUAL(TC_CLOCK_APPLIED, st);
        run_ms(20000);
        uint32_t s = 0;
        char msg[48];
        snprintf(msg, sizeof msg, "set %02u:%02u:%02u", (unsigned)(SET_S[i] / 3600), (unsigned)(SET_S[i] / 60 % 60),
                 (unsigned)(SET_S[i] % 60));
        TEST_ASSERT_TRUE_MESSAGE(clock_now(&s), msg);
        TEST_ASSERT_UINT32_WITHIN_MESSAGE(3, (SET_S[i] + 20) % 86400, s, msg);
    }
}

// The 12-hour write's edges (Mothra): midnight is hour 12 with PM clear, and bits 1-3 of the PM nibble stay the ROM's.
static uint8_t ram_at(uint16_t addr)
{
    uint8_t ram[TC_PET_RAM_NIBBLES];
    TEST_ASSERT_TRUE(tc_get_ram(ram, sizeof ram) > addr);
    return ram[addr];
}

static void set_clock(uint32_t s)
{
    uint32_t id = tc_clock_request_ms(s * 1000u), w = 0;  // w: the time written, a second or so on from s
    tc_clock_state_t st;
    int guard = 0;
    while ((st = tc_clock_poll(id, &w)) == TC_CLOCK_PENDING && guard++ < 400) run_ms(10);
    TEST_ASSERT_EQUAL(TC_CLOCK_APPLIED, st);
    run_ms(100);  // the published RAM catches up
}

static void test_clock_12h_edges(void)
{
    if (!have) TEST_IGNORE_MESSAGE("ROM not present");
    if (!tc_pet_clock_12h(model)) TEST_IGNORE_MESSAGE("24-hour clock");
    uint16_t hr = (uint16_t)tc_pet_clock_addr(model) + 4, pm = hr + 1;
    uint8_t keep = ram_at(pm) & 0xE;
    tc_debug_set_ram(pm, 0xE | (ram_at(pm) & 1));
    set_clock(5);  // 00:00:05
    TEST_ASSERT_EQUAL_HEX8(12, ram_at(hr));
    TEST_ASSERT_EQUAL_HEX8(0xE, ram_at(pm));  // PM clear, bits 1-3 untouched
    set_clock(13 * 3600 + 5);  // 1 PM
    TEST_ASSERT_EQUAL_HEX8(1, ram_at(hr));
    TEST_ASSERT_EQUAL_HEX8(0xF, ram_at(pm));
    tc_debug_set_ram(pm, keep | (ram_at(pm) & 1));
}

// C on the main screen leaves time running at real speed (Mothra's ROM has a test fast mode behind input K13,
// which reads high on the real toy: tc_model_k1_pullup).
static void test_c_on_main_screen_keeps_time(void)
{
    if (!have) TEST_IGNORE_MESSAGE("ROM not present");
    play("C C C");  // back to the main screen from whatever is open
    run_ms(3000);
    uint32_t s0 = 0, s1 = 0;
    TEST_ASSERT_TRUE(clock_now(&s0));
    play("C");
    run_ms(10000);
    TEST_ASSERT_TRUE(clock_now(&s1));
    TEST_ASSERT_UINT32_WITHIN(4, 10 + 1, (s1 - s0 + 86400) % 86400);  // the C press itself takes ~1.2 s
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
        tc_set_clock_12h(tc_pet_clock_12h(model));  // as app_main does
        tc_set_k1_pullup(tc_model_k1_pullup(model));
    }
    UNITY_BEGIN();
    RUN_TEST(test_setup_exit_starts_the_clock);
    RUN_TEST(test_first_icon_after_a);
    RUN_TEST(test_clock_set_carries_on);
    RUN_TEST(test_clock_12h_edges);
    RUN_TEST(test_c_on_main_screen_keeps_time);
    int failed = UNITY_END();
    return !have && !failed ? 77 : failed;  // 77: ctest's SKIP_RETURN_CODE, so a missing dump shows "Skipped"
}
