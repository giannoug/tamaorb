// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "test_support.h"
#include "tc_rom.h"

static uint8_t rom[TC_ROM_MAX_BYTES];
static uint16_t words[TC_ROM_MAX_WORDS];

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
    TEST_ASSERT_EQUAL(TC_MODEL_UNKNOWN, tc_rom_identify(rom, TC_ROM_BYTES_S46, NULL));
}

static void test_decode_big_endian(void)
{
    rom[0] = 0x0A; rom[1] = 0xBC; rom[2] = 0xFF; rom[3] = 0xFF;
    TEST_ASSERT_TRUE(tc_rom_decode(rom, TC_ROM_BYTES_S46, words));
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

// The known dumps by their SHA-1 (MAME's hh_e0c6200.cpp); the dumps themselves never enter the repo or CI.
static void test_model_from_sha1(void)
{
    uint8_t h[20], z[20] = {0};
    test_hex20("4b4979cf92dc9d2fb6d7295a38f209f3da144f72", h);
    TEST_ASSERT_EQUAL(TC_MODEL_P1, tc_model_from_sha1(h));
    test_hex20("09e5101b37636a314fc599d5d69b4846721b3c88", h);
    TEST_ASSERT_EQUAL(TC_MODEL_P2, tc_model_from_sha1(h));
    test_hex20("6e13d015113e16198c0059b9d0c38d7027ae7324", h);
    TEST_ASSERT_EQUAL(TC_MODEL_TEST_ROM, tc_model_from_sha1(h));
    h[19] ^= 1;  // one bit off: not a known dump
    TEST_ASSERT_EQUAL(TC_MODEL_UNKNOWN, tc_model_from_sha1(h));
    TEST_ASSERT_EQUAL(TC_MODEL_UNKNOWN, tc_model_from_sha1(z));
}

// identify = the SHA-1 of the bytes, looked up in that table.
static void test_identify_reports_sha1_of_the_bytes(void)
{
    for (size_t i = 0; i < TC_ROM_BYTES_S46; i++) rom[i] = (uint8_t)(i * 31 + 5);
    uint8_t got[20], want[20];
    TEST_ASSERT_EQUAL(TC_MODEL_UNKNOWN, tc_rom_identify(rom, TC_ROM_BYTES_S46, got));
    tc_sha1(rom, TC_ROM_BYTES_S46, want);
    TEST_ASSERT_EQUAL_MEMORY(want, got, 20);
    memset(rom, 0, sizeof rom);
}

static void test_model_from_sha1_angel_mothra(void)
{
    uint8_t h[20];
    test_hex20("f5899bb7717756ac581451cf16cf97d909961c5c", h);
    TEST_ASSERT_EQUAL(TC_MODEL_ANGEL, tc_model_from_sha1(h));
    test_hex20("74c1f6761724b7cbda8bca3113db78586b786d2d", h);
    TEST_ASSERT_EQUAL(TC_MODEL_MOTHRA, tc_model_from_sha1(h));
}

static void test_decode_16k(void)
{
    rom[2 * 8191] = 0x1A;
    rom[2 * 8191 + 1] = 0xBC;
    TEST_ASSERT_TRUE(tc_rom_decode(rom, TC_ROM_BYTES_S48, words));
    TEST_ASSERT_EQUAL_HEX16(0x0ABC, words[8191]);
    TEST_ASSERT_TRUE(tc_rom_decode(rom, TC_ROM_BYTES_S46, words));
    TEST_ASSERT_FALSE(tc_rom_decode(rom, 16383, words));
    TEST_ASSERT_TRUE(tc_rom_size_ok(12288));
    TEST_ASSERT_TRUE(tc_rom_size_ok(16384));
    TEST_ASSERT_FALSE(tc_rom_size_ok(16000));
}

static void test_identify_size_mismatch(void)
{
    for (size_t i = 0; i < TC_ROM_BYTES_S46; i++) rom[i] = (uint8_t)(i * 7 + 1);
    TEST_ASSERT_EQUAL(TC_MODEL_UNKNOWN, tc_rom_identify(rom, TC_ROM_BYTES_S46, NULL));
    TEST_ASSERT_EQUAL(TC_MODEL_UNKNOWN, tc_rom_identify(rom, 16000, NULL));
    TEST_ASSERT_EQUAL(TC_MODEL_UNKNOWN, tc_rom_identify(rom, TC_ROM_BYTES_S48, NULL));
}

static void test_playable_and_names(void)
{
    TEST_ASSERT_TRUE(tc_model_playable(TC_MODEL_P1));
    TEST_ASSERT_TRUE(tc_model_playable(TC_MODEL_P2));
    TEST_ASSERT_TRUE(tc_model_playable(TC_MODEL_ANGEL));
    TEST_ASSERT_TRUE(tc_model_playable(TC_MODEL_MOTHRA));
    TEST_ASSERT_FALSE(tc_model_playable(TC_MODEL_TEST_ROM));
    TEST_ASSERT_FALSE(tc_model_playable(TC_MODEL_UNKNOWN));
    TEST_ASSERT_EQUAL_STRING("Angel", tc_model_name(TC_MODEL_ANGEL));
    TEST_ASSERT_EQUAL_STRING("Mothra", tc_model_name(TC_MODEL_MOTHRA));
    TEST_ASSERT_EQUAL_STRING("Gen 1", tc_model_long_name(TC_MODEL_P1));
    TEST_ASSERT_EQUAL_STRING("Gen 2", tc_model_long_name(TC_MODEL_P2));
    TEST_ASSERT_EQUAL_STRING("Angel", tc_model_long_name(TC_MODEL_ANGEL));
    TEST_ASSERT_EQUAL_STRING("Mothra", tc_model_long_name(TC_MODEL_MOTHRA));
    TEST_ASSERT_NULL(tc_model_long_name(TC_MODEL_TEST_ROM));
    TEST_ASSERT_EQUAL(12288, tc_model_rom_bytes(TC_MODEL_P1));
    TEST_ASSERT_EQUAL(12288, tc_model_rom_bytes(TC_MODEL_TEST_ROM));
    TEST_ASSERT_EQUAL(16384, tc_model_rom_bytes(TC_MODEL_ANGEL));
    TEST_ASSERT_EQUAL(16384, tc_model_rom_bytes(TC_MODEL_MOTHRA));
    TEST_ASSERT_EQUAL(0, tc_model_rom_bytes(TC_MODEL_UNKNOWN));
}

// Mothra's glass wires its icons to segment/COM pairs tamalib doesn't map: TamaOrb reads them from display memory.
static void test_icon_pins_mothra(void)
{
    const tc_icon_pin_t *p = tc_model_icon_pins(TC_MODEL_MOTHRA);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL(8, p[0].seg); TEST_ASSERT_EQUAL(0, p[0].com);
    TEST_ASSERT_EQUAL(17, p[1].seg); TEST_ASSERT_EQUAL(0, p[1].com);
    TEST_ASSERT_EQUAL(39, p[4].seg); TEST_ASSERT_EQUAL(8, p[4].com);
    TEST_ASSERT_EQUAL(28, p[7].seg); TEST_ASSERT_EQUAL(8, p[7].com);
    TEST_ASSERT_NULL(tc_model_icon_pins(TC_MODEL_P1));
    TEST_ASSERT_NULL(tc_model_icon_pins(TC_MODEL_ANGEL));
}

// Display nibble n (0x00-0xFF stands for 0xE00 + n) holds COMs com0..com0+3 of segment (n & 0x7F) >> 1, where
// com0 = 8 * bit 7 + 4 * bit 0 (tamalib's set_lcd).
static void test_icons_from_lcd(void)
{
    uint8_t nib[256] = {0};
    const tc_icon_pin_t *p = tc_model_icon_pins(TC_MODEL_MOTHRA);
    TEST_ASSERT_EQUAL_HEX8(0x00, tc_icons_from_lcd(nib, p));
    nib[17 * 2] = 1;             // seg 17, COM 0
    TEST_ASSERT_EQUAL_HEX8(0x02, tc_icons_from_lcd(nib, p));
    nib[0x80 + 39 * 2] = 1;      // seg 39, COM 8
    nib[0x80 + 28 * 2] = 1;      // seg 28, COM 8
    TEST_ASSERT_EQUAL_HEX8(0x92, tc_icons_from_lcd(nib, p));
    nib[0x80 + 28 * 2] = 2;      // seg 28, COM 9: not an icon
    TEST_ASSERT_EQUAL_HEX8(0x12, tc_icons_from_lcd(nib, p));
    const tc_icon_pin_t q[8] = { {28, 13} };  // COM 13: display 2, odd nibble, bit 1
    uint8_t m[256] = {0};
    m[0x80 + 28 * 2 + 1] = 2;
    TEST_ASSERT_EQUAL_HEX8(0x01, tc_icons_from_lcd(m, q));
}

static void test_distinct_valid_includes_angel(void)
{
    tc_rom_entry_t sd[5] = { entry("tama.bin", TC_MODEL_P1, 1), entry("tamaang.bin", TC_MODEL_ANGEL, 4),
                             entry("test.bin", TC_MODEL_TEST_ROM, 3), entry("tamamot.bin", TC_MODEL_MOTHRA, 5),
                             entry("copy.bin", TC_MODEL_P1, 1) };
    int idx[5];
    TEST_ASSERT_EQUAL(3, tc_rom_distinct_valid(sd, 5, idx, 5));
}

// Halt fast-forward during catch-up: on for each ROM whose proof (test_halt_ff.c, 1/6/26 h) passed.
static void test_model_halt_ff_table(void)
{
    TEST_ASSERT_TRUE(tc_model_halt_ff(TC_MODEL_P1));
    TEST_ASSERT_TRUE(tc_model_halt_ff(TC_MODEL_P2));
    TEST_ASSERT_TRUE(tc_model_halt_ff(TC_MODEL_ANGEL));
    TEST_ASSERT_TRUE(tc_model_halt_ff(TC_MODEL_MOTHRA));
    TEST_ASSERT_FALSE(tc_model_halt_ff(TC_MODEL_UNKNOWN));
    TEST_ASSERT_FALSE(tc_model_halt_ff(TC_MODEL_TEST_ROM));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_model_halt_ff_table);
    RUN_TEST(test_sha1_abc);
    RUN_TEST(test_sha1_empty);
    RUN_TEST(test_sha1_two_blocks);
    RUN_TEST(test_identify_rejects_wrong_size);
    RUN_TEST(test_identify_zero_rom_unknown);
    RUN_TEST(test_decode_big_endian);
    RUN_TEST(test_model_names);
    RUN_TEST(test_select_cached_wins);
    RUN_TEST(test_select_single_sd);
    RUN_TEST(test_select_two_sd_picker);
    RUN_TEST(test_select_duplicates_count_once);
    RUN_TEST(test_select_ignores_unknown);
    RUN_TEST(test_select_nothing);
    RUN_TEST(test_distinct_valid_dedups_and_filters);
    RUN_TEST(test_model_from_sha1);
    RUN_TEST(test_identify_reports_sha1_of_the_bytes);
    RUN_TEST(test_model_from_sha1_angel_mothra);
    RUN_TEST(test_decode_16k);
    RUN_TEST(test_identify_size_mismatch);
    RUN_TEST(test_playable_and_names);
    RUN_TEST(test_distinct_valid_includes_angel);
    RUN_TEST(test_icon_pins_mothra);
    RUN_TEST(test_icons_from_lcd);
    return UNITY_END();
}
