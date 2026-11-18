// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "test_support.h"
#include "hostlink.h"
#include "tc_rom.h"
#include "settings.h"

static hl_session_t *s;
static char last[256];
static int replies;
static char stored_name[64], wifi_ssid[64], wifi_pass[64];
static size_t stored_len;

static void reply(void *ctx, const char *line) { snprintf(last, sizeof last, "%s", line); replies++; }
static hl_store_result_t store_result = HL_STORE_OK;
static hl_store_result_t store(void *ctx, const char *name, const uint8_t *d, size_t n) { snprintf(stored_name, sizeof stored_name, "%s", name); stored_len = n; return store_result; }
static void list(void *ctx, hl_emit_fn emit, void *e) { emit(e, "tama.bin P1 4b4979cf92dc9d2fb6d7295a38f209f3da144f72"); }
static bool rm(void *ctx, const char *name) { return strcmp(name, "tama.bin") == 0; }
static bool wifi(void *ctx, const char *ssid, const char *pass) { snprintf(wifi_ssid, 64, "%s", ssid); snprintf(wifi_pass, 64, "%s", pass); return true; }
static void status(void *ctx, char *out, size_t cap) { snprintf(out, cap, "rom=P1 wifi=1"); }
static st_settings_t put_val;
static int put_calls;
static bool put_display;
static void get_settings(void *ctx, st_settings_t *out) { st_defaults(out, 50); }
static void put_settings(void *ctx, const st_settings_t *s_, bool display) { put_val = *s_; put_calls++; put_display = display; }
static hl_clock_t clock_result;
static int32_t clock_written;
static int clock_calls;
static hl_clock_t clock_op(void *ctx, int32_t *written_s)
{
    clock_calls++;
    if (clock_result == HL_CLOCK_OK) *written_s = clock_written;
    return clock_result;
}
static const hl_ops_t ops = { .store = store, .list = list, .remove = rm, .wifi = wifi, .status = status,
                              .get_settings = get_settings, .put_settings = put_settings, .clock = clock_op, .ctx = NULL };

static void feed(const char *line) { hl_feed_line(s, line, reply, NULL); }

void setUp(void)
{
    s = malloc(hl_session_size());
    hl_init(s, &ops);
    replies = 0;
    last[0] = stored_name[0] = '\0';
    stored_len = 0;
    store_result = HL_STORE_OK;
    put_calls = 0;
    put_display = false;
    clock_result = HL_CLOCK_OK;
    clock_calls = 0;
}
void tearDown(void) { free(s); }

static void sha1_hex(const uint8_t *d, size_t n, char out[41])
{
    uint8_t h[20];
    tc_sha1(d, n, h);
    for (int i = 0; i < 20; i++) sprintf(out + 2 * i, "%02x", h[i]);
}

// Uploads `data` in 768-byte chunks; returns the last reply.
static const char *upload(const char *name, const uint8_t *data, size_t n, const char *sha_override)
{
    char hex[41], line[HL_MAX_LINE];
    sha1_hex(data, n, hex);
    snprintf(line, sizeof line, "@TO PUT %s %zu %s", name, n, sha_override ? sha_override : hex);
    feed(line);
    if (strcmp(last, "@TO OK") != 0) return last;
    for (size_t off = 0, seq = 0; off < n; off += HL_MAX_CHUNK, seq++) {
        size_t len = n - off < HL_MAX_CHUNK ? n - off : HL_MAX_CHUNK;
        int p = snprintf(line, sizeof line, "@TO D %zu ", seq);
        hl_b64_encode(data + off, len, line + p, sizeof line - p);
        feed(line);
    }
    feed("@TO END");
    return last;
}

static void test_ignores_log_lines(void)
{
    feed("I (123) main: hi");
    feed("");
    TEST_ASSERT_EQUAL(0, replies);
}

static void test_b64_roundtrip(void)
{
    char enc[16];
    uint8_t dec[16];
    TEST_ASSERT_EQUAL(8, hl_b64_encode((const uint8_t *)"hello", 5, enc, sizeof enc));
    TEST_ASSERT_EQUAL_STRING("aGVsbG8=", enc);
    TEST_ASSERT_EQUAL(5, hl_b64_decode("aGVsbG8=", dec, sizeof dec));
    TEST_ASSERT_EQUAL_MEMORY("hello", dec, 5);
    TEST_ASSERT_EQUAL(-1, hl_b64_decode("a$==", dec, sizeof dec));
    TEST_ASSERT_EQUAL(0, hl_b64_decode("", dec, sizeof dec));
}

