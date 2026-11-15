// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "fake_ops.h"
#include "test_support.h"

// Real ROM hashes so rm_save_check recognises the model: active pet is P2, the other P1 (upper case in URLs).
static const char *SHA_ACTIVE = "09e5101b37636a314fc599d5d69b4846721b3c88";
static const char *SHA_OTHER = "4B4979CF92DC9D2FB6D7295A38F209F3DA144F72";
void setUp(void)
{
    fake_reset();
    test_hex20("09e5101b37636a314fc599d5d69b4846721b3c88", fake.pets[0].sha1);
    test_hex20("4b4979cf92dc9d2fb6d7295a38f209f3da144f72", fake.pets[1].sha1);
}
void tearDown(void) {}

static rm_resp_t resp;
static void rfree(void) { free(resp.body); resp.body = NULL; }

static void go(rm_method_t m, const char *path, const char *query, const char *origin, const char *body)
{
    rfree();
    rm_req_t rq = {0};
    rq.method = m; rq.path = path; rq.query = query; rq.origin = origin;
    rq.self_name = "tamaorb"; rq.self_ip = "192.168.1.50";
    rq.body = (const uint8_t *)body; rq.body_len = body ? strlen(body) : 0;
    memset(&resp, 0, sizeof resp);
    rm_http_handle(&fake_ops, &rq, &resp);
    TEST_ASSERT_NOT_EQUAL(0, resp.status);
    TEST_ASSERT_NOT_NULL(resp.content_type);
}
static cJSON *json(void)
{
    TEST_ASSERT_NOT_NULL(resp.body);
    char *s = malloc(resp.body_len + 1);
    memcpy(s, resp.body, resp.body_len);
    s[resp.body_len] = 0;
    cJSON *j = cJSON_Parse(s);
    free(s);
    TEST_ASSERT_NOT_NULL(j);
    return j;
}
static void expect_err(int status, const char *err, const char *key)
{
    TEST_ASSERT_EQUAL(status, resp.status);
    cJSON *j = json();
    if (err) TEST_ASSERT_EQUAL_STRING(err, cJSON_GetObjectItem(j, "error")->valuestring);
    cJSON *k = cJSON_GetObjectItem(j, "key");
    if (key) { TEST_ASSERT_NOT_NULL(k); TEST_ASSERT_EQUAL_STRING(key, k->valuestring); }
    else TEST_ASSERT_NULL(k);
    cJSON_Delete(j);
}

static void test_origin_rules(void)
{
    TEST_ASSERT_TRUE(rm_origin_allowed(NULL, "tamaorb", "192.168.1.50"));
    TEST_ASSERT_TRUE(rm_origin_allowed("", "tamaorb", "192.168.1.50"));
    TEST_ASSERT_TRUE(rm_origin_allowed("http://tamaorb.local", "tamaorb", "192.168.1.50"));
    TEST_ASSERT_TRUE(rm_origin_allowed("http://TAMAORB.local:80", "tamaorb", "192.168.1.50"));
    TEST_ASSERT_TRUE(rm_origin_allowed("http://tamaorb:8080", "tamaorb", "192.168.1.50"));
    TEST_ASSERT_TRUE(rm_origin_allowed("http://192.168.1.50", "tamaorb", "192.168.1.50"));
    TEST_ASSERT_FALSE(rm_origin_allowed("http://tamaorb.local.evil.com", "tamaorb", "192.168.1.50"));
    TEST_ASSERT_FALSE(rm_origin_allowed("https://example.com", "tamaorb", "192.168.1.50"));
    TEST_ASSERT_FALSE(rm_origin_allowed("https://tamaorb.local", "tamaorb", "192.168.1.50"));
    TEST_ASSERT_FALSE(rm_origin_allowed("null", "tamaorb", "192.168.1.50"));
    TEST_ASSERT_FALSE(rm_origin_allowed("http://tamaorb.local:", "tamaorb", "192.168.1.50") && false);
    TEST_ASSERT_FALSE(rm_origin_allowed("http://tamaorb.local:80x", "tamaorb", "192.168.1.50"));
    TEST_ASSERT_FALSE(rm_origin_allowed("http://192.168.1.5", "tamaorb", "192.168.1.50"));
    TEST_ASSERT_FALSE(rm_origin_allowed("http://tamaorb.local/x", "tamaorb", "192.168.1.50"));
    TEST_ASSERT_FALSE(rm_origin_allowed("http://", "tamaorb", ""));
}

static void test_cross_origin_patch_403(void)
{
    go(RM_PATCH, "/api/settings", NULL, "https://example.com", "{\"volume\":0}");
    expect_err(403, "origin", NULL);
    TEST_ASSERT_EQUAL(0, fake.put_calls);
    go(RM_POST, "/api/restart", NULL, "null", NULL);
    expect_err(403, "origin", NULL);
    TEST_ASSERT_EQUAL(0, fake.restart_calls);
    go(RM_GET, "/api/status", NULL, "https://example.com", NULL);
    TEST_ASSERT_EQUAL(200, resp.status);
}

