// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "fake_ops.h"

#define INSTR "Read the guide resource for the active model (tamaorb://guide/p1, p2, angel or mothra) before acting."

void setUp(void) { fake_reset(); }
void tearDown(void) { rm_set_alloc(NULL); }

static cJSON *call(const char *body, int *status)
{
    char *out = NULL;
    int st = rm_mcp_handle(&fake_ops, body, body ? strlen(body) : 0, &out);
    if (status) *status = st;
    if (!out) return NULL;
    cJSON *j = cJSON_Parse(out);
    free(out);
    TEST_ASSERT_NOT_NULL(j);
    return j;
}
static int err_code(cJSON *r) { return (int)cJSON_GetObjectItem(cJSON_GetObjectItem(r, "error"), "code")->valuedouble; }
static cJSON *tool(const char *name, const char *args_json)
{
    char buf[512];
    snprintf(buf, sizeof buf, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"%s\",\"arguments\":%s}}", name, args_json);
    return call(buf, NULL);
}
static cJSON *result(cJSON *r) { return cJSON_GetObjectItem(r, "result"); }
static bool is_error(cJSON *r) { cJSON *e = cJSON_GetObjectItem(result(r), "isError"); return cJSON_IsTrue(e); }
static const char *text0(cJSON *r)
{
    cJSON *c = cJSON_GetArrayItem(cJSON_GetObjectItem(result(r), "content"), 0);
    return cJSON_GetObjectItem(c, "text")->valuestring;
}

static void test_initialize_echoes_known_version(void)
{
    cJSON *r = call("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\"}}", NULL);
    TEST_ASSERT_EQUAL_STRING("2025-06-18", cJSON_GetObjectItem(result(r), "protocolVersion")->valuestring);
    TEST_ASSERT_EQUAL_STRING("tamaorb", cJSON_GetObjectItem(cJSON_GetObjectItem(result(r), "serverInfo"), "name")->valuestring);
    TEST_ASSERT_EQUAL_STRING("test", cJSON_GetObjectItem(cJSON_GetObjectItem(result(r), "serverInfo"), "version")->valuestring);
    TEST_ASSERT_EQUAL_STRING(INSTR, cJSON_GetObjectItem(result(r), "instructions")->valuestring);
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(cJSON_GetObjectItem(result(r), "capabilities"), "tools"));
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(cJSON_GetObjectItem(result(r), "capabilities"), "resources"));
    TEST_ASSERT_EQUAL_STRING("2.0", cJSON_GetObjectItem(r, "jsonrpc")->valuestring);
    cJSON_Delete(r);
}

static void test_initialize_unknown_version(void)
{
    cJSON *r = call("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"1999-01-01\"}}", NULL);
    TEST_ASSERT_EQUAL_STRING("2025-11-25", cJSON_GetObjectItem(result(r), "protocolVersion")->valuestring);
    cJSON_Delete(r);
    r = call("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\"}", NULL);
    TEST_ASSERT_EQUAL_STRING("2025-11-25", cJSON_GetObjectItem(result(r), "protocolVersion")->valuestring);
    cJSON_Delete(r);
}

static void test_notification_202(void)
{
    char *out = (char *)1;
    const char *b = "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}";
    TEST_ASSERT_EQUAL(202, rm_mcp_handle(&fake_ops, b, strlen(b), &out));
    TEST_ASSERT_NULL(out);
}

static void test_parse_error(void)
{
    int st;
    cJSON *r = call("{", &st);
    TEST_ASSERT_EQUAL(200, st);
    TEST_ASSERT_EQUAL(-32700, err_code(r));
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(r, "id")));
    cJSON_Delete(r);
    r = call(NULL, &st);
    TEST_ASSERT_EQUAL(-32700, err_code(r));
    cJSON_Delete(r);
    r = call("", &st);
    TEST_ASSERT_EQUAL(-32700, err_code(r));
    cJSON_Delete(r);
    // Anything but whitespace after the request is a parse error, not ignored.
    r = call("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}x", &st);
    TEST_ASSERT_EQUAL(-32700, err_code(r));
    cJSON_Delete(r);
    r = call("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}{}", &st);
    TEST_ASSERT_EQUAL(-32700, err_code(r));
    cJSON_Delete(r);
    r = call("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"} \r\n", &st);
    TEST_ASSERT_NULL(cJSON_GetObjectItem(r, "error"));
    cJSON_Delete(r);
}