static void test_put_unknown_rom_rejected(void)
{
    static uint8_t junk[TC_ROM_BYTES_S46];
    memset(junk, 0x5A, sizeof junk);
    TEST_ASSERT_EQUAL_STRING("@TO ERR unknown", upload("x.bin", junk, sizeof junk, NULL));
    TEST_ASSERT_EQUAL(0, stored_len);
}

// Uploads are accepted only for ROMs identified as P1 or P2. These run on made-up bytes with an identify hook that
// calls them a P1, so the upload path is tested without Bandai's dumps (the hash table itself: test_rom).
static tc_model_t as_p1(void *ctx, const uint8_t *data, size_t len) { return TC_MODEL_P1; }
static void use_p1_identify(void)
{
    static hl_ops_t p1;
    p1 = ops;
    p1.identify = as_p1;
    hl_init(s, &p1);
}
static uint8_t *fake_rom(void)
{
    static uint8_t rom[TC_ROM_BYTES_S46];
    for (size_t i = 0; i < sizeof rom; i++) rom[i] = (uint8_t)(i * 13 + 7);
    return rom;
}

static void test_put_roundtrip_p1(void)
{
    use_p1_identify();
    TEST_ASSERT_EQUAL_STRING("@TO OK P1", upload("tama.bin", fake_rom(), TC_ROM_BYTES_S46, NULL));
    TEST_ASSERT_EQUAL_STRING("tama.bin", stored_name);
    TEST_ASSERT_EQUAL(TC_ROM_BYTES_S46, stored_len);
}

static void test_put_bad_sha_rejected(void)
{
    static uint8_t junk[TC_ROM_BYTES_S46];
    TEST_ASSERT_EQUAL_STRING("@TO ERR sha1", upload("x.bin", junk, sizeof junk, "0000000000000000000000000000000000000000"));
}

static void test_chunk_out_of_order(void)
{
    feed("@TO PUT x.bin 12288 0000000000000000000000000000000000000000");
    feed("@TO D 1 AAAA");
    TEST_ASSERT_EQUAL_STRING("@TO ERR seq", last);
}

static void test_chunk_without_put(void)
{
    feed("@TO D 0 AAAA");
    TEST_ASSERT_EQUAL_STRING("@TO ERR state", last);
}

static void test_name_validation(void)
{
    feed("@TO PUT ../x.bin 12288 0000000000000000000000000000000000000000");
    TEST_ASSERT_EQUAL_STRING("@TO ERR name", last);
    feed("@TO PUT tama.txt 12288 0000000000000000000000000000000000000000");
    TEST_ASSERT_EQUAL_STRING("@TO ERR name", last);
    feed("@TO RM ../../etc");
    TEST_ASSERT_EQUAL_STRING("@TO ERR name", last);
}

static void test_oversize_rejected(void)
{
    feed("@TO PUT big.bin 20000 0000000000000000000000000000000000000000");
    TEST_ASSERT_EQUAL_STRING("@TO ERR size", last);
}

static void test_short_upload_rejected(void)
{
    feed("@TO PUT x.bin 12288 0000000000000000000000000000000000000000");
    feed("@TO D 0 AAAA");
    feed("@TO END");
    TEST_ASSERT_EQUAL_STRING("@TO ERR size", last);
}

static void test_wifi_base64(void)
{
    feed("@TO WIFI SG9tZQ== czNjcjN0");  // "Home", "s3cr3t"
    TEST_ASSERT_EQUAL_STRING("@TO OK", last);
    TEST_ASSERT_EQUAL_STRING("Home", wifi_ssid);
    TEST_ASSERT_EQUAL_STRING("s3cr3t", wifi_pass);
    feed("@TO WIFI T3Blbg==");  // open network, no password
    TEST_ASSERT_EQUAL_STRING("@TO OK", last);
    TEST_ASSERT_EQUAL_STRING("", wifi_pass);
}

static void test_list_rm_status(void)
{
    feed("@TO LIST");
    TEST_ASSERT_EQUAL_STRING("@TO OK", last);
    TEST_ASSERT_EQUAL(2, replies);
    feed("@TO RM tama.bin");
    TEST_ASSERT_EQUAL_STRING("@TO OK", last);
    feed("@TO RM missing.bin");
    TEST_ASSERT_EQUAL_STRING("@TO ERR notfound", last);
    feed("@TO STATUS");
    TEST_ASSERT_EQUAL_STRING("@TO STATUS rom=P1 wifi=1", last);
    feed("@TO NOPE");
    TEST_ASSERT_EQUAL_STRING("@TO ERR cmd", last);
}

static void test_put_reboot_announced(void)
{
    use_p1_identify();
    store_result = HL_STORE_OK_REBOOT;
    TEST_ASSERT_EQUAL_STRING("@TO OK P1 REBOOT", upload("tama.bin", fake_rom(), TC_ROM_BYTES_S46, NULL));
}

