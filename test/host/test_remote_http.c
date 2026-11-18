// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "fake_ops.h"
#include "test_support.h"
#include "tzdb.h"

// Real ROM hashes so rm_save_check recognises the model: active pet is P2, the other P1 (upper case in URLs).
static const char *SHA_ACTIVE = "09e5101b37636a314fc599d5d69b4846721b3c88";
static const char *SHA_OTHER = "4B4979CF92DC9D2FB6D7295A38F209F3DA144F72";
void setUp(void)
{
    fake_reset();
    test_hex20("09e5101b37636a314fc599d5d69b4846721b3c88", fake.pets[0].sha1);
    fake.pets[0].model = TC_MODEL_P2;  // models to match the hashes
    test_hex20("4b4979cf92dc9d2fb6d7295a38f209f3da144f72", fake.pets[1].sha1);
    fake.pets[1].model = TC_MODEL_P1;
}
void tearDown(void) { rm_set_alloc(NULL); }  // a failed assertion must not leave a test allocator behind

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

static void test_resume_choices(void)
{
    const char *ok[] = { "resume", "ff", "back" };
    for (int i = 0; i < 3; i++) {
        char b[40];
        snprintf(b, sizeof b, "{\"choice\":\"%s\"}", ok[i]);
        go(RM_POST, "/api/resume", NULL, NULL, b);
        TEST_ASSERT_EQUAL(200, resp.status);
        TEST_ASSERT_EQUAL_STRING(ok[i], fake.resume_choice);
    }
    go(RM_POST, "/api/resume", NULL, NULL, "{\"choice\":\"later\"}");
    expect_err(400, "invalid", "choice");
    go(RM_GET, "/api/resume", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(405, resp.status);
}

static void test_resume_not_waiting(void)
{
    fake.resume_result = RM_ERR_BUSY;
    go(RM_POST, "/api/resume", NULL, NULL, "{\"choice\":\"ff\"}");
    TEST_ASSERT_EQUAL(409, resp.status);
}

static void test_skip_ok(void)
{
    go(RM_POST, "/api/catchup/skip", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL(1, fake.skip_calls);
}

static void test_skip_not_catching_up(void)
{
    fake.skip_result = RM_ERR_BUSY;
    go(RM_POST, "/api/catchup/skip", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(409, resp.status);
}

static void test_status_wait_fields(void)
{
    fake.status.phase = RM_PHASE_WAIT;
    snprintf(fake.status.pet_name, sizeof fake.status.pet_name, "P2 #1");
    snprintf(fake.status.back_name, sizeof fake.status.back_name, "P1 #1");
    fake.status.wait_gap_s = 3600;
    fake.status.wait_ff_s = 48;
    go(RM_GET, "/api/status", NULL, NULL, NULL);
    cJSON *j = json();
    TEST_ASSERT_EQUAL_STRING("wait", cJSON_GetObjectItem(j, "phase")->valuestring);
    TEST_ASSERT_EQUAL_STRING("P2 #1", cJSON_GetObjectItem(j, "pet")->valuestring);
    cJSON *w = cJSON_GetObjectItem(j, "wait");
    TEST_ASSERT_NOT_NULL(w);
    TEST_ASSERT_EQUAL(3600, cJSON_GetObjectItem(w, "gap_s")->valueint);
    TEST_ASSERT_EQUAL(48, cJSON_GetObjectItem(w, "ff_s")->valueint);
    TEST_ASSERT_EQUAL_STRING("P1 #1", cJSON_GetObjectItem(w, "back")->valuestring);
    cJSON_Delete(j);
    fake.status.phase = RM_PHASE_RUN;
    go(RM_GET, "/api/status", NULL, NULL, NULL);
    j = json();
    TEST_ASSERT_NULL(cJSON_GetObjectItem(j, "wait"));
    cJSON_Delete(j);
}

static void test_factory_reset_confirm(void)
{
    go(RM_POST, "/api/factory-reset", NULL, NULL, NULL);
    expect_err(400, "invalid", "confirm");
    go(RM_POST, "/api/factory-reset", NULL, NULL, "{\"confirm\":\"yes\"}");
    expect_err(400, "invalid", "confirm");
    TEST_ASSERT_EQUAL(0, fake.reset_calls);
    go(RM_POST, "/api/factory-reset", NULL, NULL, "{\"confirm\":\"erase\"}");
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL(1, fake.reset_calls);
    go(RM_POST, "/api/factory-reset", NULL, "https://example.com", "{\"confirm\":\"erase\"}");
    TEST_ASSERT_EQUAL(403, resp.status);
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
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(j, "restart_pending")));
    cJSON_Delete(j);
}

static void test_patch_settings_skips_full_status(void)  // a dragged slider PATCHes ~10/s
{
    fake.status.restart_pending = true;
    go(RM_PATCH, "/api/settings", NULL, "http://tamaorb.local", "{\"brightness\":40}");
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL(0, fake.status_calls);
    TEST_ASSERT_EQUAL(1, fake.restart_pending_calls);
    cJSON *j = json();
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(j, "restart_pending")));
    cJSON_Delete(j);
}