static void test_batch_rejected(void)
{
    cJSON *r = call("[{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}]", NULL);
    TEST_ASSERT_EQUAL(-32600, err_code(r));
    cJSON_Delete(r);
    r = call("{\"jsonrpc\":\"1.0\",\"id\":1,\"method\":\"ping\"}", NULL);
    TEST_ASSERT_EQUAL(-32600, err_code(r));
    cJSON_Delete(r);
    r = call("5", NULL);
    TEST_ASSERT_EQUAL(-32600, err_code(r));
    cJSON_Delete(r);
}

static void test_unknown_method(void)
{
    cJSON *r = call("{\"jsonrpc\":\"2.0\",\"id\":\"x\",\"method\":\"nope\"}", NULL);
    TEST_ASSERT_EQUAL(-32601, err_code(r));
    TEST_ASSERT_EQUAL_STRING("x", cJSON_GetObjectItem(r, "id")->valuestring);
    cJSON_Delete(r);
    r = call("{\"jsonrpc\":\"2.0\",\"id\":42,\"method\":\"ping\"}", NULL);
    TEST_ASSERT_EQUAL(42, (int)cJSON_GetObjectItem(r, "id")->valuedouble);
    TEST_ASSERT_NOT_NULL(result(r));
    cJSON_Delete(r);
}

static void test_tools_list_names(void)
{
    static const char *const want[] = {"look", "press", "status", "get_settings", "set_settings", "list_pets", "switch_pet",
                                       "sync_pet_clock", "read_ram"};
    cJSON *r = call("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}", NULL);
    cJSON *tools = cJSON_GetObjectItem(result(r), "tools");
    TEST_ASSERT_EQUAL(9, cJSON_GetArraySize(tools));
    for (int i = 0; i < 9; i++) {
        cJSON *t = cJSON_GetArrayItem(tools, i);
        TEST_ASSERT_EQUAL_STRING(want[i], cJSON_GetObjectItem(t, "name")->valuestring);
        TEST_ASSERT_EQUAL_STRING("object", cJSON_GetObjectItem(cJSON_GetObjectItem(t, "inputSchema"), "type")->valuestring);
        TEST_ASSERT_TRUE(cJSON_IsString(cJSON_GetObjectItem(t, "description")));
    }
    cJSON_Delete(r);
}

static void test_look_has_text_and_png(void)
{
    cJSON *r = tool("look", "{}");
    TEST_ASSERT_FALSE(is_error(r));
    TEST_ASSERT_EQUAL_INT(0, strncmp(text0(r), "#.......", 8));
    cJSON *img = cJSON_GetArrayItem(cJSON_GetObjectItem(result(r), "content"), 1);
    TEST_ASSERT_EQUAL_STRING("image", cJSON_GetObjectItem(img, "type")->valuestring);
    TEST_ASSERT_EQUAL_STRING("image/png", cJSON_GetObjectItem(img, "mimeType")->valuestring);
    // "iVBORw0KGgo" is base64 of the PNG signature 89 50 4E 47 0D 0A 1A 0A.
    TEST_ASSERT_EQUAL_INT(0, strncmp(cJSON_GetObjectItem(img, "data")->valuestring, "iVBORw0KGgo", 11));
    cJSON_Delete(r);
}

