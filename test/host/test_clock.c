// SPDX-License-Identifier: GPL-3.0-or-later
// Setting the pet's clock (tc_clock_request / tc_clock_poll) on the real P1 ROM, from the owner's p1.sav.
// The ROM writes 0x10 at each second and carries into 0x11..0x15 within ~150 ticks; a request must land only
// at the quiet point after that (docs/superpowers/specs/2026-11-17-pet-clock-sync-design.md, finding 1).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "test_support.h"
#include "persist.h"
#include "tama_core.h"
#include "tc_pet.h"
#include "tc_rom.h"
#include "tc_time.h"
#include "cpu.h"
#include "tamalib.h"

static uint64_t now_us(void) { return 0; }
static void sleep_until_us(uint64_t ts) { (void)ts; }

static bool have;
static uint8_t rec[4096];
static const uint8_t *payload;
static size_t payload_len;

void setUp(void)
{
    if (!have) return;
    TEST_ASSERT_TRUE(tc_state_load(payload, payload_len));
    tc_set_realtime(false);
    tc_post_buttons(0);
}
void tearDown(void) { tc_debug_set_step_hook(NULL); }

static int nib(int a) { return GET_RAM_MEMORY(tamalib_get_state()->memory, a); }

static uint32_t clock_now(void)
{
    uint8_t ram[0x16];
    for (int a = 0; a < 0x16; a++) ram[a] = (uint8_t)nib(a);
    uint32_t s = UINT32_MAX;
    TEST_ASSERT_TRUE_MESSAGE(tc_pet_clock(TC_MODEL_P1, ram, sizeof ram, &s), "clock digits out of range");
    return s;
}

static void run_ms(uint32_t ms)
{
    uint32_t start = tc_tick_counter(), want = (uint32_t)((uint64_t)ms * TC_TICK_HZ / 1000);
    while (tc_ticks_since(start, tc_tick_counter()) < want) tc_step(1);
}

// The tick of the last change of 0x10 the ROM made (the step hook runs before a pending request is applied).
static int g_last_sec;
static uint32_t g_last_change;
static void watch_seconds(void)
{
    int v = nib(0x10);
    if (v != g_last_sec) { g_last_sec = v; g_last_change = tc_tick_counter(); }
}

// Steps one instruction at a time until the request leaves PENDING (at most 3 s of emulated time).
static tc_clock_state_t step_until(uint32_t id, uint32_t *written)
{
    uint32_t start = tc_tick_counter();
    tc_clock_state_t st;
    while ((st = tc_clock_poll(id, written)) == TC_CLOCK_PENDING && tc_ticks_since(start, tc_tick_counter()) < 3 * TC_TICK_HZ)
        tc_step(1);
    return st;
}

#define HMS(h, m, s) ((uint32_t)((h) * 3600 + (m) * 60 + (s)))
#define NEED_ROM() do { if (!have) TEST_IGNORE_MESSAGE("ROM or save not present"); } while (0)

static void test_request_applies_at_quiet_point(void)
{
    NEED_ROM();
    g_last_sec = nib(0x10);
    g_last_change = tc_tick_counter();
    tc_debug_set_step_hook(watch_seconds);
    // 14:32:05.900: with whole seconds the pet would end up most of a second behind
    const uint32_t target_ms = HMS(14, 32, 5) * 1000u + 900u, t_req = tc_tick_counter();
    uint32_t id = tc_clock_request_ms(target_ms), written = 0;
    TEST_ASSERT_NOT_EQUAL(0, id);
    TEST_ASSERT_EQUAL(TC_CLOCK_APPLIED, step_until(id, &written));
    uint32_t since = tc_ticks_since(g_last_change, tc_tick_counter());
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(TC_CLOCK_QUIET_TICKS, since);
    TEST_ASSERT_LESS_THAN_UINT32(TC_TICK_HZ, since);
    TEST_ASSERT_EQUAL_UINT32(written, clock_now());
    // The pet's second began at its tick: the written second is the real one at that moment, to the nearest.
    uint64_t real_ms = target_ms + (uint64_t)tc_ticks_since(t_req, g_last_change) * 1000u / TC_TICK_HZ;
    int64_t err = (int64_t)written * 1000 - (int64_t)real_ms;
    TEST_ASSERT_TRUE_MESSAGE(err >= -501 && err <= 501, "written second more than half a second off");
}