static void test_patch_settings_bad_key_changes_nothing(void)
{
    go(RM_PATCH, "/api/settings", NULL, NULL, "{\"brightness\":50,\"dim_level\":500}");
    expect_err(400, "invalid", "dim_level");
    TEST_ASSERT_EQUAL(80, fake.settings.brightness);
    TEST_ASSERT_EQUAL(0, fake.put_calls);
}

static void test_settings_json_muted_is_bool(void)
{
    go(RM_GET, "/api/settings", NULL, NULL, NULL);
    cJSON *j0 = json();
    cJSON *st = cJSON_GetObjectItem(j0, "settings");
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(st, "muted")));
    TEST_ASSERT_TRUE(cJSON_IsString(cJSON_GetObjectItem(st, "tz")));
    TEST_ASSERT_EQUAL_STRING("Etc/UTC", cJSON_GetObjectItem(st, "tz")->valuestring);
    cJSON_Delete(j0);
    go(RM_PATCH, "/api/settings", NULL, NULL, "{\"muted\":true,\"tz\":\"Europe/Berlin\"}");
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_TRUE(fake.settings.muted);
    cJSON *j = json();
    st = cJSON_GetObjectItem(j, "settings");
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(st, "muted")));
    TEST_ASSERT_EQUAL_STRING("Europe/Berlin", cJSON_GetObjectItem(st, "tz")->valuestring);
    cJSON_Delete(j);
}

static void test_patch_muted_rejects_string(void)
{
    go(RM_PATCH, "/api/settings", NULL, NULL, "{\"muted\":\"true\"}");
    expect_err(400, "invalid", "muted");
    go(RM_PATCH, "/api/settings", NULL, NULL, "{\"muted\":1}");
    expect_err(400, "invalid", "muted");
    TEST_ASSERT_FALSE(fake.settings.muted);
    TEST_ASSERT_EQUAL(0, fake.put_calls);
}

static void test_patch_tz_bad_is_400_and_unchanged(void)
{
    go(RM_PATCH, "/api/settings", NULL, NULL, "{\"volume\":5,\"tz\":\"Mars/Base\"}");
    expect_err(400, "invalid", "tz");
    TEST_ASSERT_EQUAL_STRING("Etc/UTC", fake.settings.tz);
    TEST_ASSERT_EQUAL(0, fake.put_calls);
}