static void test_base64(void)
{
    char o[16];
    TEST_ASSERT_EQUAL(0, rm_base64_encode((const uint8_t *)"", 0, o));
    TEST_ASSERT_EQUAL(4, rm_base64_encode((const uint8_t *)"f", 1, o));
    TEST_ASSERT_EQUAL_STRING("Zg==", o);
    rm_base64_encode((const uint8_t *)"fo", 2, o);
    TEST_ASSERT_EQUAL_STRING("Zm8=", o);
    rm_base64_encode((const uint8_t *)"foo", 3, o);
    TEST_ASSERT_EQUAL_STRING("Zm9v", o);
    rm_base64_encode((const uint8_t *)"foobar", 6, o);
    TEST_ASSERT_EQUAL_STRING("Zm9vYmFy", o);
}

static void test_press_passes_request(void)
{
    cJSON *r = tool("press", "{\"buttons\":[\"A\",\"AC\"],\"hold_ms\":100}");
    TEST_ASSERT_FALSE(is_error(r));
    TEST_ASSERT_EQUAL(1, fake.press_calls);
    TEST_ASSERT_EQUAL(2, fake.press_seen.n);
    TEST_ASSERT_EQUAL(RM_STEP_A, fake.press_seen.steps[0]);
    TEST_ASSERT_EQUAL(RM_STEP_AC, fake.press_seen.steps[1]);
    TEST_ASSERT_EQUAL(100, fake.press_seen.hold_ms);
    TEST_ASSERT_EQUAL(400, fake.press_seen.gap_ms);
    TEST_ASSERT_EQUAL(2, cJSON_GetArraySize(cJSON_GetObjectItem(result(r), "content")));
    cJSON_Delete(r);
    r = tool("press", "{\"buttons\":\"B C\"}");
    TEST_ASSERT_FALSE(is_error(r));
    TEST_ASSERT_EQUAL(2, fake.press_seen.n);
    cJSON_Delete(r);
}

static void test_press_busy_is_tool_error(void)
{
    fake.press_result = RM_ERR_BUSY;
    cJSON *r = tool("press", "{\"buttons\":\"A\"}");
    TEST_ASSERT_TRUE(is_error(r));
    TEST_ASSERT_NOT_NULL(strstr(text0(r), "busy"));
    cJSON_Delete(r);
    fake.press_result = RM_ERR_CATCHUP;
    r = tool("press", "{\"buttons\":\"A\"}");
    TEST_ASSERT_TRUE(is_error(r));
    TEST_ASSERT_NOT_NULL(strstr(text0(r), "catching up"));
    cJSON_Delete(r);
}

static void test_tool_args_wrong_type(void)
{
    cJSON *r = tool("press", "{\"buttons\":5}");
    TEST_ASSERT_TRUE(is_error(r));
    TEST_ASSERT_NOT_NULL(strstr(text0(r), "buttons"));
    TEST_ASSERT_EQUAL(0, fake.press_calls);
    cJSON_Delete(r);
}

static void test_press_validated_before_ops(void)
{
    cJSON *r = tool("press", "{\"buttons\":\"A\",\"hold_ms\":5000}");
    TEST_ASSERT_TRUE(is_error(r));
    TEST_ASSERT_NOT_NULL(strstr(text0(r), "hold_ms"));
    cJSON_Delete(r);
    r = tool("press", "{\"buttons\":\"A\",\"gap_ms\":1.5}");
    TEST_ASSERT_TRUE(is_error(r));
    TEST_ASSERT_NOT_NULL(strstr(text0(r), "gap_ms"));
    cJSON_Delete(r);
    r = tool("press", "{}");
    TEST_ASSERT_TRUE(is_error(r));
    TEST_ASSERT_EQUAL(0, fake.press_calls);
    cJSON_Delete(r);
}

static void test_unknown_tool(void)
{
    cJSON *r = tool("nope", "{}");
    TEST_ASSERT_TRUE(is_error(r));
    cJSON_Delete(r);
}

static void test_set_settings_bad_key_changes_nothing(void)
{
    cJSON *r = tool("set_settings", "{\"brightness\":50,\"dim_level\":500}");
    TEST_ASSERT_TRUE(is_error(r));
    TEST_ASSERT_NOT_NULL(strstr(text0(r), "dim_level"));
    TEST_ASSERT_EQUAL(80, fake.settings.brightness);
    TEST_ASSERT_EQUAL(0, fake.put_calls);
    cJSON_Delete(r);
}