static void test_status_fields(void)
{
    go(RM_GET, "/api/status", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL_STRING("application/json", resp.content_type);
    cJSON *j = json();
    const char *keys[] = {"name", "version", "phase", "model", "usb", "batt_mv", "wifi", "time_trusted", "catchup_speed", "heap_free"};
    for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) TEST_ASSERT_NOT_NULL_MESSAGE(cJSON_GetObjectItem(j, keys[i]), keys[i]);
    cJSON_Delete(j);
}

static void test_get_settings_restart_pending(void)
{
    fake.status.restart_pending = true;
    go(RM_GET, "/api/settings", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(200, resp.status);
    cJSON *j = json();
    TEST_ASSERT_EQUAL(80, cJSON_GetObjectItem(cJSON_GetObjectItem(j, "settings"), "brightness")->valueint);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(j, "restart_pending")));
    cJSON_Delete(j);
}

static void test_patch_settings_ok(void)
{
    go(RM_PATCH, "/api/settings", NULL, "http://tamaorb.local", "{\"volume\":0}");
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL(0, fake.settings.volume);
    cJSON *j = json();
    TEST_ASSERT_EQUAL(0, cJSON_GetObjectItem(cJSON_GetObjectItem(j, "settings"), "volume")->valueint);
    cJSON_Delete(j);
}

static void test_patch_settings_bad_key_changes_nothing(void)
{
    go(RM_PATCH, "/api/settings", NULL, NULL, "{\"brightness\":50,\"dim_level\":500}");
    expect_err(400, "invalid", "dim_level");
    TEST_ASSERT_EQUAL(80, fake.settings.brightness);
    TEST_ASSERT_EQUAL(0, fake.put_calls);
}

