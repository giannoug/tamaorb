// SPDX-License-Identifier: GPL-3.0-or-later
// main/pet_store.c and the pet logic in main/rom_switch.c on the host, over a fake NVS (fake_nvs.c) and fakes
// for the SD card, the saver, the emulator phase and restarts (fake_idf.c).
#include <stdio.h>
#include <string.h>
#include "fake_idf.h"
#include "fake_nvs.h"
#include "nvs.h"
#include "pet_store.h"
#include "rom_store.h"
#include "rom_switch.h"
#include "unity.h"

static uint8_t P1[20], P2[20];

static void hex20(const char *hex, uint8_t out[20])
{
    for (int i = 0; i < 20; i++) sscanf(hex + 2 * i, "%2hhx", &out[i]);
}

static pet_ref_t ref(const uint8_t sha1[20], uint8_t id)
{
    pet_ref_t r = { .id = id };
    memcpy(r.sha1, sha1, 20);
    return r;
}

static pet_info_t s_list[PET_STORE_MAX];
static int list(void) { return pet_store_list(s_list, PET_STORE_MAX); }

void setUp(void)
{
    hex20("4b4979cf92dc9d2fb6d7295a38f209f3da144f72", P1);
    hex20("09e5101b37636a314fc599d5d69b4846721b3c88", P2);
    fake_idf_reset();
    pet_store_init();
    pet_store_test_reset();
    rom_switch_test_reset();
}
void tearDown(void) {}

// ---- the fake itself, on the edges pet_store relies on ----
static void test_fake_nvs_matches_idf_edges(void)
{
    nvs_handle_t h;
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, nvs_open_from_partition(ROM_STORE_PARTITION, "pets", NVS_READONLY, &h));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_open_from_partition(ROM_STORE_PARTITION, "pets", NVS_READWRITE, &h));
    uint8_t b[4];
    size_t len = sizeof b;
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, nvs_get_blob(h, "nope", b, &len));
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, nvs_erase_key(h, "nope"));
    nvs_iterator_t it = (nvs_iterator_t)1;
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, nvs_entry_find(ROM_STORE_PARTITION, "pets", NVS_TYPE_BLOB, &it));
    TEST_ASSERT_NULL(it);
    const uint8_t v[3] = { 1, 2, 3 };
    TEST_ASSERT_EQUAL(ESP_OK, nvs_set_blob(h, "k", v, sizeof v));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_commit(h));
    len = sizeof b;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_get_blob(h, "k", b, &len));
    TEST_ASSERT_EQUAL(3, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(v, b, 3);
    TEST_ASSERT_EQUAL(ESP_OK, nvs_entry_find(ROM_STORE_PARTITION, "pets", NVS_TYPE_BLOB, &it));
    TEST_ASSERT_NOT_NULL(it);
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_NOT_FOUND, nvs_entry_next(&it));
    TEST_ASSERT_NULL(it);
    // keys hold 15 characters: a longer one is refused on write, as nvs_page.cpp does, not cut short
    TEST_ASSERT_EQUAL(ESP_OK, nvs_set_blob(h, "abcdefghijklmno", v, sizeof v));
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_KEY_TOO_LONG, nvs_set_blob(h, "abcdefghijklmnop", v, sizeof v));
    TEST_ASSERT_FALSE(fake_nvs_has("pets", "abcdefghijklmnop"));
    nvs_close(h);
}

// ---- pet_store ----
static void test_create_names_in_order(void)
{
    pet_ref_t a, b;
    TEST_ASSERT_EQUAL(ESP_OK, pet_store_create(P1, TC_MODEL_P1, &a));
    TEST_ASSERT_EQUAL(ESP_OK, pet_store_create(P1, TC_MODEL_P1, &b));
    TEST_ASSERT_EQUAL(0, a.id);
    TEST_ASSERT_EQUAL(1, b.id);
    TEST_ASSERT_EQUAL(2, list());
    TEST_ASSERT_EQUAL(0, s_list[0].ref.id);
    TEST_ASSERT_EQUAL_STRING("P1 #1", s_list[0].name);
    TEST_ASSERT_EQUAL_STRING("P1 #2", s_list[1].name);
}

static void test_save_without_name_is_a_pet(void)
{
    fake_saver_put(P2, 3, 1, 100, true);
    TEST_ASSERT_EQUAL(1, list());
    TEST_ASSERT_EQUAL(3, s_list[0].ref.id);
    TEST_ASSERT_EQUAL_STRING("P2 #1", s_list[0].name);
    TEST_ASSERT_TRUE(s_list[0].has_save);
}