static void test_set_settings_schema_has_muted_and_tz(void)
{
    cJSON *r = call("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}", NULL);
    cJSON *tools = cJSON_GetObjectItem(result(r), "tools");
    cJSON *props = NULL;
    for (cJSON *t = tools->child; t; t = t->next)
        if (!strcmp(cJSON_GetObjectItem(t, "name")->valuestring, "set_settings"))
            props = cJSON_GetObjectItem(cJSON_GetObjectItem(t, "inputSchema"), "properties");
    TEST_ASSERT_NOT_NULL(props);
    TEST_ASSERT_EQUAL_STRING("boolean", cJSON_GetObjectItem(cJSON_GetObjectItem(props, "muted"), "type")->valuestring);
    cJSON *tz = cJSON_GetObjectItem(props, "tz");
    TEST_ASSERT_EQUAL_STRING("string", cJSON_GetObjectItem(tz, "type")->valuestring);
    TEST_ASSERT_NOT_NULL(strstr(cJSON_GetObjectItem(tz, "description")->valuestring, "IANA"));
    cJSON_Delete(r);
    r = tool("set_settings", "{\"muted\":true,\"tz\":\"Europe/Berlin\"}");
    TEST_ASSERT_FALSE(is_error(r));
    TEST_ASSERT_TRUE(fake.settings.muted);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(cJSON_Parse(text0(r)), "muted")));
    cJSON_Delete(r);
}

static void test_set_settings_ok(void)
{
    cJSON *r = tool("set_settings", "{\"brightness\":50,\"palette\":\"night\"}");
    TEST_ASSERT_FALSE(is_error(r));
    TEST_ASSERT_EQUAL(1, fake.put_calls);
    TEST_ASSERT_TRUE(fake.put_display);
    TEST_ASSERT_EQUAL(50, fake.settings.brightness);
    TEST_ASSERT_EQUAL(ST_PALETTE_NIGHT, fake.settings.palette);
    cJSON *s = cJSON_Parse(text0(r));
    TEST_ASSERT_EQUAL(50, (int)cJSON_GetObjectItem(s, "brightness")->valuedouble);
    cJSON_Delete(s);
    cJSON_Delete(r);
    r = tool("set_settings", "{\"volume\":10}");
    TEST_ASSERT_FALSE(fake.put_display);
    cJSON_Delete(r);
    r = tool("set_settings", "{\"brightness\":50}");  // unchanged
    TEST_ASSERT_FALSE(fake.put_display);
    cJSON_Delete(r);
}

static void test_patch_settings_types(void)
{
    st_settings_t s;
    st_defaults(&s, 50);
    bool d = false;
    cJSON *p = cJSON_Parse("{\"brightness\":1.5}");
    TEST_ASSERT_EQUAL_STRING("brightness", rm_json_patch_settings(p, &s, &d));
    cJSON_Delete(p);
    p = cJSON_Parse("{\"brightness\":\"60\"}");  // string for a numeric key is passed as-is and accepted
    TEST_ASSERT_NULL(rm_json_patch_settings(p, &s, &d));
    TEST_ASSERT_EQUAL(60, s.brightness);
    cJSON_Delete(p);
    p = cJSON_Parse("{\"name\":true}");
    TEST_ASSERT_EQUAL_STRING("name", rm_json_patch_settings(p, &s, &d));
    cJSON_Delete(p);
    p = cJSON_Parse("{\"bogus\":1}");
    TEST_ASSERT_EQUAL_STRING("bogus", rm_json_patch_settings(p, &s, &d));
    cJSON_Delete(p);
    p = cJSON_Parse("[1]");
    TEST_ASSERT_EQUAL_STRING("", rm_json_patch_settings(p, &s, &d));
    cJSON_Delete(p);
    TEST_ASSERT_EQUAL_STRING("", rm_json_patch_settings(NULL, &s, &d));
}