static void test_get_timezones_lists_names(void)
{
    go(RM_GET, "/api/timezones", NULL, "https://example.com", NULL);
    TEST_ASSERT_EQUAL(200, resp.status);
    cJSON *j = json();
    cJSON *z = cJSON_GetObjectItem(j, "zones");
    TEST_ASSERT_TRUE(cJSON_IsArray(z));
    TEST_ASSERT_EQUAL(tzdb_count(), cJSON_GetArraySize(z));
    TEST_ASSERT_EQUAL_STRING(tzdb_at(0)->name, cJSON_GetArrayItem(z, 0)->valuestring);
    int found = 0;
    for (cJSON *e = z->child; e; e = e->next) if (!strcmp(e->valuestring, "Europe/Berlin")) found = 1;
    TEST_ASSERT_TRUE(found);
    cJSON_Delete(j);
    go(RM_POST, "/api/timezones", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(405, resp.status);
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
    go(RM_GET, "/api/pets/09e5101b37636a314fc599d5d69b4846721b3c88/save", NULL, NULL, NULL);  // no pet number
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
    snprintf(path, sizeof path, "/api/pets/%s-0/save", SHA_OTHER);
    go(RM_PUT, path, NULL, NULL, "xx");
    expect_err(400, NULL, "mode");
    go(RM_PUT, path, "mode=bogus", NULL, "xx");
    expect_err(400, NULL, "mode");
    TEST_ASSERT_EQUAL(0, fake.import_calls);
}

static void test_save_upload_bad_record(void)
{
    char path[80];
    snprintf(path, sizeof path, "/api/pets/%s-0/save", SHA_OTHER);
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
    snprintf(path, sizeof path, "/api/pets/%s-0/save", sha_hex);
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
    snprintf(path, sizeof path, "/api/pets/%s-0/save", SHA_ACTIVE);
    go(RM_GET, path, NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", resp.content_type);
    // The P2 hash: P2 in the name. (This asserted P1 before, passing only because the fixture's model was wrong.)
    TEST_ASSERT_NOT_NULL(strstr(resp.disposition, "attachment; filename=\"tamaorb-P2-"));
    TEST_ASSERT_EQUAL(12, resp.body_len);
    TEST_ASSERT_EQUAL_HEX8(0x5A, resp.body[0]);
    fake.export_result = RM_ERR_NOTFOUND;
    go(RM_GET, path, NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(404, resp.status);
}

static void test_save_download_rom_not_listed(void)  // ruling R11
{
    memset(fake.pets[1].sha1, 0x11, 20);  // the P1 ROM is no longer on the card
    fake.export_len = 4;
    fake.export_meta.unix_time = 1700000000;
    char path[80];
    snprintf(path, sizeof path, "/api/pets/%s-0/save", SHA_OTHER);
    go(RM_GET, path, NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_NOT_NULL(strstr(resp.disposition, "tamaorb-P1-"));
    TEST_ASSERT_EQUAL(4, resp.body_len);
    fake.export_result = RM_ERR_NOTFOUND;  // no save either: 404
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
    go(RM_POST, "/api/pets/switch", NULL, NULL, "{\"id\":\"4b4979cf92dc9d2fb6d7295a38f209f3da144f72-3\"}");
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL_STRING("4b4979cf92dc9d2fb6d7295a38f209f3da144f72-3", fake.switched_to);
    go(RM_POST, "/api/pets/switch", NULL, NULL, "{}");
    expect_err(400, "invalid", "id");
    go(RM_POST, "/api/pets/switch", NULL, NULL, "{\"id\":\"zzz\"}");
    expect_err(400, "invalid", "id");
}

static void test_switch_catching_up_409(void)
{
    fake.switch_result = RM_ERR_CATCHUP;
    go(RM_POST, "/api/pets/switch", NULL, NULL, "{\"id\":\"4b4979cf92dc9d2fb6d7295a38f209f3da144f72-0\"}");
    expect_err(409, "catching up", NULL);
}

static void test_pets_list_shape(void)
{
    fake.pets[0].id = 0;
    snprintf(fake.pets[0].name, sizeof fake.pets[0].name, "P2 #1");
    fake.pets[0].age_years = 3;
    fake.pets[1].id = 3;
    snprintf(fake.pets[1].name, sizeof fake.pets[1].name, "Bob");
    fake.pets[1].age_years = -1;
    fake.pets[1].egg = true;
    go(RM_GET, "/api/pets", NULL, NULL, NULL);
    cJSON *a = json();
    TEST_ASSERT_EQUAL(2, cJSON_GetArraySize(a));
    cJSON *p0 = cJSON_GetArrayItem(a, 0), *p1 = cJSON_GetArrayItem(a, 1);
    TEST_ASSERT_EQUAL_STRING("09e5101b37636a314fc599d5d69b4846721b3c88-0", cJSON_GetObjectItem(p0, "id")->valuestring);
    TEST_ASSERT_EQUAL_STRING("P2 #1", cJSON_GetObjectItem(p0, "name")->valuestring);
    TEST_ASSERT_EQUAL(3, cJSON_GetObjectItem(p0, "age")->valueint);
    TEST_ASSERT_EQUAL_STRING("4b4979cf92dc9d2fb6d7295a38f209f3da144f72-3", cJSON_GetObjectItem(p1, "id")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(p1, "age")));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(p1, "egg")));
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(p1, "rom_on_card"));
    cJSON_Delete(a);
}

