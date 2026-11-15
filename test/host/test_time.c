// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "unity.h"
#include "test_support.h"
#include "tc_rom.h"
#include "tc_time.h"
#include "tama_core.h"

static uint32_t fake_now;
static uint32_t now_us(void) { return fake_now; }
static void sleep_until_us(uint32_t ts) { if ((int32_t)(ts - fake_now) > 0) fake_now = ts; }
static const tc_platform_t plat = { .now_us = now_us, .sleep_until_us = sleep_until_us };
static bool have_rom, inited;
static uint16_t program[TC_ROM_WORDS];

void setUp(void) { if (inited) { tc_debug_set_tick_source(NULL); tc_reset_to_boot(); tc_set_realtime(true); } }
void tearDown(void) { tc_debug_set_tick_source(NULL); }

static void test_ticks_since_wraps(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x200, tc_ticks_since(0xFFFFFF00u, 0x00000100u));
    TEST_ASSERT_EQUAL_HEX32(5, tc_ticks_since(10, 15));
}

static void test_gap_both_trusted(void)
{
    uint64_t g = 99;
    TEST_ASSERT_TRUE(tc_gap_seconds(1000, true, 4600, true, &g));
    TEST_ASSERT_EQUAL_UINT64(3600, g);
}

static void test_gap_untrusted_save(void)
{
    uint64_t g;
    TEST_ASSERT_FALSE(tc_gap_seconds(1000, false, 4600, true, &g));
}

static void test_gap_untrusted_now(void)
{
    uint64_t g;
    TEST_ASSERT_FALSE(tc_gap_seconds(1000, true, 4600, false, &g));
}

static void test_gap_clock_backwards(void)
{
    uint64_t g = 99;
    TEST_ASSERT_TRUE(tc_gap_seconds(5000, true, 4000, true, &g));
    TEST_ASSERT_EQUAL_UINT64(0, g);
}

// Fake tick source: each read after a step jumps 2^31 ticks, so the 32-bit counter wraps
// every two steps.
static uint32_t fake_ticks;
static uint32_t fake_tick_source(void) { return fake_ticks; }
static uint64_t last_done;
static int cb_calls;
static bool record_cb(uint64_t done, uint64_t total, void *ctx)
{
    TEST_ASSERT_TRUE(done > last_done);
    last_done = done;
    cb_calls++;
    return true;
}
static void advance_fake(void) { fake_ticks += 0x80000000u; }

static void test_catchup_exceeds_wrap(void)
{
    fake_ticks = 0;
    last_done = 0;
    cb_calls = 0;
    tc_debug_set_tick_source(fake_tick_source);
    tc_debug_set_step_hook(advance_fake);
    uint64_t want = 3ull * 0x80000000u;
    uint64_t ran = tc_catchup(want, record_cb, NULL, 0x80000000u);
    tc_debug_set_step_hook(NULL);
    TEST_ASSERT_TRUE(ran >= want);
    TEST_ASSERT_GREATER_OR_EQUAL(3, cb_calls);
}

static void test_catchup_without_callback(void)
{
    fake_ticks = 0;
    tc_debug_set_tick_source(fake_tick_source);
    tc_debug_set_step_hook(advance_fake);
    uint64_t ran = tc_catchup(10ull * 0x80000000u, NULL, NULL, 0);  // no callback: runs to the end
    TEST_ASSERT_TRUE(ran >= 10ull * 0x80000000u);
    tc_debug_set_step_hook(NULL);
}

static void test_catchup_matches_realtime(void)
{
    if (!have_rom) TEST_IGNORE_MESSAGE("ROM not present");
    static uint8_t a[4096], b[4096];
    // Catch-up of 20 s from boot ...
    uint64_t ran = tc_catchup(20ull * TC_TICK_HZ, NULL, NULL, 0);
    TEST_ASSERT_TRUE(ran >= 20ull * TC_TICK_HZ);
    uint32_t end_ticks = tc_tick_counter();
    size_t la = tc_state_save(a, sizeof a);
    // ... equals real-time stepping to the same tick count from the same start.
    tc_reset_to_boot();
    tc_set_realtime(true);
    while (tc_tick_counter() != end_ticks && (int32_t)(end_ticks - tc_tick_counter()) > 0) tc_step(1);
    size_t lb = tc_state_save(b, sizeof b);
    TEST_ASSERT_EQUAL(la, lb);
    TEST_ASSERT_EQUAL_MEMORY(a, b, la);
}

int main(void)
{
    static uint8_t rom[TC_ROM_BYTES];
    have_rom = test_load_file("tama.bin", rom, sizeof rom);
    if (have_rom) tc_rom_decode(rom, sizeof rom, program);
    inited = tc_init(program, &plat);

    UNITY_BEGIN();
    RUN_TEST(test_ticks_since_wraps);
    RUN_TEST(test_gap_both_trusted);
    RUN_TEST(test_gap_untrusted_save);
    RUN_TEST(test_gap_untrusted_now);
    RUN_TEST(test_gap_clock_backwards);
    RUN_TEST(test_catchup_exceeds_wrap);
    RUN_TEST(test_catchup_without_callback);
    RUN_TEST(test_catchup_matches_realtime);
    return UNITY_END();
}