static void test_status_and_pets_json(void)
{
    fake.status.last_save_known = false;
    cJSON *r = tool("status", "{}");
    cJSON *s = cJSON_Parse(text0(r));
    TEST_ASSERT_EQUAL_STRING("P1", cJSON_GetObjectItem(s, "model")->valuestring);
    TEST_ASSERT_EQUAL_STRING("run", cJSON_GetObjectItem(s, "phase")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(s, "last_save_unix")));
    TEST_ASSERT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(s, "usb")));
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(s, "catchup_speed"));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(s, "has_clock")));
    cJSON_Delete(s);
    cJSON_Delete(r);
    r = tool("list_pets", "{}");
    cJSON *a = cJSON_Parse(text0(r));
    TEST_ASSERT_EQUAL(2, cJSON_GetArraySize(a));
    TEST_ASSERT_EQUAL_STRING("a0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3", cJSON_GetObjectItem(cJSON_GetArrayItem(a, 0), "sha1")->valuestring);
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(cJSON_GetArrayItem(a, 0), "save_unix")));
    TEST_ASSERT_EQUAL_STRING("P2", cJSON_GetObjectItem(cJSON_GetArrayItem(a, 1), "model")->valuestring);
    TEST_ASSERT_EQUAL(1700000000, (long)cJSON_GetObjectItem(cJSON_GetArrayItem(a, 1), "save_unix")->valuedouble);
    cJSON_Delete(a);
    cJSON_Delete(r);
}

static void test_switch_pet(void)
{
    cJSON *r = tool("switch_pet", "{\"name\":\"other.bin\"}");
    TEST_ASSERT_FALSE(is_error(r));
    TEST_ASSERT_EQUAL_STRING("other.bin", fake.switched_to);
    cJSON_Delete(r);
    fake.switch_result = RM_ERR_NOTFOUND;
    r = tool("switch_pet", "{\"name\":\"zzz\"}");
    TEST_ASSERT_TRUE(is_error(r));
    cJSON_Delete(r);
    r = tool("switch_pet", "{}");
    TEST_ASSERT_TRUE(is_error(r));
    TEST_ASSERT_NOT_NULL(strstr(text0(r), "name"));
    cJSON_Delete(r);
}

static void test_switch_pet_during_catchup(void)
{
    fake.switch_result = RM_ERR_CATCHUP;
    cJSON *r = tool("switch_pet", "{\"name\":\"other.bin\"}");
    TEST_ASSERT_TRUE(is_error(r));
    TEST_ASSERT_EQUAL_STRING("the pet is catching up; try again when it finishes", text0(r));
    cJSON_Delete(r);
}

static void test_sync_pet_clock_description_warns(void)
{
    cJSON *r = call("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}", NULL);
    cJSON *tools = cJSON_GetObjectItem(result(r), "tools");
    cJSON *t = cJSON_GetArrayItem(tools, 7);
    TEST_ASSERT_EQUAL_STRING("sync_pet_clock", cJSON_GetObjectItem(t, "name")->valuestring);
    TEST_ASSERT_NOT_NULL(strstr(cJSON_GetObjectItem(t, "description")->valuestring, "age may change"));
    cJSON_Delete(r);
}

static void test_sync_pet_clock_ok(void)
{
    fake.sync_written = 14 * 3600 + 32 * 60 + 5;
    cJSON *r = tool("sync_pet_clock", "{}");
    TEST_ASSERT_FALSE(is_error(r));
    TEST_ASSERT_NOT_NULL(strstr(text0(r), "14:32:05"));
    TEST_ASSERT_EQUAL(1, fake.sync_calls);
    cJSON_Delete(r);
}

