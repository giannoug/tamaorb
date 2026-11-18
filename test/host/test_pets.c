// SPDX-License-Identifier: GPL-3.0-or-later
// Pet rules that need no NVS: save keys, names, free slots and which pet runs when a ROM comes with none chosen.
#include <string.h>
#include "unity.h"
#include "persist.h"

void setUp(void) {}
void tearDown(void) {}

static void test_key_parse_roundtrip(void)
{
    uint8_t sha[20], pre[5];
    for (int i = 0; i < 20; i++) sha[i] = (uint8_t)(0xA0 + i);
    char key[16], slot = 0;
    uint8_t id = 99;
    ps_key(sha, 7, 'b', key);
    TEST_ASSERT_TRUE(ps_key_parse(key, pre, &id, &slot));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(sha, pre, 5);
    TEST_ASSERT_EQUAL(7, id);
    TEST_ASSERT_EQUAL('b', slot);
    TEST_ASSERT_FALSE(ps_key_parse("rom", pre, &id, &slot));
    TEST_ASSERT_FALSE(ps_key_parse("s12_3a", pre, &id, &slot));
    TEST_ASSERT_FALSE(ps_key_parse("sZZa0a1a2a3_0a", pre, &id, &slot));
    TEST_ASSERT_FALSE(ps_key_parse("sa0a1a2a3a4_0c", pre, &id, &slot));   // slot must be a or b
    TEST_ASSERT_FALSE(ps_key_parse("sa0a1a2a3a4_xa", pre, &id, &slot));   // pet must be a digit
    TEST_ASSERT_FALSE(ps_key_parse("sa0a1a2a3a4_0ab", pre, &id, &slot));  // trailing junk
}

static void test_name_valid(void)
{
    TEST_ASSERT_TRUE(ps_name_valid("P1 #1"));
    TEST_ASSERT_TRUE(ps_name_valid("abcdefghijklmnop"));   // 16
    TEST_ASSERT_FALSE(ps_name_valid(""));
    TEST_ASSERT_FALSE(ps_name_valid(NULL));
    TEST_ASSERT_FALSE(ps_name_valid(" x"));
    TEST_ASSERT_FALSE(ps_name_valid("x "));
    TEST_ASSERT_FALSE(ps_name_valid("abcdefghijklmnopq"));  // 17
    TEST_ASSERT_FALSE(ps_name_valid("caf\xc3\xa9"));
    TEST_ASSERT_FALSE(ps_name_valid("a\tb"));
    TEST_ASSERT_FALSE(ps_name_valid("a\x7f"));
}

static void test_name_taken_ignores_case(void)
{
    const char *names[] = { "P1 #1", "Bob" };
    TEST_ASSERT_TRUE(ps_name_taken("bob", names, 2));
    TEST_ASSERT_TRUE(ps_name_taken("p1 #1", names, 2));
    TEST_ASSERT_FALSE(ps_name_taken("Bobby", names, 2));
    TEST_ASSERT_FALSE(ps_name_taken("Bob", names, 0));
}

static void test_name_auto_lowest_free(void)
{
    char out[PS_NAME_MAX + 1];
    const char *a[] = { "P1 #1", "P1 #3" };
    ps_name_auto("P1", a, 2, out);
    TEST_ASSERT_EQUAL_STRING("P1 #2", out);
    const char *b[] = { "p1 #1" };
    ps_name_auto("P1", b, 1, out);
    TEST_ASSERT_EQUAL_STRING("P1 #2", out);
    ps_name_auto("Mothra", NULL, 0, out);
    TEST_ASSERT_EQUAL_STRING("Mothra #1", out);
}

static void test_free_pet_id(void)
{
    bool used[PS_PETS_PER_ROM];
    memset(used, 1, sizeof used);
    TEST_ASSERT_EQUAL(-1, ps_free_pet_id(used));
    used[1] = false;
    used[7] = false;
    TEST_ASSERT_EQUAL(1, ps_free_pet_id(used));
    memset(used, 0, sizeof used);
    TEST_ASSERT_EQUAL(0, ps_free_pet_id(used));
}

static void test_boot_pick_newest_save(void)
{
    ps_pet_seen_t p[PS_PETS_PER_ROM] = {0};
    p[0] = (ps_pet_seen_t){ .exists = true, .has_save = true, .save_trusted = true, .save_unix = 1000, .seq = 50 };
    p[3] = (ps_pet_seen_t){ .exists = true, .has_save = true, .save_trusted = true, .save_unix = 2000, .seq = 2 };
    p[5] = (ps_pet_seen_t){ .exists = true };
    TEST_ASSERT_EQUAL(3, ps_pick_for_rom(p));
}

static void test_boot_pick_no_saves_lowest_id(void)
{
    ps_pet_seen_t p[PS_PETS_PER_ROM] = {0};
    p[4] = (ps_pet_seen_t){ .exists = true };
    p[2] = (ps_pet_seen_t){ .exists = true };
    TEST_ASSERT_EQUAL(2, ps_pick_for_rom(p));
}

static void test_boot_pick_none(void)
{
    ps_pet_seen_t p[PS_PETS_PER_ROM] = {0};
    TEST_ASSERT_EQUAL(-1, ps_pick_for_rom(p));
}

// A save whose time isn't trusted can't be ordered by time: trusted saves come first; among untrusted ones, the
// highest sequence number wins (deterministic, nothing better is known).
static void test_boot_pick_untrusted_seq(void)
{
    ps_pet_seen_t p[PS_PETS_PER_ROM] = {0};
    p[1] = (ps_pet_seen_t){ .exists = true, .has_save = true, .save_trusted = false, .save_unix = 9999, .seq = 3 };
    p[6] = (ps_pet_seen_t){ .exists = true, .has_save = true, .save_trusted = false, .save_unix = 0, .seq = 8 };
    TEST_ASSERT_EQUAL(6, ps_pick_for_rom(p));
    p[2] = (ps_pet_seen_t){ .exists = true, .has_save = true, .save_trusted = true, .save_unix = 10, .seq = 1 };
    TEST_ASSERT_EQUAL(2, ps_pick_for_rom(p));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_key_parse_roundtrip);
    RUN_TEST(test_name_valid);
    RUN_TEST(test_name_taken_ignores_case);
    RUN_TEST(test_name_auto_lowest_free);
    RUN_TEST(test_free_pet_id);
    RUN_TEST(test_boot_pick_newest_save);
    RUN_TEST(test_boot_pick_no_saves_lowest_id);
    RUN_TEST(test_boot_pick_none);
    RUN_TEST(test_boot_pick_untrusted_seq);
    return UNITY_END();
}
