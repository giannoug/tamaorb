// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "unity.h"
#include "test_support.h"
#include "remote.h"
#include "persist.h"

static uint8_t p1[20], p2[20], unk[20], payload[100], rec[200];
static ps_meta_t meta;
static size_t n;

void setUp(void)
{
    test_hex20("4b4979cf92dc9d2fb6d7295a38f209f3da144f72", p1);
    test_hex20("09e5101b37636a314fc599d5d69b4846721b3c88", p2);
    memset(unk, 0x5a, 20);
    for (size_t i = 0; i < sizeof payload; i++) payload[i] = (uint8_t)(i + 1);
    memset(&meta, 0, sizeof meta);
    memcpy(meta.rom_sha1, p1, 20);
    meta.time_trusted = true;
    meta.unix_time = 1794700000;
    meta.seq = 5;
    n = ps_record_encode(&meta, payload, sizeof payload, rec, sizeof rec);
}
void tearDown(void) {}

static bool ok_yes(const uint8_t *p, size_t l) { return l == 100; }
static bool ok_no(const uint8_t *p, size_t l) { return false; }

static void test_save_check_ok(void)
{
    ps_meta_t m;
    TEST_ASSERT_NULL(rm_save_check(rec, n, p1, ok_yes, &m));
    TEST_ASSERT_EQUAL_UINT32(5, m.seq);
    TEST_ASSERT_EQUAL_INT64(1794700000, m.unix_time);
    TEST_ASSERT_NULL(rm_save_check(rec, n, p1, NULL, &m));
}

static void test_save_check_truncated(void)
{
    ps_meta_t m;
    TEST_ASSERT_EQUAL_STRING("record", rm_save_check(rec, n - 1, p1, ok_yes, &m));
}

static void test_save_check_bad_crc(void)
{
    ps_meta_t m;
    rec[PS_HEADER_BYTES + 7] ^= 1;
    TEST_ASSERT_EQUAL_STRING("record", rm_save_check(rec, n, p1, ok_yes, &m));
}

static void test_save_check_other_rom(void)
{
    ps_meta_t m;
    TEST_ASSERT_EQUAL_STRING("rom", rm_save_check(rec, n, p2, ok_yes, &m));
}

static void test_save_check_unknown_model(void)
{
    ps_meta_t m;
    memcpy(meta.rom_sha1, unk, 20);
    n = ps_record_encode(&meta, payload, sizeof payload, rec, sizeof rec);
    TEST_ASSERT_EQUAL_STRING("model", rm_save_check(rec, n, unk, ok_yes, &m));
}

static void test_save_check_state_rejected(void)
{
    ps_meta_t m;
    TEST_ASSERT_EQUAL_STRING("state", rm_save_check(rec, n, p1, ok_no, &m));
}

// Review finding: a backup restores onto any pet of its ROM (the import rewrites the pet number).
static void test_save_check_any_pet_number(void)
{
    ps_meta_t m;
    meta.pet_id = 7;
    n = ps_record_encode(&meta, payload, sizeof payload, rec, sizeof rec);
    TEST_ASSERT_NULL(rm_save_check(rec, n, p1, ok_yes, &m));
    TEST_ASSERT_EQUAL(7, m.pet_id);
}

static void test_check_accepts_angel(void)
{
    uint8_t ang[20];
    test_hex20("f5899bb7717756ac581451cf16cf97d909961c5c", ang);
    memcpy(meta.rom_sha1, ang, 20);
    n = ps_record_encode(&meta, payload, sizeof payload, rec, sizeof rec);
    TEST_ASSERT_NULL(rm_save_check(rec, n, ang, ok_yes, NULL));
    char fn[RM_SAVE_FILENAME_LEN];
    rm_save_filename(TC_MODEL_ANGEL, &meta, fn);
    TEST_ASSERT_EQUAL_STRING("tamaorb-Angel-f5899bb7-pet1-2026-11-14.sav", fn);
}

static void test_restore_mode_parse(void)
{
    rm_restore_mode_t m;
    TEST_ASSERT_TRUE(rm_restore_mode_parse("frozen", &m));
    TEST_ASSERT_EQUAL(RM_RESTORE_FROZEN, m);
    TEST_ASSERT_TRUE(rm_restore_mode_parse("replay", &m));
    TEST_ASSERT_EQUAL(RM_RESTORE_REPLAY, m);
    TEST_ASSERT_FALSE(rm_restore_mode_parse("", &m));
    TEST_ASSERT_FALSE(rm_restore_mode_parse("Frozen", &m));
    TEST_ASSERT_FALSE(rm_restore_mode_parse(NULL, &m));
}

static void test_filename(void)
{
    char f[RM_SAVE_FILENAME_LEN];
    meta.pet_id = 2;  // two pets of one ROM saved the same day get different names
    rm_save_filename(TC_MODEL_P1, &meta, f);
    TEST_ASSERT_EQUAL_STRING("tamaorb-P1-4b4979cf-pet3-2026-11-14.sav", f);
    meta.unix_time = 0;
    meta.pet_id = 0;
    rm_save_filename(TC_MODEL_P1, &meta, f);
    TEST_ASSERT_EQUAL_STRING("tamaorb-P1-4b4979cf-pet1-undated.sav", f);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_save_check_ok);
    RUN_TEST(test_save_check_truncated);
    RUN_TEST(test_save_check_bad_crc);
    RUN_TEST(test_save_check_other_rom);
    RUN_TEST(test_save_check_unknown_model);
    RUN_TEST(test_save_check_state_rejected);
    RUN_TEST(test_save_check_any_pet_number);
    RUN_TEST(test_check_accepts_angel);
    RUN_TEST(test_restore_mode_parse);
    RUN_TEST(test_filename);
    return UNITY_END();
}