static void test_sync_pet_clock_refusals_are_tool_errors(void)
{
    fake.sync_result = RM_ERR_BUSY;
    cJSON *r = tool("sync_pet_clock", "{}");
    TEST_ASSERT_TRUE(is_error(r));
    TEST_ASSERT_NOT_NULL(strstr(text0(r), "clock menu"));
    cJSON_Delete(r);
    fake.sync_result = RM_ERR_NOTIME;
    r = tool("sync_pet_clock", "{}");
    TEST_ASSERT_TRUE(is_error(r));
    TEST_ASSERT_NOT_NULL(strstr(text0(r), "no internet time"));
    cJSON_Delete(r);
    fake.sync_result = RM_ERR_NOTFOUND;
    r = tool("sync_pet_clock", "{}");
    TEST_ASSERT_TRUE(is_error(r));
    TEST_ASSERT_NOT_NULL(strstr(text0(r), "P1 and P2"));
    cJSON_Delete(r);
}

static void test_sync_pet_clock_out_of_range_is_busy_like_usb(void)
{
    fake.sync_written = 90000;  // can't happen; reported as USB does (busy), never as 00:00:00
    cJSON *r = tool("sync_pet_clock", "{}");
    TEST_ASSERT_TRUE(is_error(r));
    TEST_ASSERT_NOT_NULL(strstr(text0(r), "clock menu"));
    cJSON_Delete(r);
}

static void test_status_description_mentions_pet_clock(void)
{
    cJSON *r = call("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}", NULL);
    cJSON *t = cJSON_GetArrayItem(cJSON_GetObjectItem(result(r), "tools"), 2);
    TEST_ASSERT_EQUAL_STRING("status", cJSON_GetObjectItem(t, "name")->valuestring);
    TEST_ASSERT_NOT_NULL(strstr(cJSON_GetObjectItem(t, "description")->valuestring, "pet's clock"));
    cJSON_Delete(r);
}

static void test_read_ram_description_says_read_only(void)
{
    cJSON *r = call("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}", NULL);
    cJSON *t = cJSON_GetArrayItem(cJSON_GetObjectItem(result(r), "tools"), 8);
    TEST_ASSERT_EQUAL_STRING("read_ram", cJSON_GetObjectItem(t, "name")->valuestring);
    TEST_ASSERT_NOT_NULL(strstr(cJSON_GetObjectItem(t, "description")->valuestring, "Read only"));
    cJSON_Delete(r);
}

static void test_read_ram_whole(void)
{
    cJSON *r = tool("read_ram", "{}");
    TEST_ASSERT_FALSE(is_error(r));
    const char *t = text0(r);
    TEST_ASSERT_NOT_NULL(strstr(t, "0x000: 0123456789ABCDEF0123456789ABCDEF\n"));
    TEST_ASSERT_NOT_NULL(strstr(t, "0x2E0: "));
    TEST_ASSERT_NOT_NULL(strstr(t, "pc=0x14E"));
    TEST_ASSERT_NOT_NULL(strstr(t, "flags=D"));
    TEST_ASSERT_NOT_NULL(strstr(t, "model=P1"));
    cJSON_Delete(r);
}

static void test_read_ram_range(void)
{
    cJSON *r = tool("read_ram", "{\"from\":16,\"count\":6}");
    TEST_ASSERT_FALSE(is_error(r));
    const char *t = text0(r);
    TEST_ASSERT_NOT_NULL(strstr(t, "0x010: 012345\n"));
    TEST_ASSERT_NULL(strstr(t, "0x000:"));
    TEST_ASSERT_NULL(strstr(t, "0x020:"));
    cJSON_Delete(r);
}

static void test_read_ram_bad_range(void)
{
    static const char *const bad[][2] = { { "{\"from\":768}", "from" }, { "{\"count\":0}", "count" },
                                           { "{\"from\":-1}", "from" }, { "{\"from\":\"x\"}", "from" },
                                           { "{\"from\":1e12}", "from" }, { "{\"count\":-1e12}", "count" },
                                           { "{\"from\":2.5}", "from" } };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        cJSON *r = tool("read_ram", bad[i][0]);
        TEST_ASSERT_TRUE_MESSAGE(is_error(r), bad[i][0]);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(text0(r), bad[i][1]), bad[i][0]);
        cJSON_Delete(r);
    }
}

