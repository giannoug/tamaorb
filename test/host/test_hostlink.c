// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "test_support.h"
#include "hostlink.h"
#include "tc_rom.h"

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
static const hl_ops_t ops = { store, list, rm, wifi, status, NULL };

static void feed(const char *line) { hl_feed_line(s, line, reply, NULL); }

void setUp(void)
{
    s = malloc(hl_session_size());
    hl_init(s, &ops);
    replies = 0;
    last[0] = stored_name[0] = '\0';
    stored_len = 0;
    store_result = HL_STORE_OK;
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
    static uint8_t junk[TC_ROM_BYTES];
    memset(junk, 0x5A, sizeof junk);
    TEST_ASSERT_EQUAL_STRING("@TO ERR unknown", upload("x.bin", junk, sizeof junk, NULL));
    TEST_ASSERT_EQUAL(0, stored_len);
}

static void test_put_roundtrip_p1(void)
{
    static uint8_t rom[TC_ROM_BYTES];
    if (!test_load_file("tama.bin", rom, sizeof rom)) TEST_IGNORE_MESSAGE("ROM not present");
    TEST_ASSERT_EQUAL_STRING("@TO OK P1", upload("tama.bin", rom, sizeof rom, NULL));
    TEST_ASSERT_EQUAL_STRING("tama.bin", stored_name);
    TEST_ASSERT_EQUAL(TC_ROM_BYTES, stored_len);
}

static void test_put_bad_sha_rejected(void)
{
    static uint8_t junk[TC_ROM_BYTES];
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
    static uint8_t rom[TC_ROM_BYTES];
    if (!test_load_file("tama.bin", rom, sizeof rom)) TEST_IGNORE_MESSAGE("ROM not present");
    store_result = HL_STORE_OK_REBOOT;
    TEST_ASSERT_EQUAL_STRING("@TO OK P1 REBOOT", upload("tama.bin", rom, sizeof rom, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ignores_log_lines);
    RUN_TEST(test_b64_roundtrip);
    RUN_TEST(test_put_unknown_rom_rejected);
    RUN_TEST(test_put_roundtrip_p1);
    RUN_TEST(test_put_reboot_announced);
    RUN_TEST(test_put_bad_sha_rejected);
    RUN_TEST(test_chunk_out_of_order);
    RUN_TEST(test_chunk_without_put);
    RUN_TEST(test_name_validation);
    RUN_TEST(test_oversize_rejected);
    RUN_TEST(test_short_upload_rejected);
    RUN_TEST(test_wifi_base64);
    RUN_TEST(test_list_rm_status);
    return UNITY_END();
}