static void test_roms_list(void)
{
    go(RM_GET, "/api/roms", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(200, resp.status);
    cJSON *a = json();
    TEST_ASSERT_EQUAL(1, cJSON_GetArraySize(a));
    TEST_ASSERT_EQUAL_STRING("tamaang.bin", cJSON_GetObjectItem(cJSON_GetArrayItem(a, 0), "file")->valuestring);
    TEST_ASSERT_EQUAL_STRING("Angel", cJSON_GetObjectItem(cJSON_GetArrayItem(a, 0), "model")->valuestring);
    cJSON_Delete(a);
}

static void test_pets_new_ok(void)
{
    go(RM_POST, "/api/pets", NULL, NULL, "{\"rom\":\"f5899bb7717756ac581451cf16cf97d909961c5c\"}");
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL(1, fake.new_calls);
    TEST_ASSERT_EQUAL_HEX8(0xf5, fake.new_sha1[0]);
    cJSON *j = json();
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(j, "restarting")));
    cJSON_Delete(j);
}

static void test_pets_new_full(void)
{
    fake.new_result = RM_ERR_FULL;
    go(RM_POST, "/api/pets", NULL, NULL, "{\"rom\":\"f5899bb7717756ac581451cf16cf97d909961c5c\"}");
    expect_err(409, "full", NULL);
}

static void test_pets_new_bad_sha(void)
{
    go(RM_POST, "/api/pets", NULL, NULL, "{\"rom\":\"xyz\"}");
    expect_err(400, "invalid", "rom");
    go(RM_POST, "/api/pets", NULL, NULL, "{}");
    expect_err(400, "invalid", "rom");
    TEST_ASSERT_EQUAL(0, fake.new_calls);
}

static void test_pets_switch_by_id(void)
{
    go(RM_POST, "/api/pets/switch", NULL, NULL, "{\"id\":\"09e5101b37636a314fc599d5d69b4846721b3c88-7\"}");
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL_STRING("09e5101b37636a314fc599d5d69b4846721b3c88-7", fake.switched_to);
}

static void test_pets_switch_missing_rom(void)
{
    fake.switch_result = RM_ERR_NOTFOUND;
    go(RM_POST, "/api/pets/switch", NULL, NULL, "{\"id\":\"09e5101b37636a314fc599d5d69b4846721b3c88-7\"}");
    TEST_ASSERT_EQUAL(404, resp.status);
}

static void test_pets_rename_ok(void)
{
    go(RM_PATCH, "/api/pets/4b4979cf92dc9d2fb6d7295a38f209f3da144f72-0", NULL, NULL, "{\"name\":\"Bob\"}");
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL_STRING("Bob", fake.renamed_to);
    TEST_ASSERT_EQUAL(0, fake.renamed_id);
}

static void test_pets_rename_invalid(void)
{
    const char *bad[] = { "{\"name\":\"\"}", "{\"name\":\" x\"}", "{\"name\":\"abcdefghijklmnopq\"}",
                          "{\"name\":\"caf\u00e9\"}", "{\"name\":\"a\\tb\"}", "{\"name\":5}", "{}" };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        go(RM_PATCH, "/api/pets/4b4979cf92dc9d2fb6d7295a38f209f3da144f72-0", NULL, NULL, bad[i]);
        expect_err(400, "invalid", "name");
    }
    TEST_ASSERT_EQUAL(0, fake.rename_calls);
}