static void test_name_cached_until_rename(void)
{
    pet_ref_t a;
    TEST_ASSERT_EQUAL(ESP_OK, pet_store_create(P1, TC_MODEL_P1, &a));
    char name[PS_NAME_MAX + 1];
    TEST_ASSERT_EQUAL(ESP_OK, pet_store_name(&a, name));
    TEST_ASSERT_EQUAL_STRING("P1 #1", name);
    int reads = fake_nvs_reads();
    TEST_ASSERT_EQUAL(ESP_OK, pet_store_name(&a, name));
    TEST_ASSERT_EQUAL(reads, fake_nvs_reads());
    TEST_ASSERT_EQUAL(ESP_OK, pet_store_rename(&a, "Bob"));
    TEST_ASSERT_EQUAL(ESP_OK, pet_store_name(&a, name));
    TEST_ASSERT_EQUAL_STRING("Bob", name);
}

static void test_failed_rename_writes_nothing(void)
{
    pet_ref_t a;
    TEST_ASSERT_EQUAL(ESP_OK, pet_store_create(P1, TC_MODEL_P1, &a));
    TEST_ASSERT_EQUAL(ESP_OK, pet_store_rename(&a, "Bob"));
    fake_saver_put(P2, 0, 1, 100, true);  // a pet with a save but no stored name
    pet_ref_t b = ref(P2, 0);
    int writes = fake_nvs_writes();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, pet_store_rename(&b, "bob"));  // taken, case-insensitively
    TEST_ASSERT_EQUAL(writes, fake_nvs_writes());
}

static void test_missing_pet_has_no_name(void)
{
    pet_ref_t a = ref(P1, 5);
    char name[PS_NAME_MAX + 1];
    int writes = fake_nvs_writes();
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, pet_store_name(&a, name));
    TEST_ASSERT_EQUAL(writes, fake_nvs_writes());
    TEST_ASSERT_EQUAL(0, list());
}

static void test_delete_removes_saves_and_name(void)
{
    pet_ref_t a;
    TEST_ASSERT_EQUAL(ESP_OK, pet_store_create(P1, TC_MODEL_P1, &a));
    fake_saver_put(P1, 0, 1, 100, true);
    TEST_ASSERT_TRUE(fake_nvs_has("saves", "s4b4979cf92_0a"));
    TEST_ASSERT_TRUE(fake_nvs_has("pets", "n4b4979cf92_0"));
    TEST_ASSERT_EQUAL(ESP_OK, pet_store_delete(&a));
    TEST_ASSERT_EQUAL(0, list());
    TEST_ASSERT_FALSE(fake_nvs_has("saves", "s4b4979cf92_0a"));
    TEST_ASSERT_FALSE(fake_nvs_has("pets", "n4b4979cf92_0"));
}

static void test_delete_running_refused(void)
{
    pet_ref_t a;
    TEST_ASSERT_EQUAL(ESP_OK, pet_store_create(P1, TC_MODEL_P1, &a));
    fake_saver_set_running(P1, 0);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, pet_store_delete(&a));
    TEST_ASSERT_EQUAL(1, list());
}

// ---- rom_switch ----
static void test_boot_pick_newest_trusted(void)
{
    fake_saver_put(P1, 0, 5, 1000, true);
    fake_saver_put(P1, 2, 2, 2000, true);
    pet_ref_t out;
    TEST_ASSERT_EQUAL(ESP_OK, pet_for_rom(P1, TC_MODEL_P1, &out));
    TEST_ASSERT_EQUAL(2, out.id);

    fake_idf_reset();
    pet_store_test_reset();
    TEST_ASSERT_EQUAL(ESP_OK, pet_for_rom(P1, TC_MODEL_P1, &out));  // no pets: a new pet 0
    TEST_ASSERT_EQUAL(0, out.id);
    TEST_ASSERT_EQUAL(1, list());
    TEST_ASSERT_EQUAL_STRING("P1 #1", s_list[0].name);
}

static void on_card_running_p1(void)
{
    fake_rom_add_sd("tama.bin", P1);
    fake_rom_set_cached(P1);
    fake_saver_set_running(P1, 0);
    fake_emu_set_phase(EMU_RUN);
}

static void test_failed_new_pet_leaves_nothing(void)
{
    on_card_running_p1();
    pet_ref_t a;
    TEST_ASSERT_EQUAL(ESP_OK, pet_store_create(P1, TC_MODEL_P1, &a));
    int before = list();
    fake_saver_fail_save(true);
    pet_ref_t created;
    TEST_ASSERT_EQUAL(ESP_FAIL, pet_new(P1, &created));
    TEST_ASSERT_EQUAL(before, list());
    TEST_ASSERT_EQUAL(0, fake_restarts());
}

