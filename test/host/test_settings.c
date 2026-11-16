// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "unity.h"
#include "settings.h"

static st_settings_t s;

void setUp(void) { st_defaults(&s, 1); }
void tearDown(void) {}

static bool ok(const char *k, const char *v)
{
    st_settings_t t = s;
    return st_set(&t, k, v);
}

static void test_defaults(void)
{
    TEST_ASSERT_EQUAL(80, s.brightness);
    TEST_ASSERT_EQUAL(5, s.dim_level);
    TEST_ASSERT_EQUAL(30, s.dim_usb_s);
    TEST_ASSERT_EQUAL(15, s.dim_battery_s);
    TEST_ASSERT_EQUAL(30, s.off_battery_s);
    TEST_ASSERT_EQUAL(1, s.volume);
    TEST_ASSERT_EQUAL(ST_PALETTE_CLASSIC, s.palette);
    TEST_ASSERT_EQUAL(ST_AGENT_QUIET, s.agent_feedback);
    TEST_ASSERT_EQUAL_STRING("tamaorb", s.name);
    TEST_ASSERT_EQUAL_STRING("pool.ntp.org", s.ntp);
    TEST_ASSERT_EQUAL(12, st_key_count());
}

static void test_ranges(void)
{
    TEST_ASSERT_FALSE(ok("brightness", "0"));
    TEST_ASSERT_TRUE(ok("brightness", "1"));
    TEST_ASSERT_TRUE(ok("brightness", "100"));
    TEST_ASSERT_FALSE(ok("brightness", "101"));
    TEST_ASSERT_TRUE(ok("dim_usb_s", "0"));
    TEST_ASSERT_FALSE(ok("dim_usb_s", "4"));
    TEST_ASSERT_TRUE(ok("dim_usb_s", "5"));
    TEST_ASSERT_TRUE(ok("dim_usb_s", "3600"));
    TEST_ASSERT_FALSE(ok("dim_usb_s", "3601"));
    TEST_ASSERT_FALSE(ok("off_battery_s", "9"));
    TEST_ASSERT_TRUE(ok("off_battery_s", "10"));
    TEST_ASSERT_FALSE(ok("off_battery_s", "0"));
    TEST_ASSERT_TRUE(ok("volume", "0"));
    TEST_ASSERT_FALSE(ok("volume", "12x"));
    TEST_ASSERT_FALSE(ok("volume", ""));
    TEST_ASSERT_FALSE(ok("volume", "-1"));
    TEST_ASSERT_FALSE(ok("volume", " 5"));
    TEST_ASSERT_FALSE(ok("volume", "+5"));
}

static void test_enums(void)
{
    TEST_ASSERT_TRUE(ok("palette", "night"));
    TEST_ASSERT_FALSE(ok("palette", "Night"));
    TEST_ASSERT_FALSE(ok("palette", "blue"));
    TEST_ASSERT_TRUE(ok("agent_feedback", "silent"));
}

static void test_name_rules(void)
{
    char b[80];
    TEST_ASSERT_TRUE(ok("name", "ball-2"));
    memset(b, 'a', 32); b[32] = 0;
    TEST_ASSERT_TRUE(ok("name", b));
    memset(b, 'a', 33); b[33] = 0;
    TEST_ASSERT_FALSE(ok("name", b));
    TEST_ASSERT_FALSE(ok("name", ""));
    TEST_ASSERT_FALSE(ok("name", "-ball"));
    TEST_ASSERT_FALSE(ok("name", "ball-"));
    TEST_ASSERT_FALSE(ok("name", "Ball"));
    TEST_ASSERT_FALSE(ok("name", "tama orb"));
}