static void test_resources(void)
{
    cJSON *r = call("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"resources/list\"}", NULL);
    cJSON *l = cJSON_GetObjectItem(result(r), "resources");
    TEST_ASSERT_EQUAL(4, cJSON_GetArraySize(l));
    TEST_ASSERT_EQUAL_STRING("tamaorb://guide/p1", cJSON_GetObjectItem(cJSON_GetArrayItem(l, 0), "uri")->valuestring);
    TEST_ASSERT_EQUAL_STRING("tamaorb://guide/p2", cJSON_GetObjectItem(cJSON_GetArrayItem(l, 1), "uri")->valuestring);
    TEST_ASSERT_EQUAL_STRING("tamaorb://guide/angel", cJSON_GetObjectItem(cJSON_GetArrayItem(l, 2), "uri")->valuestring);
    TEST_ASSERT_EQUAL_STRING("tamaorb://guide/mothra", cJSON_GetObjectItem(cJSON_GetArrayItem(l, 3), "uri")->valuestring);
    TEST_ASSERT_EQUAL_STRING("text/markdown", cJSON_GetObjectItem(cJSON_GetArrayItem(l, 1), "mimeType")->valuestring);
    cJSON_Delete(r);
}

static void test_resources_read_guide(void)
{
    cJSON *r = call("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"resources/read\",\"params\":{\"uri\":\"tamaorb://guide/p1\"}}", NULL);
    cJSON *c = cJSON_GetArrayItem(cJSON_GetObjectItem(result(r), "contents"), 0);
    TEST_ASSERT_EQUAL_STRING("# P1 guide", cJSON_GetObjectItem(c, "text")->valuestring);
    TEST_ASSERT_EQUAL_STRING("tamaorb://guide/p1", cJSON_GetObjectItem(c, "uri")->valuestring);
    cJSON_Delete(r);
    r = call("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"resources/read\",\"params\":{\"uri\":\"tamaorb://guide/x\"}}", NULL);
    TEST_ASSERT_EQUAL(-32602, err_code(r));
    cJSON_Delete(r);
    r = call("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"resources/read\",\"params\":{\"uri\":\"tamaorb://guide/p2\"}}", NULL);
    TEST_ASSERT_EQUAL(-32602, err_code(r));  // p2 has no guide in the fake
    cJSON_Delete(r);
    r = call("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"resources/read\",\"params\":{\"uri\":\"tamaorb://guide/angel\"}}", NULL);
    c = cJSON_GetArrayItem(cJSON_GetObjectItem(result(r), "contents"), 0);
    TEST_ASSERT_EQUAL_STRING("# angel guide", cJSON_GetObjectItem(c, "text")->valuestring);
    cJSON_Delete(r);
    r = call("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"resources/read\"}", NULL);
    TEST_ASSERT_EQUAL(-32602, err_code(r));
    cJSON_Delete(r);
}

static void test_hostile_input(void)
{
    const char *bad[] = {"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":5}", "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\"}",
                         "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":[]}",
                         "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":7}}",
                         "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"press\",\"arguments\":5}}",
                         "{\"jsonrpc\":\"2.0\",\"id\":{},\"method\":\"ping\"}", "null", "\"x\""};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        char *out = NULL;
        int st = rm_mcp_handle(&fake_ops, bad[i], strlen(bad[i]), &out);
        TEST_ASSERT_TRUE(st == 200 || st == 202);
        if (st == 200) {
            TEST_ASSERT_NOT_NULL(out);
            TEST_ASSERT_NOT_NULL(strstr(out, "\"jsonrpc\":\"2.0\""));
        }
        free(out);
    }
}