static void test_switch_records_prev_and_active(void)
{
    on_card_running_p1();
    pet_ref_t a, b;
    TEST_ASSERT_EQUAL(ESP_OK, pet_store_create(P1, TC_MODEL_P1, &a));
    TEST_ASSERT_EQUAL(ESP_OK, pet_store_create(P1, TC_MODEL_P1, &b));
    TEST_ASSERT_EQUAL(ESP_OK, pet_switch_to(&b));
    pet_ref_t got;
    TEST_ASSERT_TRUE(pet_store_active(&got));
    TEST_ASSERT_TRUE(pet_ref_eq(&got, &b));
    TEST_ASSERT_TRUE(pet_store_prev(&got));
    TEST_ASSERT_TRUE(pet_ref_eq(&got, &a));
    TEST_ASSERT_EQUAL(1, fake_restarts());
}

static void test_switch_refused_while_waiting(void)
{
    on_card_running_p1();
    pet_ref_t a, b;
    TEST_ASSERT_EQUAL(ESP_OK, pet_store_create(P1, TC_MODEL_P1, &a));
    TEST_ASSERT_EQUAL(ESP_OK, pet_store_create(P1, TC_MODEL_P1, &b));
    fake_emu_set_phase(EMU_WAIT);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, pet_switch_to(&b));
    TEST_ASSERT_EQUAL(0, fake_restarts());
}

// Running P1 #0; the previous pet is P2 #0 ("P2 #1"), on the card unless told otherwise.
static pet_ref_t back_setup(bool p2_on_card)
{
    on_card_running_p1();
    if (p2_on_card) fake_rom_add_sd("tamag2.bin", P2);
    pet_ref_t b;
    TEST_ASSERT_EQUAL(ESP_OK, pet_store_create(P2, TC_MODEL_P2, &b));
    TEST_ASSERT_EQUAL(ESP_OK, pet_store_set_prev(&b));
    return b;
}

static void test_back_kept_while_asking(void)
{
    back_setup(true);
    pet_switch_boot(true);
    pet_ref_t out;
    char name[PS_NAME_MAX + 1];
    TEST_ASSERT_TRUE(pet_prev(&out, name));
    TEST_ASSERT_EQUAL_STRING("P2 #1", name);
    TEST_ASSERT_TRUE(fake_nvs_has("pets", "prev"));  // kept: a restart asks again, with Back
    pet_prev_forget();
    TEST_ASSERT_FALSE(pet_prev(&out, name));
    TEST_ASSERT_FALSE(fake_nvs_has("pets", "prev"));
}

static void test_back_cleared_when_not_asking(void)
{
    back_setup(true);
    pet_switch_boot(false);
    pet_ref_t out;
    TEST_ASSERT_FALSE(pet_prev(&out, NULL));
    TEST_ASSERT_FALSE(fake_nvs_has("pets", "prev"));
}

static void test_back_hidden_when_rom_missing(void)
{
    back_setup(false);
    pet_switch_boot(true);
    pet_ref_t out;
    TEST_ASSERT_FALSE(pet_prev(&out, NULL));
    TEST_ASSERT_FALSE(fake_nvs_has("pets", "prev"));
}

static void test_back_gone_after_delete(void)
{
    pet_ref_t b = back_setup(true);
    pet_switch_boot(true);
    TEST_ASSERT_EQUAL(ESP_OK, pet_delete(&b));
    pet_ref_t out;
    TEST_ASSERT_FALSE(pet_prev(&out, NULL));
    TEST_ASSERT_FALSE(fake_nvs_has("pets", "prev"));
    TEST_ASSERT_EQUAL(0, list());  // not brought back by naming it
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_fake_nvs_matches_idf_edges);
    RUN_TEST(test_create_names_in_order);
    RUN_TEST(test_save_without_name_is_a_pet);
    RUN_TEST(test_name_cached_until_rename);
    RUN_TEST(test_failed_rename_writes_nothing);
    RUN_TEST(test_missing_pet_has_no_name);
    RUN_TEST(test_delete_removes_saves_and_name);
    RUN_TEST(test_delete_running_refused);
    RUN_TEST(test_boot_pick_newest_trusted);
    RUN_TEST(test_failed_new_pet_leaves_nothing);
    RUN_TEST(test_switch_records_prev_and_active);
    RUN_TEST(test_switch_refused_while_waiting);
    RUN_TEST(test_back_kept_while_asking);
    RUN_TEST(test_back_cleared_when_not_asking);
    RUN_TEST(test_back_hidden_when_rom_missing);
    RUN_TEST(test_back_gone_after_delete);
    return UNITY_END();
}
