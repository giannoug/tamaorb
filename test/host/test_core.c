// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "unity.h"
#include "test_support.h"
#include "tc_rom.h"
#include "tama_core.h"
#include "cpu.h"
#include "tamalib.h"

static uint64_t fake_now;
static uint64_t now_us(void) { return fake_now; }
static void sleep_until_us(uint64_t ts) { if (ts > fake_now) fake_now = ts; }
static int tone_on_events;
static void tone_cb(uint32_t freq_dhz, bool on) { if (on && freq_dhz) tone_on_events++; }
static const tc_platform_t plat = { .now_us = now_us, .sleep_until_us = sleep_until_us, .tone = tone_cb };

static bool have_rom;
static uint16_t program[TC_ROM_WORDS];
static uint8_t s1[4096], s2[4096], s3[4096];

static bool inited;  // tc_init runs once, after the fork-based test (which needs uninitialised parents)
void setUp(void) { if (inited) { tc_reset_to_boot(); tc_set_realtime(false); } }
void tearDown(void) {}

static void run_seconds(uint32_t s)
{
    uint32_t start = tc_tick_counter();
    while ((uint32_t)(tc_tick_counter() - start) < s * 32768u) tc_step(64);
}

static int lit_pixels(const tc_frame_t *f)
{
    int n = 0;
    for (int y = 0; y < 16; y++) n += __builtin_popcount(f->rows[y]);
    return n;
}

static void test_boots_and_draws(void)
{
    if (!have_rom) TEST_IGNORE_MESSAGE("ROM not present");
    run_seconds(5);
    tc_frame_t f;
    tc_get_frame(&f);
    TEST_ASSERT_GREATER_OR_EQUAL(1, lit_pixels(&f));
}

static void test_frame_changed_sequence(void)
{
    if (!have_rom) TEST_IGNORE_MESSAGE("ROM not present");
    uint32_t seq = 0;
    run_seconds(1);
    TEST_ASSERT_TRUE(tc_frame_changed(&seq));
    TEST_ASSERT_FALSE(tc_frame_changed(&seq));
}

static void test_state_roundtrip_deterministic(void)
{
    if (!have_rom) TEST_IGNORE_MESSAGE("ROM not present");
    run_seconds(2);
    size_t len = tc_state_save(s1, sizeof s1);
    TEST_ASSERT_EQUAL(tc_state_size(), len);
    run_seconds(3);
    tc_frame_t f1, f2;
    tc_get_frame(&f1);
    TEST_ASSERT_TRUE(tc_state_load(s1, len));
    run_seconds(3);
    tc_get_frame(&f2);
    TEST_ASSERT_EQUAL_MEMORY(&f1, &f2, sizeof f1);
}

static void test_reset_to_boot_unsticks(void)
{
    if (!have_rom) TEST_IGNORE_MESSAGE("ROM not present");
    run_seconds(1);
    tc_reset_to_boot();
    uint32_t t0 = tc_tick_counter();
    for (int i = 0; i < 20000; i++) tc_step(1);
    TEST_ASSERT_NOT_EQUAL(t0, tc_tick_counter());
}

