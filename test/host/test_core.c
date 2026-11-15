// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "unity.h"
#include "test_support.h"
#include "tc_rom.h"
#include "tama_core.h"

static uint32_t fake_now;
static uint32_t now_us(void) { return fake_now; }
static void sleep_until_us(uint32_t ts) { if ((int32_t)(ts - fake_now) > 0) fake_now = ts; }
static const tc_platform_t plat = { .now_us = now_us, .sleep_until_us = sleep_until_us };

static bool have_rom;
static uint16_t program[TC_ROM_WORDS];
static uint8_t s1[4096], s2[4096];

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

static void test_load_rejects_wrong_version(void)
{
    size_t len = tc_state_save(s1, sizeof s1);
    TEST_ASSERT_GREATER_THAN(0, len);
    s1[0] = 99;
    TEST_ASSERT_FALSE(tc_state_load(s1, len));
    TEST_ASSERT_FALSE(tc_state_load(s1, len - 1));
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

int main(void)
{
    static uint8_t rom[TC_ROM_BYTES];
    have_rom = test_load_file("tama.bin", rom, sizeof rom);
    if (have_rom) tc_rom_decode(rom, sizeof rom, program);  // otherwise an all-zero program still runs

    UNITY_BEGIN();
    RUN_TEST(test_realtime_equals_fast);
    inited = tc_init(program, &plat);
    TEST_ASSERT_TRUE(inited);
    RUN_TEST(test_post_buttons_mask);
    RUN_TEST(test_load_rejects_wrong_version);
    RUN_TEST(test_boots_and_draws);
    RUN_TEST(test_frame_changed_sequence);
    RUN_TEST(test_state_roundtrip_deterministic);
    RUN_TEST(test_reset_to_boot_unsticks);
    return UNITY_END();
}