static void test_rollovers_carry_from_written_value(void)
{
    NEED_ROM();
    static const uint32_t target[] = { HMS(9, 59, 59), HMS(19, 59, 59), HMS(23, 59, 59) };
    for (int i = 0; i < 3; i++) {
        setUp();
        uint32_t written = 0;
        TEST_ASSERT_EQUAL(TC_CLOCK_APPLIED, step_until(tc_clock_request_ms(target[i] * 1000u), &written));
        run_ms(3000);
        TEST_ASSERT_EQUAL_UINT32((written + 3) % 86400, clock_now());
    }
}

static void test_write_changes_only_clock_nibbles(void)
{
    NEED_ROM();
    uint32_t start = tc_tick_counter();
    TEST_ASSERT_EQUAL(TC_CLOCK_APPLIED, step_until(tc_clock_request_ms(HMS(3, 3, 3) * 1000u), NULL));
    uint32_t ticks = tc_ticks_since(start, tc_tick_counter());
    static uint8_t a[MEM_RAM_SIZE], b[MEM_RAM_SIZE];
    for (int i = 0; i < MEM_RAM_SIZE; i++) a[i] = (uint8_t)nib(i);
    setUp();  // the same run without the request, to the same tick
    start = tc_tick_counter();
    while (tc_ticks_since(start, tc_tick_counter()) < ticks) tc_step(1);
    TEST_ASSERT_EQUAL_UINT32(ticks, tc_ticks_since(start, tc_tick_counter()));
    for (int i = 0; i < MEM_RAM_SIZE; i++) b[i] = (uint8_t)nib(i);
    for (int i = 0; i < MEM_RAM_SIZE; i++) {
        if (i >= 0x10 && i <= 0x15) continue;
        char msg[32];
        snprintf(msg, sizeof msg, "nibble 0x%03X", i);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(b[i], a[i], msg);
    }
}

static void test_newer_request_replaces_older(void)
{
    NEED_ROM();
    uint32_t id1 = tc_clock_request_ms(HMS(8, 0, 0) * 1000u), id2 = tc_clock_request_ms(HMS(12, 0, 0) * 1000u), written = 0;
    TEST_ASSERT_NOT_EQUAL(id1, id2);
    // The older request is folded into the newer one: it reports the newer one's progress and result, so a
    // caller whose request was overtaken (the panel while the ball's confirm was tapped) hears the clock was set.
    TEST_ASSERT_EQUAL(TC_CLOCK_PENDING, tc_clock_poll(id1, NULL));
    TEST_ASSERT_EQUAL(TC_CLOCK_APPLIED, step_until(id2, &written));
    TEST_ASSERT_TRUE(written == HMS(12, 0, 0) || written == HMS(12, 0, 1));
    uint32_t w1 = 0;
    TEST_ASSERT_EQUAL(TC_CLOCK_APPLIED, tc_clock_poll(id1, &w1));
    TEST_ASSERT_EQUAL_UINT32(written, w1);
    TEST_ASSERT_EQUAL_UINT32(written, clock_now());  // one write, the newer target
}

// Posts a request from the step hook at the instruction the ROM starts its 09:59:59 -> 10:00:00 carry, so it is
// accepted while 0x11-0x15 are still being rewritten. The write must wait for the next tick and its quiet point.
static uint32_t g_carry_id;
static int g_carry_hour_low_at_post;
static void post_mid_carry(void)
{
    watch_seconds();
    if (g_carry_id || nib(0x10) != 0 || nib(0x11) != 5 || nib(0x12) != 9 || nib(0x13) != 5) return;
    g_carry_hour_low_at_post = nib(0x14);  // still 9: the carry hasn't reached the hours yet
    g_carry_id = tc_clock_request_ms(HMS(15, 15, 15) * 1000u);
}