// tamalib keeps the previous instruction's cycle count outside its state, so a restored state can
// differ by up to 12 cycles of timing. To compare real-time and fast emulation exactly, each run
// starts from a genuine first boot in its own process.
static size_t run_fresh_boot(bool realtime, uint32_t seconds, uint8_t *out)
{
    int fd[2];
    TEST_ASSERT_EQUAL(0, pipe(fd));
    pid_t pid = fork();
    TEST_ASSERT_TRUE(pid >= 0);
    if (pid == 0) {
        close(fd[0]);
        tc_init(program, &plat);
        tc_set_realtime(realtime);
        run_seconds(seconds);
        size_t n = tc_state_save(out, sizeof s1);
        if (write(fd[1], out, n) != (ssize_t)n) _exit(1);
        _exit(0);
    }
    close(fd[1]);
    size_t got = 0;
    ssize_t r;
    while ((r = read(fd[0], out + got, sizeof s1 - got)) > 0) got += (size_t)r;
    close(fd[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    TEST_ASSERT_EQUAL(0, status);
    return got;
}

static void test_realtime_equals_fast(void)
{
    if (!have_rom) TEST_IGNORE_MESSAGE("ROM not present");
    size_t l1 = run_fresh_boot(true, 10, s1);
    size_t l2 = run_fresh_boot(false, 10, s2);
    TEST_ASSERT_GREATER_THAN(0, l1);
    TEST_ASSERT_EQUAL(l1, l2);
    TEST_ASSERT_EQUAL_MEMORY(s1, s2, l1);
}

// Wall time spent sleeping in real-time mode must match emulated time (measured on hardware:
// truncating each wait to whole microseconds made the emulator run 0.35 % fast).
static void test_realtime_pace_matches_wall(void)
{
    if (!have_rom) TEST_IGNORE_MESSAGE("ROM not present");
    tc_set_realtime(true);
    uint64_t t0 = fake_now;
    uint32_t start = tc_tick_counter();
    run_seconds(60);
    int64_t emu_us = (int64_t)(uint32_t)(tc_tick_counter() - start) * 1000000 / 32768;
    int64_t wall_us = (int64_t)(fake_now - t0);
    TEST_ASSERT_INT64_WITHIN(5000, emu_us, wall_us);  // within the 2 ms sleep batching plus slack
}

// Every buzzer change must reach the platform as it happens; polling missed short key clicks.
static void test_tone_changes_reach_platform(void)
{
    if (!have_rom) TEST_IGNORE_MESSAGE("ROM not present");
    tone_on_events = 0;
    run_seconds(3);  // the P1 plays a sound right after boot (the test ROM beeps for its first second)
    TEST_ASSERT_GREATER_THAN(0, tone_on_events);
}

static void test_load_rejects_wrong_version(void)
{
    size_t len = tc_state_save(s1, sizeof s1);
    TEST_ASSERT_GREATER_THAN(0, len);
    s1[0] = 99;
    TEST_ASSERT_FALSE(tc_state_load(s1, len));
    TEST_ASSERT_FALSE(tc_state_load(s1, len - 1));
}

// The restore path checks an uploaded payload without loading it into the running emulator.
static void test_state_compatible(void)
{
    if (!have_rom) TEST_IGNORE_MESSAGE("ROM not present");
    size_t len = tc_state_save(s1, sizeof s1);
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_TRUE(tc_state_compatible(s1, len));
    TEST_ASSERT_FALSE(tc_state_compatible(s1, len - 1));
    s1[0]++;
    TEST_ASSERT_FALSE(tc_state_compatible(s1, len));
    TEST_ASSERT_FALSE(tc_state_compatible(NULL, 0));
}

// Version 2 saves carry the CPU's timing fields (tamalib 223ba64), so a restore resumes exactly where it was.
#define V2_EXTRA 9  // previous_cycles (1) + scaled_cycle_accumulator (4) + cpu_frequency (4)

static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static void test_state_v2_saves_timing_fields(void)
{
    if (!have_rom) TEST_IGNORE_MESSAGE("ROM not present");
    run_seconds(1);
    state_t *st = tamalib_get_state();
    *st->previous_cycles = 7;
    *st->scaled_cycle_accumulator = 1234;
    size_t len = tc_state_save(s1, sizeof s1);
    TEST_ASSERT_EQUAL(tc_state_size(), len);
    TEST_ASSERT_EQUAL_HEX8(2, s1[0]);
    *st->previous_cycles = 0;
    *st->scaled_cycle_accumulator = 0;
    TEST_ASSERT_TRUE(tc_state_load(s1, len));
    TEST_ASSERT_EQUAL(7, *st->previous_cycles);
    TEST_ASSERT_EQUAL(1234, *st->scaled_cycle_accumulator);
    TEST_ASSERT_EQUAL(32768, *st->cpu_frequency);
}

// Saves made before the timing fields existed (version 1, 9 bytes shorter) still load, with the fields reset.
static void test_state_loads_version_1(void)
{
    if (!have_rom) TEST_IGNORE_MESSAGE("ROM not present");
    run_seconds(2);
    size_t len = tc_state_save(s1, sizeof s1), v1len = len - V2_EXTRA;
    memcpy(s2, s1, v1len);
    s2[0] = 1;
    run_seconds(3);  // move on, so the load has something to undo
    state_t *st = tamalib_get_state();
    *st->previous_cycles = 5;
    *st->scaled_cycle_accumulator = 9;
    TEST_ASSERT_TRUE(tc_state_compatible(s2, v1len));
    TEST_ASSERT_TRUE(tc_state_load(s2, v1len));
    TEST_ASSERT_EQUAL(0, *st->previous_cycles);
    TEST_ASSERT_EQUAL(0, *st->scaled_cycle_accumulator);
    TEST_ASSERT_EQUAL(32768, *st->cpu_frequency);
    size_t again = tc_state_save(s3, sizeof s3);
    TEST_ASSERT_EQUAL(len, again);
    TEST_ASSERT_EQUAL_MEMORY(s1 + 1, s3 + 1, v1len - 1);  // every old field came back
    // A version byte that doesn't match the length is refused.
    TEST_ASSERT_FALSE(tc_state_compatible(s2, len));
    TEST_ASSERT_FALSE(tc_state_load(s2, len));
    s1[0] = 1;
    TEST_ASSERT_FALSE(tc_state_compatible(s1, len));
}

// A clock other than OSC1 (32768 Hz) or OSC3 (1 MHz) is corrupt: refused, and the running state is untouched.
static void test_load_rejects_bad_cpu_frequency(void)
{
    if (!have_rom) TEST_IGNORE_MESSAGE("ROM not present");
    run_seconds(1);
    size_t len = tc_state_save(s1, sizeof s1);
    put32(s1 + len - 4, 12345);
    TEST_ASSERT_FALSE(tc_state_compatible(s1, len));
    TEST_ASSERT_EQUAL(len, tc_state_save(s2, sizeof s2));
    TEST_ASSERT_FALSE(tc_state_load(s1, len));
    TEST_ASSERT_EQUAL(len, tc_state_save(s3, sizeof s3));
    TEST_ASSERT_EQUAL_MEMORY(s2, s3, len);
    put32(s1 + len - 4, 1000000);
    TEST_ASSERT_TRUE(tc_state_compatible(s1, len));
}

static void test_post_buttons_mask(void)
{
    tc_post_buttons(TC_BTN_MASK(TC_BTN_B));
    TEST_ASSERT_EQUAL_HEX8(0, tc_debug_applied_buttons());  // nothing applied until the emulator steps
    tc_step(1);
    TEST_ASSERT_EQUAL_HEX8(TC_BTN_MASK(TC_BTN_B), tc_debug_applied_buttons());
    tc_post_buttons(0);
    tc_step(1);
    TEST_ASSERT_EQUAL_HEX8(0, tc_debug_applied_buttons());
}

static void test_remote_buttons_or_touch(void)
{
    if (!have_rom) TEST_IGNORE_MESSAGE("ROM not present");
    tc_post_buttons(TC_BTN_MASK(TC_BTN_A));
    tc_post_remote_buttons(TC_BTN_MASK(TC_BTN_C));
    tc_step(1);
    TEST_ASSERT_EQUAL_HEX8(TC_BTN_MASK(TC_BTN_A) | TC_BTN_MASK(TC_BTN_C), tc_debug_applied_buttons());
    tc_post_remote_buttons(0);
    tc_step(1);
    TEST_ASSERT_EQUAL_HEX8(TC_BTN_MASK(TC_BTN_A), tc_debug_applied_buttons());
    tc_post_buttons(0);
    tc_step(1);
}

static void test_ram_snapshot_follows_publish(void)
{
    if (!have_rom) TEST_IGNORE_MESSAGE("ROM not present");
    run_seconds(1);
    size_t len = tc_state_save(s1, sizeof s1);
    TEST_ASSERT_GREATER_THAN(0, len);
    // The packed memory ends V2_EXTRA bytes before the end of the state (the timing fields follow it);
    // the RAM nibble pair (0x010, 0x011) is byte 8 of it.
    s1[len - V2_EXTRA - MEM_BUFFER_SIZE + 8] = 0x5A;
    TEST_ASSERT_TRUE(tc_state_load(s1, len));  // publishes
    uint8_t ram[TC_RAM_NIBBLES + 8];
    memset(ram, 0xEE, sizeof ram);
    TEST_ASSERT_EQUAL(MEM_RAM_SIZE, tc_get_ram(ram, sizeof ram));
    TEST_ASSERT_EQUAL_HEX8(0xA, ram[0x010]);
    TEST_ASSERT_EQUAL_HEX8(0x5, ram[0x011]);
    TEST_ASSERT_EQUAL_HEX8(0xEE, ram[TC_RAM_NIBBLES]);  // nothing written past the RAM size
    TEST_ASSERT_EQUAL(4, tc_get_ram(ram, 4));           // n caps the copy
    memset(ram, 0xEE, sizeof ram);
    TEST_ASSERT_EQUAL(0x11, tc_get_ram(ram, 0x11));     // odd n: the low nibble of the pair only
    TEST_ASSERT_EQUAL_HEX8(0xA, ram[0x010]);
    TEST_ASSERT_EQUAL_HEX8(0xEE, ram[0x011]);
}

// ---- The debug snapshot: CPU, frame and RAM from one publish ----
typedef struct { uint32_t tick; uint16_t pc; uint8_t a; uint8_t n10; } step_rec_t;
static step_rec_t g_recs[1 << 16];
static size_t g_nrec;
static void record_step(void)
{
    state_t *st = tamalib_get_state();
    if (g_nrec < sizeof g_recs / sizeof g_recs[0])
        g_recs[g_nrec++] = (step_rec_t){ *st->tick_counter, *st->pc, *st->a, (uint8_t)GET_RAM_MEMORY(st->memory, 0x10) };
}

static void test_snapshot_is_one_instant(void)
{
    if (!have_rom) TEST_IGNORE_MESSAGE("ROM not present");
    for (int k = 0; k < 5; k++) {
        g_nrec = 0;
        tc_debug_set_step_hook(record_step);
        run_seconds(1);
        tc_debug_set_step_hook(NULL);
        tc_cpu_t cpu;
        uint8_t ram[TC_RAM_NIBBLES];
        TEST_ASSERT_GREATER_THAN(0, tc_get_snapshot(&cpu, NULL, ram, sizeof ram));
        const step_rec_t *hit = NULL;
        for (size_t i = 0; i < g_nrec; i++)
            if (g_recs[i].tick == cpu.tick) hit = &g_recs[i];  // the last instruction at that tick: the publish
        TEST_ASSERT_NOT_NULL_MESSAGE(hit, "no instruction ended at the snapshot's tick");
        TEST_ASSERT_EQUAL_HEX16(hit->pc, cpu.pc);
        TEST_ASSERT_EQUAL_HEX8(hit->a, cpu.a);
        TEST_ASSERT_EQUAL_HEX8(hit->n10, ram[0x10]);
    }
}

static void test_snapshot_matches_frame_and_ram_calls(void)
{
    if (!have_rom) TEST_IGNORE_MESSAGE("ROM not present");
    uint32_t seq = 0;
    run_seconds(1);
    tc_frame_changed(&seq);
    tc_cpu_t cpu;
    tc_frame_t f1, f2;
    uint8_t r1[TC_RAM_NIBBLES], r2[TC_RAM_NIBBLES];
    size_t n = tc_get_snapshot(&cpu, &f1, r1, sizeof r1);
    TEST_ASSERT_EQUAL(tc_get_ram(r2, sizeof r2), n);
    tc_get_frame(&f2);
    TEST_ASSERT_EQUAL_MEMORY(&f2, &f1, sizeof f1);
    TEST_ASSERT_EQUAL_MEMORY(r2, r1, n);
    TEST_ASSERT_EQUAL_UINT32(seq, cpu.seq);
    run_seconds(1);
    TEST_ASSERT_TRUE(tc_frame_changed(&seq));
    tc_get_snapshot(&cpu, NULL, NULL, 0);
    TEST_ASSERT_EQUAL_UINT32(seq, cpu.seq);
}

static void test_snapshot_buttons_and_flags(void)
{
    if (!have_rom) TEST_IGNORE_MESSAGE("ROM not present");
    tc_post_buttons(TC_BTN_MASK(TC_BTN_A));
    run_seconds(1);
    tc_cpu_t cpu;
    tc_get_snapshot(&cpu, NULL, NULL, 0);
    tc_post_buttons(0);
    TEST_ASSERT_EQUAL_HEX8(TC_BTN_MASK(TC_BTN_A), cpu.buttons);
    TEST_ASSERT_EQUAL_HEX8(0, cpu.flags & ~0xF);
    TEST_ASSERT_LESS_THAN(0x2000, cpu.pc);
    TEST_ASSERT_LESS_THAN(0x1000, cpu.x);
    TEST_ASSERT_LESS_THAN(0x1000, cpu.y);
    TEST_ASSERT_LESS_THAN(16, cpu.a);
    TEST_ASSERT_EQUAL_HEX8(8, cpu.irq[5].mask & 8);  // the clock timer's 1 Hz interrupt is enabled (P1 and the test ROM)
    run_seconds(1);
    tc_get_snapshot(&cpu, NULL, NULL, 0);
    TEST_ASSERT_EQUAL_HEX8(0, cpu.buttons);
}

int main(void)
{
    static uint8_t rom[TC_ROM_BYTES];
    have_rom = test_require_rom(test_load_file(test_rom_name("tama.bin"), rom, sizeof rom));
    if (have_rom) tc_rom_decode(rom, sizeof rom, program);  // otherwise an all-zero program still runs

    UNITY_BEGIN();
    RUN_TEST(test_realtime_equals_fast);
    uint8_t early[4];
    TEST_ASSERT_EQUAL(0, tc_get_ram(early, sizeof early));  // nothing published yet
    TEST_ASSERT_EQUAL(0, tc_get_snapshot(NULL, NULL, early, sizeof early));
    inited = tc_init(program, &plat);
    TEST_ASSERT_TRUE(inited);
    RUN_TEST(test_post_buttons_mask);
    RUN_TEST(test_remote_buttons_or_touch);
    RUN_TEST(test_load_rejects_wrong_version);
    RUN_TEST(test_state_compatible);
    RUN_TEST(test_state_v2_saves_timing_fields);
    RUN_TEST(test_state_loads_version_1);
    RUN_TEST(test_load_rejects_bad_cpu_frequency);
    RUN_TEST(test_boots_and_draws);
    RUN_TEST(test_frame_changed_sequence);
    RUN_TEST(test_state_roundtrip_deterministic);
    RUN_TEST(test_reset_to_boot_unsticks);
    RUN_TEST(test_realtime_pace_matches_wall);
    RUN_TEST(test_tone_changes_reach_platform);
    RUN_TEST(test_ram_snapshot_follows_publish);
    RUN_TEST(test_snapshot_is_one_instant);
    RUN_TEST(test_snapshot_matches_frame_and_ram_calls);
    RUN_TEST(test_snapshot_buttons_and_flags);
    return UNITY_END();
}
