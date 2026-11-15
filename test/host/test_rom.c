// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "test_support.h"
#include "tc_rom.h"

static uint8_t rom[TC_ROM_BYTES];
static uint16_t words[TC_ROM_WORDS];

void setUp(void) { memset(rom, 0, sizeof rom); }
void tearDown(void) {}

static void assert_sha1(const char *hex, const uint8_t got[20])
{
    uint8_t want[20];
    test_hex20(hex, want);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, 20);
}

static void test_sha1_abc(void)
{
    uint8_t h[20];
    tc_sha1((const uint8_t *)"abc", 3, h);
    assert_sha1("a9993e364706816aba3e25717850c26c9cd0d89d", h);
}

static void test_sha1_empty(void)
{
    uint8_t h[20];
    tc_sha1((const uint8_t *)"", 0, h);
    assert_sha1("da39a3ee5e6b4b0d3255bfef95601890afd80709", h);
}

static void test_sha1_two_blocks(void)
{
    // 56-byte message forces the padding into a second block.
    const char *m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    uint8_t h[20];
    tc_sha1((const uint8_t *)m, strlen(m), h);
    assert_sha1("84983e441c3bd26ebaae4aa1f95129e5e54670f1", h);
}

static void test_identify_rejects_wrong_size(void)
{
    TEST_ASSERT_EQUAL(TC_MODEL_UNKNOWN, tc_rom_identify(rom, 100, NULL));
}

static void test_identify_zero_rom_unknown(void)
{
    TEST_ASSERT_EQUAL(TC_MODEL_UNKNOWN, tc_rom_identify(rom, TC_ROM_BYTES, NULL));
}

static void test_decode_big_endian(void)
{
    rom[0] = 0x0A; rom[1] = 0xBC; rom[2] = 0xFF; rom[3] = 0xFF;
    TEST_ASSERT_TRUE(tc_rom_decode(rom, TC_ROM_BYTES, words));
    TEST_ASSERT_EQUAL_HEX16(0x0ABC, words[0]);
    TEST_ASSERT_EQUAL_HEX16(0x0FFF, words[1]);
    TEST_ASSERT_FALSE(tc_rom_decode(rom, 100, words));
}

static void test_model_names(void)
{
    TEST_ASSERT_EQUAL_STRING("P1", tc_model_name(TC_MODEL_P1));
    TEST_ASSERT_EQUAL_STRING("P2", tc_model_name(TC_MODEL_P2));
    TEST_ASSERT_EQUAL_STRING("unknown", tc_model_name(TC_MODEL_UNKNOWN));
    TEST_ASSERT_EQUAL_STRING("test ROM", tc_model_name(TC_MODEL_TEST_ROM));
}

static void identify_file(const char *file, tc_model_t want, const char *hex)
{
    if (!test_load_file(file, rom, sizeof rom)) TEST_IGNORE_MESSAGE("ROM not present");
    uint8_t h[20];
    TEST_ASSERT_EQUAL(want, tc_rom_identify(rom, sizeof rom, h));
    assert_sha1(hex, h);
}

static void test_identify_p1(void) { identify_file("tama.bin", TC_MODEL_P1, "4b4979cf92dc9d2fb6d7295a38f209f3da144f72"); }
static void test_identify_p2(void) { identify_file("tamag2.bin", TC_MODEL_P2, "09e5101b37636a314fc599d5d69b4846721b3c88"); }

static tc_rom_entry_t entry(const char *name, tc_model_t m, uint8_t tag)
{
    tc_rom_entry_t e = { .model = m };
    snprintf(e.name, sizeof e.name, "%s", name);
    memset(e.sha1, tag, 20);
    return e;
}

static void test_select_cached_wins(void)
{
    tc_rom_entry_t sd[2] = { entry("tama.bin", TC_MODEL_P1, 1), entry("tamag2.bin", TC_MODEL_P2, 2) };
    int idx = -1;
    TEST_ASSERT_EQUAL(TC_SEL_USE_CACHED, tc_rom_select(true, sd, 2, &idx));
    TEST_ASSERT_EQUAL(TC_SEL_USE_CACHED, tc_rom_select(true, NULL, 0, &idx));
}

static void test_select_single_sd(void)
{
    tc_rom_entry_t sd[2] = { entry("readme", TC_MODEL_UNKNOWN, 9), entry("tamag2.bin", TC_MODEL_P2, 2) };
    int idx = -1;
    TEST_ASSERT_EQUAL(TC_SEL_USE_SD, tc_rom_select(false, sd, 2, &idx));
    TEST_ASSERT_EQUAL(1, idx);
}

static void test_select_two_sd_picker(void)
{
    tc_rom_entry_t sd[2] = { entry("tama.bin", TC_MODEL_P1, 1), entry("tamag2.bin", TC_MODEL_P2, 2) };
    int idx;
    TEST_ASSERT_EQUAL(TC_SEL_PICKER, tc_rom_select(false, sd, 2, &idx));
}

static void test_select_duplicates_count_once(void)
{
    tc_rom_entry_t sd[2] = { entry("tama.bin", TC_MODEL_P1, 1), entry("copy.bin", TC_MODEL_P1, 1) };
    int idx = -1;
    TEST_ASSERT_EQUAL(TC_SEL_USE_SD, tc_rom_select(false, sd, 2, &idx));
    TEST_ASSERT_EQUAL(0, idx);
}

static void test_select_ignores_unknown(void)
{
    tc_rom_entry_t sd[2] = { entry("x.bin", TC_MODEL_UNKNOWN, 5), entry("test.bin", TC_MODEL_TEST_ROM, 6) };
    int idx;
    TEST_ASSERT_EQUAL(TC_SEL_NO_ROM, tc_rom_select(false, sd, 2, &idx));
}

static void test_select_nothing(void)
{
    int idx;
    TEST_ASSERT_EQUAL(TC_SEL_NO_ROM, tc_rom_select(false, NULL, 0, &idx));
}

static void test_distinct_valid_dedups_and_filters(void)
{
    tc_rom_entry_t sd[5] = { entry("junk.bin", TC_MODEL_UNKNOWN, 9), entry("tama.bin", TC_MODEL_P1, 1),
                             entry("copy.bin", TC_MODEL_P1, 1), entry("tamag2.bin", TC_MODEL_P2, 2),
                             entry("test.bin", TC_MODEL_TEST_ROM, 3) };
    int idx[5];
    TEST_ASSERT_EQUAL(2, tc_rom_distinct_valid(sd, 5, idx, 5));
    TEST_ASSERT_EQUAL(1, idx[0]);
    TEST_ASSERT_EQUAL(3, idx[1]);
    TEST_ASSERT_EQUAL(1, tc_rom_distinct_valid(sd, 5, idx, 1));  // respects max
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sha1_abc);
    RUN_TEST(test_sha1_empty);
    RUN_TEST(test_sha1_two_blocks);
    RUN_TEST(test_identify_rejects_wrong_size);
    RUN_TEST(test_identify_zero_rom_unknown);
    RUN_TEST(test_decode_big_endian);
    RUN_TEST(test_model_names);
    RUN_TEST(test_identify_p1);
    RUN_TEST(test_identify_p2);
    RUN_TEST(test_select_cached_wins);
    RUN_TEST(test_select_single_sd);
    RUN_TEST(test_select_two_sd_picker);
    RUN_TEST(test_select_duplicates_count_once);
    RUN_TEST(test_select_ignores_unknown);
    RUN_TEST(test_select_nothing);
    RUN_TEST(test_distinct_valid_dedups_and_filters);
    return UNITY_END();
}