static void test_request_during_rom_carry(void)
{
    NEED_ROM();
    TEST_ASSERT_EQUAL(TC_CLOCK_APPLIED, step_until(tc_clock_request_ms(HMS(9, 59, 57) * 1000u), NULL));
    g_carry_id = 0;
    g_last_sec = nib(0x10);
    g_last_change = tc_tick_counter();
    tc_debug_set_step_hook(post_mid_carry);
    run_ms(4000);  // the carry happens within these seconds; the request is posted in it
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, g_carry_id, "never saw the carry start");
    TEST_ASSERT_EQUAL(9, g_carry_hour_low_at_post);
    uint32_t written = 0;
    tc_clock_state_t st = step_until(g_carry_id, &written);
    if (st == TC_CLOCK_PENDING) TEST_FAIL_MESSAGE("not applied within 3 s");
    TEST_ASSERT_EQUAL(TC_CLOCK_APPLIED, tc_clock_poll(g_carry_id, &written));
    TEST_ASSERT_EQUAL_UINT32(written, clock_now());
    TEST_ASSERT_TRUE(written >= HMS(15, 15, 15) && written <= HMS(15, 15, 20));
    run_ms(2000);
    TEST_ASSERT_EQUAL_UINT32(written + 2, clock_now());
}

static void press(uint8_t mask, uint32_t hold_ms, uint32_t gap_ms)
{
    tc_post_buttons(mask);
    run_ms(hold_ms);
    tc_post_buttons(0);
    run_ms(gap_ms);
}

static void test_busy_when_rom_clock_menu_open(void)
{
    NEED_ROM();
    press(TC_BTN_MASK(TC_BTN_B), 200, 1500);  // the clock screen
    press(TC_BTN_MASK(TC_BTN_A) | TC_BTN_MASK(TC_BTN_C), 3000, 800);  // hold A+C: set mode, the clock stops
    uint32_t stopped = clock_now();
    TEST_ASSERT_EQUAL_UINT32(0, stopped % 60);  // set mode zeroes the seconds
    uint32_t id = tc_clock_request_ms(HMS(7, 0, 0) * 1000u);
    run_ms(2500);
    TEST_ASSERT_EQUAL(TC_CLOCK_BUSY, tc_clock_poll(id, NULL));
    TEST_ASSERT_EQUAL_UINT32(stopped, clock_now());
}

static void test_unknown_id_is_busy(void)
{
    TEST_ASSERT_EQUAL(TC_CLOCK_BUSY, tc_clock_poll(12345, NULL));
    TEST_ASSERT_EQUAL(TC_CLOCK_BUSY, tc_clock_poll(0, NULL));
}

int main(void)
{
    static uint8_t rom[TC_ROM_BYTES];
    static uint16_t program[TC_ROM_WORDS];
    const char *root = getenv("TAMAORB_ROOT");
    char path[512];
    snprintf(path, sizeof path, "%s/roms/saves/p1.sav", root ? root : TAMAORB_ROOT_DEFAULT);
    FILE *f = fopen(path, "rb");
    size_t rec_len = f ? fread(rec, 1, sizeof rec, f) : 0;
    if (f) fclose(f);
    ps_meta_t meta;
    static const tc_platform_t plat = { .now_us = now_us, .sleep_until_us = sleep_until_us };
    have = test_load_file("tama.bin", rom, sizeof rom) && tc_rom_decode(rom, sizeof rom, program) && rec_len &&
           ps_record_decode(rec, rec_len, &meta, &payload, &payload_len);
    if (have) have = tc_init(program, &plat);

    UNITY_BEGIN();
    RUN_TEST(test_unknown_id_is_busy);
    RUN_TEST(test_request_applies_at_quiet_point);
    RUN_TEST(test_rollovers_carry_from_written_value);
    RUN_TEST(test_write_changes_only_clock_nibbles);
    RUN_TEST(test_newer_request_replaces_older);
    RUN_TEST(test_request_during_rom_carry);
    RUN_TEST(test_busy_when_rom_clock_menu_open);
    return UNITY_END();
}
