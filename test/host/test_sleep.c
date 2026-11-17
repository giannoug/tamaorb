// SPDX-License-Identifier: GPL-3.0-or-later
// The real-time sleep stretch (tc_set_sleep_stretch_us) and buzzer changes waiting for their moment, on a fake clock
// that only moves when the emulator sleeps: so "how far ahead" and "when did the beep happen" are exact.
// Runs on the real P1 when its dump is present, else on the test ROM (it beeps for its first second).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "test_support.h"
#include "tc_rom.h"
#include "tc_time.h"
#include "tama_core.h"

static uint64_t fake_now;
static uint64_t now_us(void) { return fake_now; }

#define MAX_EV 4096
static struct { uint64_t at, ahead; bool tone; } sleeps[MAX_EV];  // tone: the wait before a buzzer change
static int n_sleeps;
static void sleep_until_us(uint64_t ts)
{
    if (n_sleeps < MAX_EV) sleeps[n_sleeps] = (typeof(sleeps[0])){ fake_now, ts > fake_now ? ts - fake_now : 0, false };
    n_sleeps++;
    if (ts > fake_now) fake_now = ts;
}

typedef struct { uint64_t at; uint32_t tick; bool on; } tone_ev_t;
static tone_ev_t tones[MAX_EV];
static int n_tones;
static int n_inaudible;  // tone calls that change nothing audible (same on/off, and same pitch while on)
static struct { bool have, on; uint32_t freq; } last_tone;
static int stretch_sleeps(void);
static void tone(uint32_t freq_dhz, bool on)
{
    // a sleep that ended right now, just before this change, was the wait for the change's moment
    if (n_sleeps > 0 && n_sleeps <= MAX_EV && sleeps[n_sleeps - 1].at + sleeps[n_sleeps - 1].ahead == fake_now)
        sleeps[n_sleeps - 1].tone = true;
    if (n_tones < MAX_EV) tones[n_tones] = (tone_ev_t){ fake_now, tc_tick_counter(), on };
    n_tones++;
    if (last_tone.have && last_tone.on == on && (!on || last_tone.freq == freq_dhz)) n_inaudible++;
    last_tone.have = true, last_tone.on = on, last_tone.freq = freq_dhz;
}
static uint64_t prepares[MAX_EV];
static int n_prepares;
static void tone_prepare(void)
{
    if (n_prepares < MAX_EV) prepares[n_prepares] = fake_now;
    n_prepares++;
}

static const tc_platform_t plat = { .now_us = now_us, .sleep_until_us = sleep_until_us, .tone = tone,
                                    .tone_prepare = tone_prepare };
static bool have;
static uint64_t base_now;
static uint32_t base_tick;

// Real time the emulated moment of `tick` stands for (both clocks start together at boot).
static uint64_t moment_us(uint32_t tick) { return base_now + (uint64_t)tc_ticks_since(base_tick, tick) * 1000000u / TC_TICK_HZ; }
static int64_t ahead_us(void) { return (int64_t)moment_us(tc_tick_counter()) - (int64_t)fake_now; }

static int64_t max_ahead;
static void watch_ahead(void)
{
    int64_t a = ahead_us();
    if (a > max_ahead) max_ahead = a;
}

void setUp(void)
{
    if (!have) return;
    tc_reset_to_boot();
    fake_now = 1000000;  // not 0: a sleep target must be able to lie before "now"
    tc_set_sleep_stretch_us(TC_SLEEP_STRETCH_US_DEFAULT);
    tc_set_realtime(true);  // re-syncs tamalib's reference to now: emulated and real time start together
    base_now = fake_now;
    base_tick = tc_tick_counter();
    n_sleeps = n_tones = n_prepares = n_inaudible = 0;
    last_tone.have = false;
    max_ahead = 0;
}
void tearDown(void) { tc_debug_set_step_hook(NULL); }

static int stretch_sleeps(void)
{
    int n = 0;
    for (int i = 0; i < n_sleeps && i < MAX_EV; i++) n += !sleeps[i].tone;
    return n;
}

#define NEED() do { if (!have) TEST_IGNORE_MESSAGE("ROM not present"); } while (0)

static void run_real_us(uint64_t us)
{
    uint64_t end = fake_now + us;
    while (fake_now < end) tc_step(16);
}

static void test_stretch_default_sleeps_in_2ms_steps(void)
{
    NEED();
    run_real_us(1000000);
    TEST_ASSERT_GREATER_OR_EQUAL(300, stretch_sleeps());
    for (int i = 0; i < n_sleeps && i < MAX_EV; i++)
        if (!sleeps[i].tone) TEST_ASSERT_GREATER_OR_EQUAL_UINT64(2000, sleeps[i].ahead);
}

static void test_stretch_20ms_sleeps_rarely(void)
{
    NEED();
    tc_set_sleep_stretch_us(20000);
    tc_debug_set_step_hook(watch_ahead);
    run_real_us(2000000);
    TEST_ASSERT_LESS_OR_EQUAL(110, stretch_sleeps());
    TEST_ASSERT_GREATER_OR_EQUAL(80, stretch_sleeps());
    for (int i = 0; i < n_sleeps && i < MAX_EV; i++)
        if (!sleeps[i].tone) TEST_ASSERT_GREATER_OR_EQUAL_UINT64(20000, sleeps[i].ahead);
    TEST_ASSERT_LESS_OR_EQUAL_INT64(20000 + 500, max_ahead);  // one stretch plus at most one instruction
}