static void test_ntp_rules(void)
{
    char b[80];
    TEST_ASSERT_TRUE(ok("ntp", "192.168.1.1"));
    TEST_ASSERT_TRUE(ok("ntp", "time.example.org"));
    memset(b, 'a', 63); b[63] = 0;
    TEST_ASSERT_TRUE(ok("ntp", b));
    memset(b, 'a', 64); b[64] = 0;
    TEST_ASSERT_FALSE(ok("ntp", b));
    TEST_ASSERT_FALSE(ok("ntp", ""));
    TEST_ASSERT_FALSE(ok("ntp", "a b"));
    TEST_ASSERT_FALSE(ok("ntp", "x;y"));
}

static void test_set_unknown_key(void)
{
    st_settings_t c = s;
    TEST_ASSERT_FALSE(st_set(&s, "colour", "x"));
    TEST_ASSERT_EQUAL_MEMORY(&c, &s, sizeof s);
    char out[8];
    TEST_ASSERT_FALSE(st_get(&s, "colour", out, sizeof out));
}

static void test_set_many_all_or_nothing(void)
{
    const char *k[] = {"brightness", "dim_level"};
    const char *v[] = {"50", "500"};
    int bad = -1;
    TEST_ASSERT_FALSE(st_set_many(&s, k, v, 2, &bad));
    TEST_ASSERT_EQUAL(1, bad);
    TEST_ASSERT_EQUAL(80, s.brightness);
    v[1] = "7";
    TEST_ASSERT_TRUE(st_set_many(&s, k, v, 2, &bad));
    TEST_ASSERT_EQUAL(50, s.brightness);
    TEST_ASSERT_EQUAL(7, s.dim_level);
}