// Without the hook, the real identification decides: made-up bytes are no P1.
static void test_put_without_hook_uses_real_identify(void)
{
    TEST_ASSERT_EQUAL_STRING("@TO ERR unknown", upload("tama.bin", fake_rom(), TC_ROM_BYTES_S46, NULL));
}

static void test_get_lists_all_settings(void)
{
    feed("@TO GET");
    TEST_ASSERT_EQUAL(st_key_count() + 1, replies);
    TEST_ASSERT_EQUAL_STRING("@TO OK", last);
    TEST_ASSERT_EQUAL(12, st_key_count());
}

static char first[256];
static void reply_first(void *ctx, const char *line) { if (!first[0]) snprintf(first, sizeof first, "%s", line); }

static void test_get_first_is_brightness(void)
{
    first[0] = '\0';
    hl_feed_line(s, "@TO GET", reply_first, NULL);
    TEST_ASSERT_EQUAL_STRING("@TO SET brightness=80", first);
}

static void test_set_ok(void)
{
    feed("@TO SET volume=0 palette=night");
    TEST_ASSERT_EQUAL_STRING("@TO OK", last);
    TEST_ASSERT_EQUAL(1, put_calls);
    TEST_ASSERT_EQUAL(0, put_val.volume);
    TEST_ASSERT_EQUAL(ST_PALETTE_NIGHT, put_val.palette);
    TEST_ASSERT_TRUE(put_display);
}

static void test_set_non_display_change(void)
{
    feed("@TO SET volume=0 brightness=80");
    TEST_ASSERT_EQUAL_STRING("@TO OK", last);
    TEST_ASSERT_FALSE(put_display);
}

static void test_set_bad_value(void)
{
    feed("@TO SET volume=5 brightness=0");
    TEST_ASSERT_EQUAL_STRING("@TO ERR brightness", last);
    TEST_ASSERT_EQUAL(0, put_calls);
}

static void test_set_no_equals(void)
{
    feed("@TO SET volume");
    TEST_ASSERT_EQUAL_STRING("@TO ERR data", last);
    TEST_ASSERT_EQUAL(0, put_calls);
}

static void test_clock_ok_replies_time(void)
{
    clock_written = 14 * 3600 + 32 * 60 + 5;
    feed("@TO CLOCK");
    TEST_ASSERT_EQUAL_STRING("@TO OK 14:32:05", last);
    TEST_ASSERT_EQUAL(1, clock_calls);
}

static void test_clock_refusals(void)
{
    static const struct { hl_clock_t r; const char *want; } cases[] = {
        {HL_CLOCK_UNSUPPORTED, "@TO ERR unsupported"}, {HL_CLOCK_NO_TIME, "@TO ERR notime"},
        {HL_CLOCK_CATCHUP, "@TO ERR catchup"}, {HL_CLOCK_BUSY, "@TO ERR busy"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        clock_result = cases[i].r;
        feed("@TO CLOCK");
        TEST_ASSERT_EQUAL_STRING(cases[i].want, last);
    }
}

static void test_clock_without_op_is_cmd_error(void)
{
    static const hl_ops_t none = { .store = store, .list = list, .remove = rm, .wifi = wifi, .status = status,
                                   .get_settings = get_settings, .put_settings = put_settings, .clock = NULL };
    hl_init(s, &none);
    feed("@TO CLOCK");
    TEST_ASSERT_EQUAL_STRING("@TO ERR cmd", last);
    TEST_ASSERT_EQUAL(0, clock_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ignores_log_lines);
    RUN_TEST(test_b64_roundtrip);
    RUN_TEST(test_put_unknown_rom_rejected);
    RUN_TEST(test_put_roundtrip_p1);
    RUN_TEST(test_put_reboot_announced);
    RUN_TEST(test_put_without_hook_uses_real_identify);
    RUN_TEST(test_put_bad_sha_rejected);
    RUN_TEST(test_chunk_out_of_order);
    RUN_TEST(test_chunk_without_put);
    RUN_TEST(test_name_validation);
    RUN_TEST(test_oversize_rejected);
    RUN_TEST(test_short_upload_rejected);
    RUN_TEST(test_wifi_base64);
    RUN_TEST(test_list_rm_status);
    RUN_TEST(test_get_lists_all_settings);
    RUN_TEST(test_get_first_is_brightness);
    RUN_TEST(test_set_ok);
    RUN_TEST(test_set_non_display_change);
    RUN_TEST(test_set_bad_value);
    RUN_TEST(test_set_no_equals);
    RUN_TEST(test_clock_ok_replies_time);
    RUN_TEST(test_clock_refusals);
    RUN_TEST(test_clock_without_op_is_cmd_error);
    return UNITY_END();
}
