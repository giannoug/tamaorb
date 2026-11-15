// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "unity.h"
#include "test_support.h"
#include "persist.h"

static uint8_t payload[600], rec[700];
static ps_meta_t meta;

void setUp(void)
{
    for (size_t i = 0; i < sizeof payload; i++) payload[i] = (uint8_t)(i * 7 + 3);
    memset(&meta, 0, sizeof meta);
    test_hex20("4b4979cf92dc9d2fb6d7295a38f209f3da144f72", meta.rom_sha1);
    meta.time_trusted = true;
    meta.unix_time = 1794748000;
    meta.seq = 42;
}
void tearDown(void) {}

static size_t encode(void) { return ps_record_encode(&meta, payload, sizeof payload, rec, sizeof rec); }

static void test_crc32_check_value(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, ps_crc32((const uint8_t *)"123456789", 9));
}

static void test_record_roundtrip(void)
{
    size_t n = encode();
    TEST_ASSERT_EQUAL(sizeof payload + PS_OVERHEAD, n);
    ps_meta_t got;
    const uint8_t *p;
    size_t len;
    TEST_ASSERT_TRUE(ps_record_decode(rec, n, &got, &p, &len));
    TEST_ASSERT_EQUAL_MEMORY(meta.rom_sha1, got.rom_sha1, 20);
    TEST_ASSERT_EQUAL(meta.pet_id, got.pet_id);
    TEST_ASSERT_TRUE(got.time_trusted);
    TEST_ASSERT_EQUAL_INT64(meta.unix_time, got.unix_time);
    TEST_ASSERT_EQUAL_UINT32(42, got.seq);
    TEST_ASSERT_EQUAL(sizeof payload, len);
    TEST_ASSERT_EQUAL_MEMORY(payload, p, len);
}

static void test_record_untrusted_flag(void)
{
    meta.time_trusted = false;
    size_t n = encode();
    ps_meta_t got;
    const uint8_t *p;
    size_t len;
    TEST_ASSERT_TRUE(ps_record_decode(rec, n, &got, &p, &len));
    TEST_ASSERT_FALSE(got.time_trusted);
}

static void test_record_crc_flip_rejected(void)
{
    size_t n = encode();
    rec[PS_HEADER_BYTES + 10] ^= 0x01;
    ps_meta_t got;
    const uint8_t *p;
    size_t len;
    TEST_ASSERT_FALSE(ps_record_decode(rec, n, &got, &p, &len));
}

static void test_record_truncated_rejected(void)
{
    size_t n = encode();
    ps_meta_t got;
    const uint8_t *p;
    size_t len;
    TEST_ASSERT_FALSE(ps_record_decode(rec, n - 1, &got, &p, &len));
    TEST_ASSERT_FALSE(ps_record_decode(rec, 3, &got, &p, &len));
}

static void test_record_bad_version_rejected(void)
{
    size_t n = encode();
    rec[4] = PS_VERSION + 1;
    ps_meta_t got;
    const uint8_t *p;
    size_t len;
    TEST_ASSERT_FALSE(ps_record_decode(rec, n, &got, &p, &len));
}

static void test_encode_too_small_buffer(void)
{
    TEST_ASSERT_EQUAL(0, ps_record_encode(&meta, payload, sizeof payload, rec, 100));
}

static void test_key_format(void)
{
    char k[16];
    ps_key(meta.rom_sha1, 0, 'a', k);
    TEST_ASSERT_EQUAL_STRING("s4b4979cf92_0a", k);
    TEST_ASSERT_LESS_OR_EQUAL(15, strlen(k));
}

static void test_policy_after_release(void)
{
    ps_policy_t p;
    ps_policy_init(&p, 0);
    ps_policy_on_buttons(&p, 500, 0, 1);
    ps_policy_on_buttons(&p, 1000, 1, 0);
    TEST_ASSERT_FALSE(ps_policy_due(&p, 3999));
    TEST_ASSERT_TRUE(ps_policy_due(&p, 4000));
    ps_policy_saved(&p, 4000);
    TEST_ASSERT_FALSE(ps_policy_due(&p, 8000));
}

static void test_policy_periodic(void)
{
    ps_policy_t p;
    ps_policy_init(&p, 0);
    TEST_ASSERT_FALSE(ps_policy_due(&p, 299999));
    TEST_ASSERT_TRUE(ps_policy_due(&p, 300000));
}

static void test_policy_no_save_while_held(void)
{
    ps_policy_t p;
    ps_policy_init(&p, 0);
    ps_policy_on_buttons(&p, 1000, 0, 2);
    TEST_ASSERT_FALSE(ps_policy_due(&p, 11000));
}

static void test_pick_newest(void)
{
    ps_meta_t a = { .seq = 5 }, b = { .seq = 6 };
    TEST_ASSERT_EQUAL(1, ps_pick_newest(true, &a, true, &b));
    TEST_ASSERT_EQUAL(0, ps_pick_newest(true, &a, false, &b));
    TEST_ASSERT_EQUAL(1, ps_pick_newest(false, &a, true, &b));
    TEST_ASSERT_EQUAL(-1, ps_pick_newest(false, &a, false, &b));
    a.seq = 0; b.seq = 0xFFFFFFFFu;  // a was written after the counter wrapped
    TEST_ASSERT_EQUAL(0, ps_pick_newest(true, &a, true, &b));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc32_check_value);
    RUN_TEST(test_record_roundtrip);
    RUN_TEST(test_record_untrusted_flag);
    RUN_TEST(test_record_crc_flip_rejected);
    RUN_TEST(test_record_truncated_rejected);
    RUN_TEST(test_record_bad_version_rejected);
    RUN_TEST(test_encode_too_small_buffer);
    RUN_TEST(test_key_format);
    RUN_TEST(test_policy_after_release);
    RUN_TEST(test_policy_periodic);
    RUN_TEST(test_policy_no_save_while_held);
    RUN_TEST(test_pick_newest);
    return UNITY_END();
}