static void test_get_roundtrip(void)
{
    st_settings_t a;
    st_defaults(&a, 1);
    a.brightness = 42; a.palette = ST_PALETTE_NIGHT; a.agent_feedback = ST_AGENT_VISIBLE;
    a.dim_usb_s = 0; a.volume = 9;
    strcpy(a.name, "x-1"); strcpy(a.ntp, "t.example.org");
    st_settings_t b;
    st_defaults(&b, 1);
    for (int i = 0; i < st_key_count(); i++) {
        char out[80];
        TEST_ASSERT_TRUE(st_get(&a, st_key(i), out, sizeof out));
        TEST_ASSERT_TRUE(st_set(&b, st_key(i), out));
    }
    TEST_ASSERT_EQUAL_STRING(a.name, b.name);
    TEST_ASSERT_EQUAL_STRING(a.ntp, b.ntp);
    TEST_ASSERT_EQUAL(a.palette, b.palette);
    TEST_ASSERT_EQUAL(a.agent_feedback, b.agent_feedback);
    TEST_ASSERT_EQUAL(a.dim_usb_s, b.dim_usb_s);
    TEST_ASSERT_EQUAL(a.volume, b.volume);
    TEST_ASSERT_EQUAL(a.brightness, b.brightness);
    char out[16];
    TEST_ASSERT_TRUE(st_get(&a, "palette", out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("night", out);
    TEST_ASSERT_TRUE(st_get(&a, "brightness", out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("42", out);
    TEST_ASSERT_FALSE(st_get(&a, "ntp", out, 4));   // too small
}

static void test_effective_dim(void)
{
    s.brightness = 3;
    TEST_ASSERT_EQUAL(3, st_effective_dim(&s));
    s.brightness = 80;
    TEST_ASSERT_EQUAL(5, st_effective_dim(&s));
}

static void test_flags(void)
{
    TEST_ASSERT_TRUE(st_needs_restart("name"));
    TEST_ASSERT_TRUE(st_needs_restart("ntp"));
    TEST_ASSERT_FALSE(st_needs_restart("volume"));
    TEST_ASSERT_TRUE(st_is_display("palette"));
    TEST_ASSERT_TRUE(st_is_display("brightness"));
    TEST_ASSERT_FALSE(st_is_display("volume"));
    TEST_ASSERT_TRUE(st_is_numeric("dim_usb_s"));
    TEST_ASSERT_FALSE(st_is_numeric("palette"));
}

static void test_restart_pending_against_boot_values(void)
{
    st_settings_t boot, cur;
    st_defaults(&boot, 50);
    cur = boot;
    TEST_ASSERT_FALSE(st_restart_pending(&boot, &cur));
    TEST_ASSERT_TRUE(st_set(&cur, "brightness", "10"));  // live keys never need a restart
    TEST_ASSERT_TRUE(st_set(&cur, "palette", "night"));
    TEST_ASSERT_FALSE(st_restart_pending(&boot, &cur));
    TEST_ASSERT_TRUE(st_set(&cur, "name", "kitchen"));  // e.g. over USB, which calls settings_put directly
    TEST_ASSERT_TRUE(st_restart_pending(&boot, &cur));
    TEST_ASSERT_TRUE(st_set(&cur, "name", "tamaorb"));  // back to the boot name: nothing pending
    TEST_ASSERT_FALSE(st_restart_pending(&boot, &cur));
    TEST_ASSERT_TRUE(st_set(&cur, "ntp", "time.example.org"));
    TEST_ASSERT_TRUE(st_restart_pending(&boot, &cur));
}

static void test_writer_debounce(void)
{
    st_writer_t w = {0};
    TEST_ASSERT_FALSE(st_writer_due(&w, 5000));
    st_writer_changed(&w, 1000);
    TEST_ASSERT_FALSE(st_writer_due(&w, 2999));
    TEST_ASSERT_TRUE(st_writer_due(&w, 3000));
    st_writer_changed(&w, 2500);
    TEST_ASSERT_FALSE(st_writer_due(&w, 4499));
    TEST_ASSERT_TRUE(st_writer_due(&w, 4500));
    st_writer_done(&w);
    TEST_ASSERT_FALSE(st_writer_due(&w, 9000));
    st_writer_changed(&w, 0xFFFFFC00u);
    TEST_ASSERT_FALSE(st_writer_due(&w, 0x000003CFu));
    TEST_ASSERT_TRUE(st_writer_due(&w, 0x000003D0u));
}

static void test_muted_roundtrip(void)
{
    char b[16];
    TEST_ASSERT_FALSE(s.muted);
    TEST_ASSERT_TRUE(st_get(&s, "muted", b, sizeof b));
    TEST_ASSERT_EQUAL_STRING("false", b);
    TEST_ASSERT_TRUE(st_set(&s, "muted", "true"));
    TEST_ASSERT_TRUE(s.muted);
    TEST_ASSERT_TRUE(st_get(&s, "muted", b, sizeof b));
    TEST_ASSERT_EQUAL_STRING("true", b);
    TEST_ASSERT_TRUE(st_set(&s, "muted", "false"));
    TEST_ASSERT_FALSE(s.muted);
    TEST_ASSERT_FALSE(ok("muted", "1"));
    TEST_ASSERT_FALSE(ok("muted", "True"));
    TEST_ASSERT_FALSE(ok("muted", ""));
    TEST_ASSERT_TRUE(st_is_bool("muted"));
    TEST_ASSERT_FALSE(st_is_bool("volume"));
    TEST_ASSERT_FALSE(st_is_bool("tz"));
    TEST_ASSERT_FALSE(st_is_bool(NULL));
    TEST_ASSERT_FALSE(st_needs_restart("muted"));
    TEST_ASSERT_FALSE(st_needs_restart("tz"));
    TEST_ASSERT_FALSE(st_is_display("muted"));
    TEST_ASSERT_FALSE(st_is_display("tz"));
    TEST_ASSERT_FALSE(st_is_numeric("muted"));
    TEST_ASSERT_FALSE(st_is_numeric("tz"));
}

static void test_effective_volume_muted_keeps_volume(void)
{
    TEST_ASSERT_TRUE(st_set(&s, "volume", "20"));
    TEST_ASSERT_TRUE(st_set(&s, "muted", "true"));
    TEST_ASSERT_EQUAL(0, st_effective_volume(&s));
    TEST_ASSERT_EQUAL(20, s.volume);
    TEST_ASSERT_TRUE(st_set(&s, "muted", "false"));
    TEST_ASSERT_EQUAL(20, st_effective_volume(&s));
}

static void test_tz_accepts_name_and_posix(void)
{
    char b[80];
    TEST_ASSERT_EQUAL_STRING("Etc/UTC", s.tz);
    TEST_ASSERT_TRUE(st_set(&s, "tz", "Europe/Berlin"));
    TEST_ASSERT_TRUE(st_get(&s, "tz", b, sizeof b));
    TEST_ASSERT_EQUAL_STRING("Europe/Berlin", b);
    TEST_ASSERT_TRUE(st_set(&s, "tz", "<+03>-3"));
    TEST_ASSERT_EQUAL_STRING("<+03>-3", s.tz);
    TEST_ASSERT_TRUE(ok("tz", "EST5EDT,M3.2.0,M11.1.0"));
}

static void test_tz_rejects_unknown_name(void)
{
    TEST_ASSERT_FALSE(ok("tz", "Europe/Nowhere"));
    TEST_ASSERT_FALSE(ok("tz", "Berlin"));
    TEST_ASSERT_FALSE(ok("tz", ""));
    TEST_ASSERT_FALSE(ok("tz", "\"; rm"));
    char longv[80];
    memset(longv, 'A', sizeof longv - 1);
    longv[sizeof longv - 1] = 0;
    TEST_ASSERT_FALSE(ok("tz", longv));
    longv[63] = 0;   // 63 chars of letters is still not POSIX (no offset)
    TEST_ASSERT_FALSE(ok("tz", longv));
}

static void test_tz_posix_lookup(void)
{
    st_set(&s, "tz", "Europe/Berlin");
    TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", st_tz_posix(&s));
    st_set(&s, "tz", "<+03>-3");
    TEST_ASSERT_EQUAL_STRING("<+03>-3", st_tz_posix(&s));
    st_defaults(&s, 1);
    TEST_ASSERT_EQUAL_STRING("UTC0", st_tz_posix(&s));
}

static void test_set_many_bad_tz_is_all_or_nothing(void)
{
    const char *k[] = {"muted", "tz", "volume"};
    const char *v[] = {"true", "Europe/Nowhere", "50"};
    st_settings_t before = s;
    int bad = -1;
    TEST_ASSERT_FALSE(st_set_many(&s, k, v, 3, &bad));
    TEST_ASSERT_EQUAL(1, bad);
    TEST_ASSERT_EQUAL_MEMORY(&before, &s, sizeof s);
    v[1] = "Asia/Tokyo";
    TEST_ASSERT_TRUE(st_set_many(&s, k, v, 3, &bad));
    TEST_ASSERT_TRUE(s.muted);
    TEST_ASSERT_EQUAL_STRING("Asia/Tokyo", s.tz);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults);
    RUN_TEST(test_ranges);
    RUN_TEST(test_enums);
    RUN_TEST(test_name_rules);
    RUN_TEST(test_ntp_rules);
    RUN_TEST(test_set_unknown_key);
    RUN_TEST(test_set_many_all_or_nothing);
    RUN_TEST(test_get_roundtrip);
    RUN_TEST(test_effective_dim);
    RUN_TEST(test_flags);
    RUN_TEST(test_restart_pending_against_boot_values);
    RUN_TEST(test_writer_debounce);
    RUN_TEST(test_muted_roundtrip);
    RUN_TEST(test_effective_volume_muted_keeps_volume);
    RUN_TEST(test_tz_accepts_name_and_posix);
    RUN_TEST(test_tz_rejects_unknown_name);
    RUN_TEST(test_tz_posix_lookup);
    RUN_TEST(test_set_many_bad_tz_is_all_or_nothing);
    return UNITY_END();
}