static void test_depth_limit(void)
{
    char *deep = malloc(2001);
    memset(deep, '[', 2000);
    deep[2000] = 0;
    cJSON *r = call(deep, NULL);
    TEST_ASSERT_EQUAL(-32600, err_code(r));
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(r, "id")));
    cJSON_Delete(r);
    free(deep);

    // Exactly depth 16: the outer object plus 15 arrays.
    char ok[256] = "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"ping\",\"params\":";
    strcat(ok, "[[[[[[[[[[[[[[[");
    strcat(ok, "]]]]]]]]]]]]]]]}");
    r = call(ok, NULL);
    TEST_ASSERT_NOT_NULL(result(r));
    cJSON_Delete(r);
    // One more level is refused.
    char over[256] = "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"ping\",\"params\":";
    strcat(over, "[[[[[[[[[[[[[[[[");
    strcat(over, "]]]]]]]]]]]]]]]]}");
    r = call(over, NULL);
    TEST_ASSERT_EQUAL(-32600, err_code(r));
    cJSON_Delete(r);

    TEST_ASSERT_TRUE(rm_json_depth_ok("\"[[[[[[[[[[[[[[[[[[[[\"", 22, 2));
    const char *esc = "{\"a\":\"\\\"[[[[[[[[[[[[[[[[[[[[\"}";
    TEST_ASSERT_TRUE(rm_json_depth_ok(esc, strlen(esc), 2));
    TEST_ASSERT_FALSE(rm_json_depth_ok("[[[", 3, 2));
}

static void *failing_alloc(size_t n) { (void)n; return NULL; }

static void test_tools_out_of_memory(void)
{
    rm_set_alloc(failing_alloc);  // only remote's big buffers fail; cJSON still allocates
    cJSON *r = tool("look", "{}");
    TEST_ASSERT_TRUE(is_error(r));
    TEST_ASSERT_EQUAL_STRING("could not render the screen", text0(r));
    cJSON_Delete(r);
    r = tool("list_pets", "{}");
    TEST_ASSERT_TRUE(is_error(r));
    TEST_ASSERT_EQUAL_STRING("out of memory", text0(r));
    cJSON_Delete(r);
    r = tool("press", "{\"buttons\":\"A\"}");  // the press runs; only the look after it fails
    TEST_ASSERT_TRUE(is_error(r));
    TEST_ASSERT_EQUAL(1, fake.press_calls);
    cJSON_Delete(r);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_initialize_echoes_known_version);
    RUN_TEST(test_initialize_unknown_version);
    RUN_TEST(test_notification_202);
    RUN_TEST(test_parse_error);
    RUN_TEST(test_batch_rejected);
    RUN_TEST(test_unknown_method);
    RUN_TEST(test_tools_list_names);
    RUN_TEST(test_look_has_text_and_png);
    RUN_TEST(test_base64);
    RUN_TEST(test_press_passes_request);
    RUN_TEST(test_press_busy_is_tool_error);
    RUN_TEST(test_tool_args_wrong_type);
    RUN_TEST(test_press_validated_before_ops);
    RUN_TEST(test_unknown_tool);
    RUN_TEST(test_set_settings_bad_key_changes_nothing);
    RUN_TEST(test_set_settings_schema_has_muted_and_tz);
    RUN_TEST(test_set_settings_ok);
    RUN_TEST(test_patch_settings_types);
    RUN_TEST(test_status_and_pets_json);
    RUN_TEST(test_switch_pet);
    RUN_TEST(test_switch_pet_during_catchup);
    RUN_TEST(test_sync_pet_clock_description_warns);
    RUN_TEST(test_sync_pet_clock_ok);
    RUN_TEST(test_sync_pet_clock_refusals_are_tool_errors);
    RUN_TEST(test_sync_pet_clock_out_of_range_is_busy_like_usb);
    RUN_TEST(test_status_description_mentions_pet_clock);
    RUN_TEST(test_read_ram_description_says_read_only);
    RUN_TEST(test_read_ram_whole);
    RUN_TEST(test_read_ram_range);
    RUN_TEST(test_read_ram_bad_range);
    RUN_TEST(test_resources);
    RUN_TEST(test_resources_read_guide);
    RUN_TEST(test_hostile_input);
    RUN_TEST(test_depth_limit);
    RUN_TEST(test_tools_out_of_memory);
    return UNITY_END();
}