static void test_stretch_change_takes_effect(void)
{
    NEED();
    tc_set_sleep_stretch_us(20000);
    run_real_us(1000000);
    tc_set_sleep_stretch_us(TC_SLEEP_STRETCH_US_DEFAULT);  // the screen woke up mid-stretch
    int first = n_sleeps, first_stretch = stretch_sleeps();
    run_real_us(200000);
    TEST_ASSERT_GREATER_OR_EQUAL(60, stretch_sleeps() - first_stretch);  // back to ~2 ms steps (100 in 200 ms)
    for (int i = first + 1; i < n_sleeps && i < MAX_EV; i++) TEST_ASSERT_LESS_THAN_UINT64(20000, sleeps[i].ahead);
}

static void test_pace_exact_with_stretch(void)
{
    NEED();
    tc_set_sleep_stretch_us(20000);
    run_real_us(10000000);
    int64_t a = ahead_us();
    TEST_ASSERT_TRUE_MESSAGE(a >= -500 && a <= 20500, "emulated time drifted from real time");
}

// Every buzzer change reaches the platform at the real time of its emulated moment: not before, at most 1 ms after.
static void check_tones_on_time(void)
{
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, n_tones, "no buzzer change in the run");
    for (int i = 0; i < n_tones && i < MAX_EV; i++) {
        int64_t late = (int64_t)tones[i].at - (int64_t)moment_us(tones[i].tick);
        char msg[64];
        snprintf(msg, sizeof msg, "tone %d (%s) %+lld us from its moment", i, tones[i].on ? "on" : "off", (long long)late);
        TEST_ASSERT_TRUE_MESSAGE(late >= -50 && late <= 1000, msg);
    }
}

static void test_tone_on_time_in_stretch(void)
{
    NEED();
    tc_set_sleep_stretch_us(20000);
    run_real_us(3000000);
    check_tones_on_time();
}

static void test_tone_on_time_default(void)
{
    NEED();
    run_real_us(3000000);
    check_tones_on_time();
}

static void test_tone_prepare_before_wait(void)
{
    NEED();
    tc_set_sleep_stretch_us(20000);
    run_real_us(3000000);
    int ons = 0;
    for (int i = 0; i < n_tones && i < MAX_EV; i++) ons += tones[i].on;
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, ons, "no tone-on in the run");
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, n_prepares, "tone_prepare never called");
    TEST_ASSERT_LESS_OR_EQUAL(ons, n_prepares);  // only for tone-on (and only when ahead)
    // each prepare comes before a tone-on, at a time no later than it
    for (int p = 0, t = 0; p < n_prepares && p < MAX_EV; p++) {
        while (t < n_tones && !(tones[t].on && tones[t].at >= prepares[p])) t++;
        TEST_ASSERT_LESS_THAN_MESSAGE(n_tones, t, "a prepare without a following tone-on");
        t++;
    }
}

// The ROM rewrites the port holding the buzzer bit for other reasons, and sets the pitch while silent: neither
// reaches the platform, so neither splits a stretch with a wait (nor wakes the speaker's task).
static void test_inaudible_buzzer_writes_are_dropped(void)
{
    NEED();
    tc_set_sleep_stretch_us(20000);
    run_real_us(3000000);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, n_tones, "no buzzer change in the run");
    TEST_ASSERT_EQUAL_INT(0, n_inaudible);
}

// Loading a state saved later in emulated time (the tick counter jumps ahead) re-sends the buzzer state: that
// must not wait against the old timeline (it would sleep for the jump, up to hours).
static void test_state_load_in_realtime_never_sleeps_long(void)
{
    NEED();
    size_t cap = tc_state_size();
    uint8_t *later = malloc(cap);
    TEST_ASSERT_NOT_NULL(later);
    run_real_us(1000000);
    size_t len = tc_state_save(later, cap);
    TEST_ASSERT_GREATER_THAN(0, len);
    tc_reset_to_boot();
    run_real_us(100000);  // real-time on the boot timeline again: the emulator knows where it is
    int first = n_sleeps;
    TEST_ASSERT_TRUE(tc_state_load(later, len));
    for (int i = first; i < n_sleeps && i < MAX_EV; i++) TEST_ASSERT_LESS_OR_EQUAL_UINT64(20500, sleeps[i].ahead);
    free(later);
}

static void test_fast_mode_never_waits_for_tones(void)
{
    NEED();
    tc_catchup(2u * TC_TICK_HZ, NULL, NULL, 0);
    TEST_ASSERT_EQUAL(0, n_sleeps);
    TEST_ASSERT_EQUAL(0, n_prepares);
    TEST_ASSERT_EQUAL(1, n_tones);  // the final buzzer state, reported once at the end (as before)
}

int main(void)
{
    static uint8_t rom[TC_ROM_BYTES];
    static uint16_t program[TC_ROM_WORDS];
    have = test_require_rom(test_load_file(test_rom_name("tama.bin"), rom, sizeof rom)) &&
           tc_rom_decode(rom, sizeof rom, program) && tc_init(program, &plat);
    UNITY_BEGIN();
    RUN_TEST(test_fast_mode_never_waits_for_tones);
    RUN_TEST(test_stretch_default_sleeps_in_2ms_steps);
    RUN_TEST(test_stretch_20ms_sleeps_rarely);
    RUN_TEST(test_stretch_change_takes_effect);
    RUN_TEST(test_pace_exact_with_stretch);
    RUN_TEST(test_tone_on_time_default);
    RUN_TEST(test_tone_on_time_in_stretch);
    RUN_TEST(test_tone_prepare_before_wait);
    RUN_TEST(test_inaudible_buzzer_writes_are_dropped);
    RUN_TEST(test_state_load_in_realtime_never_sleeps_long);
    return UNITY_END();
}