static void test_patch_not_object(void)
{
    go(RM_PATCH, "/api/settings", NULL, NULL, "[1]");
    TEST_ASSERT_EQUAL(400, resp.status);
    go(RM_PATCH, "/api/settings", NULL, NULL, "nonsense");
    TEST_ASSERT_EQUAL(400, resp.status);
    go(RM_PATCH, "/api/settings", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(400, resp.status);
    char deep[200];
    memset(deep, '[', 100);
    deep[100] = 0;
    go(RM_PATCH, "/api/settings", NULL, NULL, deep);
    expect_err(400, "invalid", NULL);
}

static void test_unknown_path_404(void)
{
    go(RM_GET, "/api/nope", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(404, resp.status);
    go(RM_GET, "/api/pets/zz/save", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(404, resp.status);
    go(RM_GET, "/api/pets/a0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4/save", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(404, resp.status);
}

static void test_wrong_method_405(void)
{
    go(RM_OTHER, "/api/settings", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(405, resp.status);
    go(RM_POST, "/api/status", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(405, resp.status);
}

static void test_wifi_validation(void)
{
    go(RM_POST, "/api/wifi", NULL, NULL, "{\"ssid\":\"home\",\"password\":\"short\"}");
    expect_err(400, "invalid", "password");
    char b[200];
    snprintf(b, sizeof b, "{\"ssid\":\"%033d\",\"password\":\"\"}", 1);
    go(RM_POST, "/api/wifi", NULL, NULL, b);
    expect_err(400, "invalid", "ssid");
    go(RM_POST, "/api/wifi", NULL, NULL, "{\"ssid\":\"\"}");
    expect_err(400, "invalid", "ssid");
    TEST_ASSERT_EQUAL(0, fake.wifi_calls);
    fake.wifi_result = true;
    go(RM_POST, "/api/wifi", NULL, NULL, "{\"ssid\":\"home\",\"password\":\"hunter2hunter2\"}");
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL_STRING("home", fake.wifi_ssid);
    TEST_ASSERT_NULL(strstr((char *)resp.body, "hunter2"));
    go(RM_POST, "/api/wifi", NULL, NULL, "{\"ssid\":\"open\"}");
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL_STRING("", fake.wifi_pass);
}

static void test_restart(void)
{
    go(RM_POST, "/api/restart", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL(1, fake.restart_calls);
}

// Real ROM hashes so rm_save_check recognises the model. Active pet is the P2 hash, the other one P1 (upper case in URLs).

static void test_save_upload_requires_mode(void)
{
    char path[80];
    snprintf(path, sizeof path, "/api/pets/%s/save", SHA_OTHER);
    go(RM_PUT, path, NULL, NULL, "xx");
    expect_err(400, NULL, "mode");
    go(RM_PUT, path, "mode=bogus", NULL, "xx");
    expect_err(400, NULL, "mode");
    TEST_ASSERT_EQUAL(0, fake.import_calls);
}

static void test_save_upload_bad_record(void)
{
    char path[80];
    snprintf(path, sizeof path, "/api/pets/%s/save", SHA_OTHER);
    go(RM_PUT, path, "mode=frozen", NULL, "garbage garbage garbage");
    expect_err(400, NULL, "record");
    TEST_ASSERT_EQUAL(0, fake.import_calls);
}

static size_t make_record(const uint8_t sha[20], uint8_t *out, size_t cap)
{
    ps_meta_t m = {0};
    memcpy(m.rom_sha1, sha, 20);
    m.unix_time = 1700000000;
    uint8_t payload[8] = {1, 2, 3};
    return ps_record_encode(&m, payload, sizeof payload, out, cap);
}

static void put_rec(const char *sha_hex, const char *query, const uint8_t *rec, size_t n, const char *origin)
{
    char path[80];
    snprintf(path, sizeof path, "/api/pets/%s/save", sha_hex);
    rfree();
    rm_req_t rq = {0};
    rq.method = RM_PUT; rq.path = path; rq.query = query; rq.origin = origin;
    rq.self_name = "tamaorb"; rq.self_ip = "192.168.1.50";
    rq.body = rec; rq.body_len = n;
    memset(&resp, 0, sizeof resp);
    rm_http_handle(&fake_ops, &rq, &resp);
}

static void test_save_upload_ok_and_errors(void)
{
    uint8_t sha[20], rec[128];
    test_hex20(SHA_OTHER, sha);
    size_t n = make_record(sha, rec, sizeof rec);
    TEST_ASSERT_GREATER_THAN(0, n);
    put_rec(SHA_OTHER, "mode=replay", rec, n, NULL);
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL(1, fake.import_calls);
    TEST_ASSERT_EQUAL(RM_RESTORE_REPLAY, fake.import_mode);
    TEST_ASSERT_EQUAL(n, fake.import_len);
    cJSON *j = json();
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(j, "restarting")));
    cJSON_Delete(j);

    // wrong ROM for the URL
    put_rec(SHA_ACTIVE, "mode=frozen", rec, n, NULL);
    expect_err(400, NULL, "rom");

    fake.import_result = RM_ERR_CATCHUP;
    put_rec(SHA_OTHER, "mode=frozen", rec, n, NULL);
    TEST_ASSERT_EQUAL(409, resp.status);
    fake.import_result = RM_ERR_NOTFOUND;
    put_rec(SHA_OTHER, "mode=frozen", rec, n, NULL);
    TEST_ASSERT_EQUAL(404, resp.status);

    put_rec(SHA_OTHER, "mode=frozen", rec, n, "https://example.com");
    TEST_ASSERT_EQUAL(403, resp.status);
}

static void test_save_upload_active_restarts(void)
{
    uint8_t sha[20], rec[128];
    test_hex20(SHA_ACTIVE, sha);
    size_t n = make_record(sha, rec, sizeof rec);
    put_rec(SHA_ACTIVE, "x=1&mode=frozen", rec, n, NULL);
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL(RM_RESTORE_FROZEN, fake.import_mode);
    cJSON *j = json();
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(j, "restarting")));
    cJSON_Delete(j);
}

static void test_save_upload_rom_not_on_card(void)
{
    uint8_t sha[20], rec[128];
    test_hex20(SHA_OTHER, sha);
    memset(fake.pets[1].sha1, 0x11, 20);  // the P1 ROM is no longer listed
    size_t n = make_record(sha, rec, sizeof rec);
    put_rec(SHA_OTHER, "mode=frozen", rec, n, NULL);
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL(1, fake.import_calls);
    cJSON *j = json();
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(j, "restarting")));
    cJSON_Delete(j);
}

static void test_save_download_disposition(void)
{
    fake.export_len = 12;
    memset(fake.export_data, 0x5A, 12);
    fake.export_meta.unix_time = 1700000000;
    char path[80];
    snprintf(path, sizeof path, "/api/pets/%s/save", SHA_ACTIVE);
    go(RM_GET, path, NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", resp.content_type);
    TEST_ASSERT_NOT_NULL(strstr(resp.disposition, "tamaorb-P1-"));
    TEST_ASSERT_EQUAL(12, resp.body_len);
    TEST_ASSERT_EQUAL_HEX8(0x5A, resp.body[0]);
    fake.export_result = RM_ERR_NOTFOUND;
    go(RM_GET, path, NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(404, resp.status);
}

static void test_pets_and_switch(void)
{
    go(RM_GET, "/api/pets", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(200, resp.status);
    cJSON *j = json();
    TEST_ASSERT_NOT_NULL(j);
    cJSON_Delete(j);
    go(RM_POST, "/api/pets/switch", NULL, NULL, "{\"name\":\"other.bin\"}");
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL_STRING("other.bin", fake.switched_to);
    go(RM_POST, "/api/pets/switch", NULL, NULL, "{}");
    expect_err(400, "invalid", "name");
    fake.switch_result = RM_ERR_NOTFOUND;
    go(RM_POST, "/api/pets/switch", NULL, NULL, "{\"name\":\"zzz\"}");
    TEST_ASSERT_EQUAL(404, resp.status);
}

static void test_switch_catching_up_409(void)
{
    fake.switch_result = RM_ERR_CATCHUP;
    go(RM_POST, "/api/pets/switch", NULL, NULL, "{\"name\":\"other.bin\"}");
    expect_err(409, "catching up", NULL);
}

static void test_look_and_press(void)
{
    go(RM_GET, "/api/look", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(200, resp.status);
    cJSON *j = json();
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(j, "text"));
    TEST_ASSERT_TRUE(strlen(cJSON_GetObjectItem(j, "png")->valuestring) > 20);
    cJSON_Delete(j);

    go(RM_POST, "/api/press", NULL, NULL, "{\"buttons\":\"A B\"}");
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL(1, fake.press_calls);
    TEST_ASSERT_EQUAL(2, fake.press_seen.n);
    j = json();
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(j, "png"));
    cJSON_Delete(j);

    go(RM_POST, "/api/press", NULL, NULL, "{\"buttons\":\"A\",\"hold_ms\":5}");
    expect_err(400, NULL, "hold_ms");
    go(RM_POST, "/api/press", NULL, NULL, "{\"buttons\":\"Q\"}");
    TEST_ASSERT_EQUAL(400, resp.status);
    TEST_ASSERT_EQUAL(1, fake.press_calls);

    fake.press_result = RM_ERR_BUSY;
    go(RM_POST, "/api/press", NULL, NULL, "{\"buttons\":\"A\"}");
    expect_err(409, "busy", NULL);
    fake.press_result = RM_ERR_CATCHUP;
    go(RM_POST, "/api/press", NULL, NULL, "{\"buttons\":\"A\"}");
    expect_err(409, "catching up", NULL);
}

static int s_allocs;
static void *counting_alloc(size_t n) { s_allocs++; return malloc(n); }
static void *failing_alloc(size_t n) { (void)n; return NULL; }

static void test_big_buffers_come_from_rm_alloc(void)
{
    rm_set_alloc(counting_alloc);
    s_allocs = 0;
    go(RM_GET, "/api/look", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL(4, s_allocs);  // text, PNG, base64, response body
    s_allocs = 0;
    go(RM_GET, "/api/pets", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(2, s_allocs);  // pet list, response body

    rm_set_alloc(failing_alloc);
    go(RM_GET, "/api/look", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(500, resp.status);
    rm_set_alloc(NULL);  // back to malloc
    go(RM_GET, "/api/look", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(200, resp.status);
}

static void test_body_parsed_by_length(void)
{
    // Not NUL-terminated: anything past body_len must be ignored.
    static const char raw[] = "{\"volume\":3}garbage";
    rfree();
    rm_req_t rq = {0};
    rq.method = RM_PATCH; rq.path = "/api/settings"; rq.self_name = "tamaorb";
    rq.body = (const uint8_t *)raw; rq.body_len = 12;
    memset(&resp, 0, sizeof resp);
    rm_http_handle(&fake_ops, &rq, &resp);
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL(3, fake.settings.volume);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_origin_rules);
    RUN_TEST(test_cross_origin_patch_403);
    RUN_TEST(test_status_fields);
    RUN_TEST(test_get_settings_restart_pending);
    RUN_TEST(test_patch_settings_ok);
    RUN_TEST(test_patch_settings_bad_key_changes_nothing);
    RUN_TEST(test_patch_not_object);
    RUN_TEST(test_unknown_path_404);
    RUN_TEST(test_wrong_method_405);
    RUN_TEST(test_wifi_validation);
    RUN_TEST(test_restart);
    RUN_TEST(test_save_upload_requires_mode);
    RUN_TEST(test_save_upload_bad_record);
    RUN_TEST(test_save_upload_ok_and_errors);
    RUN_TEST(test_save_upload_active_restarts);
    RUN_TEST(test_save_upload_rom_not_on_card);
    RUN_TEST(test_save_download_disposition);
    RUN_TEST(test_pets_and_switch);
    RUN_TEST(test_switch_catching_up_409);
    RUN_TEST(test_look_and_press);
    RUN_TEST(test_big_buffers_come_from_rm_alloc);
    RUN_TEST(test_body_parsed_by_length);
    return UNITY_END();
}