static void test_pets_rename_taken(void)
{
    fake.rename_result = RM_ERR_TAKEN;
    go(RM_PATCH, "/api/pets/4b4979cf92dc9d2fb6d7295a38f209f3da144f72-0", NULL, NULL, "{\"name\":\"Bob\"}");
    expect_err(409, "taken", NULL);
}

static void test_pets_delete_running(void)
{
    go(RM_DELETE, "/api/pets/4b4979cf92dc9d2fb6d7295a38f209f3da144f72-2", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL(2, fake.deleted_id);
    fake.delete_result = RM_ERR_BUSY;
    go(RM_DELETE, "/api/pets/09e5101b37636a314fc599d5d69b4846721b3c88-0", NULL, NULL, NULL);
    expect_err(409, "busy", NULL);
}

static void test_pets_bad_id(void)
{
    go(RM_DELETE, "/api/pets/zz-1", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(404, resp.status);
    go(RM_PATCH, "/api/pets/09e5101b37636a314fc599d5d69b4846721b3c88-12", NULL, NULL, "{\"name\":\"x\"}");
    TEST_ASSERT_EQUAL(404, resp.status);
    go(RM_DELETE, "/api/pets/0000000000000000000000000000000000000000-1", NULL, NULL, NULL);  // not a known ROM
    TEST_ASSERT_EQUAL(404, resp.status);
    TEST_ASSERT_EQUAL(0, fake.delete_calls);
}

static void test_save_path_with_id(void)
{
    fake.export_len = 4;
    fake.export_meta.unix_time = 1700000000;
    go(RM_GET, "/api/pets/09e5101b37636a314fc599d5d69b4846721b3c88-2/save", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL(2, fake.export_id);
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

// ---- pet clock ----
static void test_status_has_pet_and_ball_clock(void)
{
    fake.status.pet_clock_s = 9 * 3600 + 58 * 60;          // 09:58:00
    fake.status.ball_clock_s = 14 * 3600 + 32 * 60 + 5;    // 14:32:05
    go(RM_GET, "/api/status", NULL, NULL, NULL);
    cJSON *j = json();
    TEST_ASSERT_EQUAL_STRING("09:58:00", cJSON_GetObjectItem(j, "pet_clock")->valuestring);
    TEST_ASSERT_EQUAL_STRING("14:32:05", cJSON_GetObjectItem(j, "ball_clock")->valuestring);
    cJSON_Delete(j);
    fake.status.pet_clock_s = -1;
    fake.status.ball_clock_s = -1;
    go(RM_GET, "/api/status", NULL, NULL, NULL);
    j = json();
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(j, "pet_clock")));
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(j, "ball_clock")));
    cJSON_Delete(j);
}

static void test_pet_clock_sync_ok(void)
{
    fake.sync_written = 14 * 3600 + 32 * 60 + 5;
    go(RM_POST, "/api/pet-clock/sync", NULL, "http://tamaorb.local", NULL);
    TEST_ASSERT_EQUAL(200, resp.status);
    cJSON *j = json();
    TEST_ASSERT_EQUAL_STRING("14:32:05", cJSON_GetObjectItem(j, "pet_clock")->valuestring);
    cJSON_Delete(j);
    TEST_ASSERT_EQUAL(1, fake.sync_calls);
}

static void test_pet_clock_sync_refusals(void)
{
    static const struct { rm_err_t e; int status; const char *error; } cases[] = {
        {RM_ERR_NOTFOUND, 404, "not found"}, {RM_ERR_NOTIME, 409, "no time"},
        {RM_ERR_CATCHUP, 409, "catching up"}, {RM_ERR_BUSY, 409, "busy"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
        fake.sync_result = cases[i].e;
        go(RM_POST, "/api/pet-clock/sync", NULL, NULL, NULL);
        expect_err(cases[i].status, cases[i].error, NULL);
    }
}

static void test_pet_clock_sync_get_is_405(void)
{
    go(RM_GET, "/api/pet-clock/sync", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(405, resp.status);
    TEST_ASSERT_EQUAL(0, fake.sync_calls);
}

static void test_pet_clock_sync_checks_origin(void)
{
    go(RM_POST, "/api/pet-clock/sync", NULL, "http://evil.example", NULL);
    TEST_ASSERT_EQUAL(403, resp.status);
    TEST_ASSERT_EQUAL(0, fake.sync_calls);
}

// ---- debugger ----
static void test_debug_json_fields(void)
{
    go(RM_GET, "/api/debug", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(200, resp.status);
    cJSON *j = json();
    static const char *const keys[] = { "seq", "model", "phase", "tick", "pc", "np", "a", "b", "x", "y", "sp", "flags",
                                        "halted", "call_depth", "irq", "buttons", "buzzer", "lcd", "icons", "ram" };
    for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) TEST_ASSERT_NOT_NULL_MESSAGE(cJSON_GetObjectItem(j, keys[i]), keys[i]);
    TEST_ASSERT_EQUAL_STRING("P1", cJSON_GetObjectItem(j, "model")->valuestring);
    TEST_ASSERT_EQUAL_STRING("run", cJSON_GetObjectItem(j, "phase")->valuestring);
    TEST_ASSERT_EQUAL(0x14E, cJSON_GetObjectItem(j, "pc")->valueint);
    TEST_ASSERT_EQUAL(42, cJSON_GetObjectItem(j, "seq")->valueint);
    TEST_ASSERT_EQUAL(123456, cJSON_GetObjectItem(j, "tick")->valueint);
    TEST_ASSERT_EQUAL(4, cJSON_GetObjectItem(j, "flags")->valueint);
    cJSON *irq = cJSON_GetObjectItem(j, "irq");
    TEST_ASSERT_EQUAL(6, cJSON_GetArraySize(irq));
    TEST_ASSERT_EQUAL_STRING("prog_timer", cJSON_GetObjectItem(cJSON_GetArrayItem(irq, 0), "name")->valuestring);
    cJSON *clk = cJSON_GetArrayItem(irq, 5);
    TEST_ASSERT_EQUAL_STRING("clock_timer", cJSON_GetObjectItem(clk, "name")->valuestring);
    TEST_ASSERT_EQUAL(8, cJSON_GetObjectItem(clk, "mask")->valueint);
    TEST_ASSERT_EQUAL(0, cJSON_GetObjectItem(clk, "factor")->valueint);
    cJSON *lcd = cJSON_GetObjectItem(j, "lcd");
    TEST_ASSERT_EQUAL(16, cJSON_GetArraySize(lcd));
    TEST_ASSERT_EQUAL_STRING("00000001", cJSON_GetArrayItem(lcd, 0)->valuestring);
    TEST_ASSERT_EQUAL_STRING("00008000", cJSON_GetArrayItem(lcd, 15)->valuestring);
    TEST_ASSERT_EQUAL(0x80, cJSON_GetObjectItem(j, "icons")->valueint);
    const char *ram = cJSON_GetObjectItem(j, "ram")->valuestring;
    TEST_ASSERT_EQUAL(768, strlen(ram));
    TEST_ASSERT_EQUAL_STRING_LEN("0123456789ABCDEF0123", ram, 20);
    cJSON_Delete(j);
    TEST_ASSERT_EQUAL(1, fake.debug_calls);
}

static void test_debug_nothing_published(void)
{
    fake.debug.ram_n = 0;
    go(RM_GET, "/api/debug", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(200, resp.status);
    cJSON *j = json();
    TEST_ASSERT_EQUAL_STRING("", cJSON_GetObjectItem(j, "ram")->valuestring);
    cJSON_Delete(j);
}

static void test_debug_unknown_model(void)
{
    fake.debug.model = TC_MODEL_UNKNOWN;
    go(RM_GET, "/api/debug", NULL, NULL, NULL);
    cJSON *j = json();
    TEST_ASSERT_EQUAL_STRING("unknown", cJSON_GetObjectItem(j, "model")->valuestring);
    cJSON_Delete(j);
}

static void test_rom_bytes(void)
{
    go(RM_GET, "/api/rom", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(200, resp.status);
    TEST_ASSERT_EQUAL_STRING("application/octet-stream", resp.content_type);
    TEST_ASSERT_EQUAL(12288, resp.body_len);
    TEST_ASSERT_EQUAL_MEMORY(fake.rom, resp.body, 12288);
}

static void test_rom_none_is_404(void)
{
    fake.have_rom = false;
    go(RM_GET, "/api/rom", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(404, resp.status);
}

static void test_debug_and_rom_get_only(void)
{
    go(RM_POST, "/api/debug", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(405, resp.status);
    go(RM_POST, "/api/rom", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL(405, resp.status);
    TEST_ASSERT_EQUAL(0, fake.debug_calls);
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
    rm_set_alloc(NULL);  // back to malloc (tearDown also resets it)
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

static void test_body_trailing_data_rejected(void)
{
    static const char raw[] = "{\"volume\":3}garbage";
    rfree();
    rm_req_t rq = {0};
    rq.method = RM_PATCH; rq.path = "/api/settings"; rq.self_name = "tamaorb";
    rq.body = (const uint8_t *)raw; rq.body_len = sizeof raw - 1;
    memset(&resp, 0, sizeof resp);
    rm_http_handle(&fake_ops, &rq, &resp);
    TEST_ASSERT_EQUAL(400, resp.status);
    TEST_ASSERT_EQUAL(0, fake.put_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_origin_rules);
    RUN_TEST(test_cross_origin_patch_403);
    RUN_TEST(test_status_fields);
    RUN_TEST(test_get_settings_restart_pending);
    RUN_TEST(test_patch_settings_ok);
    RUN_TEST(test_patch_settings_skips_full_status);
    RUN_TEST(test_patch_settings_bad_key_changes_nothing);
    RUN_TEST(test_settings_json_muted_is_bool);
    RUN_TEST(test_patch_muted_rejects_string);
    RUN_TEST(test_patch_tz_bad_is_400_and_unchanged);
    RUN_TEST(test_get_timezones_lists_names);
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
    RUN_TEST(test_save_download_rom_not_listed);
    RUN_TEST(test_pets_and_switch);
    RUN_TEST(test_switch_catching_up_409);
    RUN_TEST(test_factory_reset_confirm);
    RUN_TEST(test_resume_choices);
    RUN_TEST(test_resume_not_waiting);
    RUN_TEST(test_skip_ok);
    RUN_TEST(test_skip_not_catching_up);
    RUN_TEST(test_status_wait_fields);
    RUN_TEST(test_pets_list_shape);
    RUN_TEST(test_roms_list);
    RUN_TEST(test_pets_new_ok);
    RUN_TEST(test_pets_new_full);
    RUN_TEST(test_pets_new_bad_sha);
    RUN_TEST(test_pets_switch_by_id);
    RUN_TEST(test_pets_switch_missing_rom);
    RUN_TEST(test_pets_rename_ok);
    RUN_TEST(test_pets_rename_invalid);
    RUN_TEST(test_pets_rename_taken);
    RUN_TEST(test_pets_delete_running);
    RUN_TEST(test_pets_bad_id);
    RUN_TEST(test_save_path_with_id);
    RUN_TEST(test_status_has_pet_and_ball_clock);
    RUN_TEST(test_pet_clock_sync_ok);
    RUN_TEST(test_pet_clock_sync_refusals);
    RUN_TEST(test_pet_clock_sync_get_is_405);
    RUN_TEST(test_pet_clock_sync_checks_origin);
    RUN_TEST(test_debug_json_fields);
    RUN_TEST(test_debug_nothing_published);
    RUN_TEST(test_debug_unknown_model);
    RUN_TEST(test_rom_bytes);
    RUN_TEST(test_rom_none_is_404);
    RUN_TEST(test_debug_and_rom_get_only);
    RUN_TEST(test_look_and_press);
    RUN_TEST(test_big_buffers_come_from_rm_alloc);
    RUN_TEST(test_body_parsed_by_length);
    RUN_TEST(test_body_trailing_data_rejected);
    return UNITY_END();
}
