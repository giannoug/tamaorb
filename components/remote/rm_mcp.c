// SPDX-License-Identifier: GPL-3.0-or-later
// MCP (Streamable HTTP, JSON-RPC 2.0) dispatcher over an rm_ops_t. Portable. Spec §8.1.
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "remote.h"

#define LATEST_VERSION "2025-11-25"
static const char *const VERSIONS[] = {"2025-11-25", "2025-06-18", "2025-03-26"};
#define INSTRUCTIONS "Read the guide resource for the active model (tamaorb://guide/p1, p2, angel or mothra) before acting."

#define BUTTONS_DESC "Button steps in order: A, B, C or AC (A and C together), as an array or a space-separated string."

typedef struct {
    const char *name, *description, *schema;
} tool_def_t;

static const tool_def_t TOOLS[] = {
    {"look",
     "Returns the LCD as text (# lit, . unlit) plus a PNG, the lit icons by name, the buzzer state, the phase (boot, run or catch-up) "
     "and a frame counter that only tells you whether the screen changed.",
     "{\"type\":\"object\",\"properties\":{}}"},
    {"press",
     "Presses buttons in sequence, then returns what look returns. At most 32 steps; the whole sequence must take at most 10 s (about 16 steps at the default timing). "
     "The game reads about one press per 500 ms: keep hold_ms + gap_ms at 500 or more, or presses get lost. "
     "Fails if another sequence is running or the ball is catching up.",
     "{\"type\":\"object\",\"properties\":{"
     "\"buttons\":{\"description\":\"" BUTTONS_DESC "\",\"oneOf\":[{\"type\":\"string\"},{\"type\":\"array\",\"items\":{\"type\":\"string\",\"enum\":[\"A\",\"B\",\"C\",\"AC\"]}}]},"
     "\"hold_ms\":{\"type\":\"integer\",\"minimum\":50,\"maximum\":2000,\"default\":200,\"description\":\"How long each step is held.\"},"
     "\"gap_ms\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":2000,\"default\":400,\"description\":\"Pause between steps.\"},"
     "\"settle_ms\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":3000,\"default\":500,\"description\":\"Wait after the last step before looking.\"}},"
     "\"required\":[\"buttons\"]}"},
    {"status", "Returns the ball's status: ROM, model, phase, Wi-Fi, battery, memory, time, last save, and the pet's clock "
               "next to the ball's local time (pet_clock, ball_clock: HH:MM:SS, null when unknown).",
     "{\"type\":\"object\",\"properties\":{}}"},
    {"get_settings", "Returns the ball's settings.", "{\"type\":\"object\",\"properties\":{}}"},
    {"set_settings",
     "Changes settings; pass only the keys to change. All or nothing: one bad value changes nothing. name and ntp apply after a restart.",
     "{\"type\":\"object\",\"properties\":{"
     "\"brightness\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":100},"
     "\"dim_level\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100},"
     "\"dim_usb_s\":{\"type\":\"integer\",\"description\":\"0 (never) or 5-3600\"},"
     "\"dim_battery_s\":{\"type\":\"integer\",\"description\":\"0 (never) or 5-3600\"},"
     "\"off_battery_s\":{\"type\":\"integer\",\"minimum\":10,\"maximum\":3600},"
     "\"volume\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100},"
     "\"muted\":{\"type\":\"boolean\",\"description\":\"Silences the speaker; the volume is kept and comes back on unmute\"},"
     "\"tz\":{\"type\":\"string\",\"description\":\"IANA name such as Europe/Berlin, or a POSIX TZ string\"},"
     "\"palette\":{\"type\":\"string\",\"enum\":[\"classic\",\"neutral\",\"night\"]},"
     "\"name\":{\"type\":\"string\",\"description\":\"1-32 of a-z 0-9 -, not starting or ending with -\"},"
     "\"ntp\":{\"type\":\"string\",\"description\":\"NTP host, 1-63 of A-Z a-z 0-9 . -\"},"
     "\"agent_feedback\":{\"type\":\"string\",\"enum\":[\"quiet\",\"silent\",\"visible\"]}},"
     "\"additionalProperties\":false}"},
    {"list_pets", "Lists the ROMs (pets) on the SD card with the active one, and when each was last saved.",
     "{\"type\":\"object\",\"properties\":{}}"},
    {"switch_pet",
     "Switches to another pet. Saves the current pet, restarts the ball, and the new pet catches up from its own last save.",
     "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\",\"description\":\"A pet name from list_pets.\"}},\"required\":[\"name\"]}"},
    {"sync_pet_clock",
     "Sets the pet's clock to the ball's local time (the tz setting). The pet may wake up or fall asleep, and its age may "
     "change: tell the owner. Fails for ROMs other than P1/P2, without internet time, while catching up, or while the "
     "pet's own clock menu is open.",
     "{\"type\":\"object\",\"properties\":{}}"},
    {"read_ram",
     "Read only: the emulator's RAM (one hex digit per 4-bit nibble, rows of 32 with their address), then the CPU "
     "registers, flags, interrupts, model and phase, all from one instant. Known pet addresses (hunger 0x40, clock "
     "0x10-0x15, ...) are in docs/pet-ram.md. Nothing is changed.",
     "{\"type\":\"object\",\"properties\":{"
     "\"from\":{\"type\":\"integer\",\"minimum\":0,\"description\":\"First nibble address (default 0).\"},"
     "\"count\":{\"type\":\"integer\",\"minimum\":1,\"description\":\"Nibbles to read (default: to the end).\"}}}"},
};
#define N_TOOLS ((int)(sizeof TOOLS / sizeof TOOLS[0]))

static cJSON *id_copy(const cJSON *id) { return id ? cJSON_Duplicate(id, true) : cJSON_CreateNull(); }

static cJSON *envelope(const cJSON *id)
{
    cJSON *r = cJSON_CreateObject();
    if (!r) return NULL;
    cJSON_AddStringToObject(r, "jsonrpc", "2.0");
    cJSON_AddItemToObject(r, "id", id_copy(id));
    return r;
}

static cJSON *error_resp(const cJSON *id, int code, const char *msg)
{
    cJSON *r = envelope(id);
    if (!r) return NULL;
    cJSON *e = cJSON_AddObjectToObject(r, "error");
    if (e) {
        cJSON_AddNumberToObject(e, "code", code);
        cJSON_AddStringToObject(e, "message", msg);
    }
    return r;
}

static cJSON *text_item(const char *text)
{
    cJSON *c = cJSON_CreateObject();
    if (!c) return NULL;
    cJSON_AddStringToObject(c, "type", "text");
    cJSON_AddStringToObject(c, "text", text);
    return c;
}

// A tools/call result carrying one text item (and isError when err).
static cJSON *tool_result(const char *text, bool err)
{
    cJSON *r = cJSON_CreateObject();
    if (!r) return NULL;
    cJSON *arr = cJSON_AddArrayToObject(r, "content");
    cJSON *t = text_item(text);
    if (!arr || !t) { cJSON_Delete(t); cJSON_Delete(r); return NULL; }
    cJSON_AddItemToArray(arr, t);
    if (err) cJSON_AddTrueToObject(r, "isError");
    return r;
}

static cJSON *tool_error(const char *fmt, const char *arg)
{
    char buf[160];
    snprintf(buf, sizeof buf, fmt, arg);
    return tool_result(buf, true);
}

static cJSON *tool_json(cJSON *j)  // takes ownership
{
    if (!j) return tool_result("out of memory", true);
    char *s = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!s) return tool_result("out of memory", true);
    cJSON *r = tool_result(s, false);
    cJSON_free(s);
    return r;
}

static cJSON *look_result(const rm_ops_t *ops)
{
    rm_look_t l;
    ops->look(ops->ctx, &l);
    char *text, *b64;
    cJSON *r = NULL;
    if (!rm_look_encode(&l, &text, &b64)) goto done;
    r = tool_result(text, false);
    cJSON *img = cJSON_CreateObject();
    if (!r || !img) { cJSON_Delete(img); cJSON_Delete(r); r = NULL; goto done; }
    cJSON_AddStringToObject(img, "type", "image");
    cJSON_AddStringToObject(img, "data", b64);
    cJSON_AddStringToObject(img, "mimeType", "image/png");
    cJSON_AddItemToArray(cJSON_GetObjectItem(r, "content"), img);
done:
    free(text);
    free(b64);
    return r ? r : tool_result("could not render the screen", true);
}

static cJSON *tool_press(const rm_ops_t *ops, const cJSON *args)
{
    rm_press_req_t req;
    const char *bad = rm_json_press_req(args, &req);
    if (!bad) bad = rm_press_validate(&req);
    if (bad) return tool_error("invalid value for %s", bad);
    switch (ops->press(ops->ctx, &req)) {
    case RM_OK: return look_result(ops);
    case RM_ERR_BUSY: return tool_result("busy: another press sequence is running", true);
    case RM_ERR_CATCHUP: return tool_result("the ball is catching up", true);
    default: return tool_result("press failed", true);
    }
}

static cJSON *tool_set_settings(const rm_ops_t *ops, const cJSON *args)
{
    st_settings_t s;
    ops->get_settings(ops->ctx, &s);
    bool display = false;
    const char *bad = rm_json_patch_settings(args, &s, &display);
    if (bad) return tool_error(*bad ? "invalid value for %s" : "invalid arguments: expected an object%s", bad);
    ops->put_settings(ops->ctx, &s, display);
    return tool_json(rm_json_settings(&s));
}

static cJSON *tool_switch_pet(const rm_ops_t *ops, const cJSON *args)
{
    const cJSON *n = cJSON_GetObjectItemCaseSensitive(args, "name");
    if (!cJSON_IsString(n) || !n->valuestring[0]) return tool_error("invalid value for %s", "name");
    switch (ops->switch_pet(ops->ctx, n->valuestring)) {
    case RM_OK: return tool_result("switching: the ball saves this pet and restarts shortly", false);
    case RM_ERR_NOTFOUND: return tool_result("no such pet", true);
    case RM_ERR_BUSY: return tool_result("busy", true);
    case RM_ERR_CATCHUP: return tool_result("the pet is catching up; try again when it finishes", true);
    default: return tool_result("switch failed", true);
    }
}

static cJSON *tool_sync_pet_clock(const rm_ops_t *ops)
{
    int32_t w = -1;
    rm_err_t e = ops->sync_clock(ops->ctx, &w);
    if (e == RM_OK && (w < 0 || w >= 86400)) e = RM_ERR_BUSY;  // can't happen; reported as USB does
    switch (e) {
    case RM_OK: {
        char t[48];
        snprintf(t, sizeof t, "Pet clock set to %02d:%02d:%02d.", (int)(w / 3600), (int)(w / 60 % 60), (int)(w % 60));
        return tool_result(t, false);
    }
    case RM_ERR_NOTFOUND: return tool_result("not available: the pet's clock is only known for P1 and P2", true);
    case RM_ERR_NOTIME: return tool_result("the ball has no internet time yet", true);
    case RM_ERR_CATCHUP: return tool_result("the pet is catching up; try again when it finishes", true);
    case RM_ERR_BUSY: return tool_result("the pet's clock is stopped (its own clock menu is open); close it and try again", true);
    default: return tool_result("failed", true);
    }
}

static bool int_arg(const cJSON *args, const char *key, int dflt, int min, int *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(args, key);
    if (!v) { *out = dflt; return true; }
    // Range first: converting a double outside int's range is undefined behaviour.
    if (!cJSON_IsNumber(v) || !(v->valuedouble >= min && v->valuedouble <= INT_MAX)) return false;
    if (v->valuedouble != (double)(int)v->valuedouble) return false;  // 2.5 is not an address
    *out = (int)v->valuedouble;
    return true;
}

static cJSON *tool_read_ram(const rm_ops_t *ops, const cJSON *args)
{
    rm_debug_t *d = rm_alloc(sizeof *d);
    if (!d) return tool_result("out of memory", true);
    ops->debug(ops->ctx, d);
    int n = (int)d->ram_n, from, count;
    cJSON *r = NULL;
    if (!int_arg(args, "from", 0, 0, &from) || (n && from >= n)) { r = tool_error("invalid value for %s", "from"); goto done; }
    if (!int_arg(args, "count", n - from, 1, &count)) { r = tool_error("invalid value for %s", "count"); goto done; }
    if (!n) { r = tool_result("nothing to read yet: the emulator hasn't published a snapshot", true); goto done; }
    if (count > n - from) count = n - from;
    const tc_cpu_t *c = &d->cpu;
    size_t cap = 512 + (size_t)count + (size_t)(count / 32 + 2) * 8;
    char *t = rm_alloc(cap);
    if (!t) { r = tool_result("out of memory", true); goto done; }
    char fl[5] = "", bt[4] = "";
    for (int i = 0; i < 4; i++) if (c->flags & (1 << i)) strncat(fl, &"CZDI"[i], 1);
    for (int i = 0; i < 3; i++) if (c->buttons & (1 << i)) strncat(bt, &"ABC"[i], 1);
    static const char *const MODEL[] = { "unknown", "P1", "P2", "test", "Angel", "Mothra" };
    size_t k = (size_t)snprintf(t, cap,
        "model=%s phase=%s seq=%lu tick=%lu\n"
        "pc=0x%03X np=0x%02X a=0x%X b=0x%X x=0x%03X y=0x%03X sp=0x%02X flags=%s halted=%d call_depth=%lu buttons=%s\n"
        "irq (mask/factor): prog_timer %X/%X serial %X/%X k10_k13 %X/%X k00_k03 %X/%X stopwatch %X/%X clock_timer %X/%X\n",
        MODEL[d->model <= TC_MODEL_MOTHRA ? d->model : 0],
        d->phase == RM_PHASE_RUN ? "run" : d->phase == RM_PHASE_CATCHUP ? "catch-up" : "boot",
        (unsigned long)c->seq, (unsigned long)c->tick, c->pc, c->np, c->a, c->b, c->x, c->y, c->sp, fl[0] ? fl : "-",
        c->halted, (unsigned long)c->call_depth, bt[0] ? bt : "-",
        c->irq[0].mask, c->irq[0].factor, c->irq[1].mask, c->irq[1].factor, c->irq[2].mask, c->irq[2].factor,
        c->irq[3].mask, c->irq[3].factor, c->irq[4].mask, c->irq[4].factor, c->irq[5].mask, c->irq[5].factor);
    for (int a = from; a < from + count && k + 48 < cap; a += 32) {
        k += (size_t)snprintf(t + k, cap - k, "0x%03X: ", a);
        for (int i = a; i < a + 32 && i < from + count; i++) t[k++] = "0123456789ABCDEF"[d->ram[i] & 15];
        t[k++] = '\n';
        t[k] = '\0';
    }
    r = tool_result(t, false);
    free(t);
done:
    free(d);
    return r;
}

static cJSON *tools_call(const rm_ops_t *ops, const cJSON *params)
{
    const cJSON *name = cJSON_IsObject(params) ? cJSON_GetObjectItemCaseSensitive(params, "name") : NULL;
    if (!cJSON_IsString(name)) return tool_error("invalid value for %s", "name");
    cJSON *own = NULL;
    const cJSON *args = cJSON_GetObjectItemCaseSensitive(params, "arguments");
    if (!args) args = own = cJSON_CreateObject();
    cJSON *r;
    const char *t = name->valuestring;
    if (!cJSON_IsObject(args)) r = tool_result("invalid arguments: expected an object", true);
    else if (!strcmp(t, "look")) r = look_result(ops);
    else if (!strcmp(t, "press")) r = tool_press(ops, args);
    else if (!strcmp(t, "status")) {
        rm_status_t st;
        ops->status(ops->ctx, &st);
        r = tool_json(rm_json_status(&st));
    } else if (!strcmp(t, "get_settings")) {
        st_settings_t s;
        ops->get_settings(ops->ctx, &s);
        r = tool_json(rm_json_settings(&s));
    } else if (!strcmp(t, "set_settings")) r = tool_set_settings(ops, args);
    else if (!strcmp(t, "list_pets")) {
        rm_pet_t *pets = rm_alloc(sizeof *pets * 16);  // ~1.7 KB: off the httpd stack
        int n = pets ? ops->list_pets(ops->ctx, pets, 16) : 0;
        r = pets ? tool_json(rm_json_pets(pets, n < 0 ? 0 : n)) : tool_result("out of memory", true);
        free(pets);
    } else if (!strcmp(t, "switch_pet")) r = tool_switch_pet(ops, args);
    else if (!strcmp(t, "sync_pet_clock")) r = tool_sync_pet_clock(ops);
    else if (!strcmp(t, "read_ram")) r = tool_read_ram(ops, args);
    else r = tool_error("unknown tool: %s", t);
    cJSON_Delete(own);
    return r;
}

static cJSON *tools_list(void)
{
    cJSON *r = cJSON_CreateObject();
    cJSON *arr = r ? cJSON_AddArrayToObject(r, "tools") : NULL;
    if (!arr) { cJSON_Delete(r); return NULL; }
    for (int i = 0; i < N_TOOLS; i++) {
        cJSON *t = cJSON_CreateObject();
        if (!t) break;
        cJSON_AddItemToArray(arr, t);
        cJSON_AddStringToObject(t, "name", TOOLS[i].name);
        cJSON_AddStringToObject(t, "description", TOOLS[i].description);
        cJSON_AddItemToObject(t, "inputSchema", cJSON_Parse(TOOLS[i].schema));
    }
    return r;
}

static cJSON *initialize(const rm_ops_t *ops, const cJSON *params)
{
    const char *ver = LATEST_VERSION;
    const cJSON *pv = cJSON_IsObject(params) ? cJSON_GetObjectItemCaseSensitive(params, "protocolVersion") : NULL;
    if (cJSON_IsString(pv))
        for (size_t i = 0; i < sizeof VERSIONS / sizeof VERSIONS[0]; i++)
            if (!strcmp(pv->valuestring, VERSIONS[i])) ver = VERSIONS[i];
    rm_status_t st;
    ops->status(ops->ctx, &st);
    cJSON *r = cJSON_CreateObject();
    if (!r) return NULL;
    cJSON_AddStringToObject(r, "protocolVersion", ver);
    cJSON *caps = cJSON_AddObjectToObject(r, "capabilities");
    if (caps) {
        cJSON_AddObjectToObject(caps, "tools");
        cJSON_AddObjectToObject(caps, "resources");
    }
    cJSON *si = cJSON_AddObjectToObject(r, "serverInfo");
    if (si) {
        cJSON_AddStringToObject(si, "name", "tamaorb");
        cJSON_AddStringToObject(si, "version", st.version);
    }
    cJSON_AddStringToObject(r, "instructions", INSTRUCTIONS);
    return r;
}

#define GUIDES 4
static const char *const GUIDE_URI[GUIDES] = {"tamaorb://guide/p1", "tamaorb://guide/p2", "tamaorb://guide/angel",
                                              "tamaorb://guide/mothra"};
static const char *const GUIDE_MODEL[GUIDES] = {"p1", "p2", "angel", "mothra"};

static cJSON *resources_list(void)
{
    cJSON *r = cJSON_CreateObject();
    cJSON *arr = r ? cJSON_AddArrayToObject(r, "resources") : NULL;
    if (!arr) { cJSON_Delete(r); return NULL; }
    static const char *const NAMES[GUIDES] = {"Guide for the P1 model", "Guide for the P2 model",
                                              "Guide for the Angel model", "Guide for the Mothra model"};
    for (int i = 0; i < GUIDES; i++) {
        cJSON *o = cJSON_CreateObject();
        if (!o) break;
        cJSON_AddItemToArray(arr, o);
        cJSON_AddStringToObject(o, "uri", GUIDE_URI[i]);
        cJSON_AddStringToObject(o, "name", NAMES[i]);
        cJSON_AddStringToObject(o, "mimeType", "text/markdown");
    }
    return r;
}

// NULL with *bad set when the URI is unknown or has no guide.
static cJSON *resources_read(const rm_ops_t *ops, const cJSON *params, bool *bad)
{
    *bad = true;
    const cJSON *u = cJSON_IsObject(params) ? cJSON_GetObjectItemCaseSensitive(params, "uri") : NULL;
    if (!cJSON_IsString(u)) return NULL;
    for (int i = 0; i < GUIDES; i++) {
        if (strcmp(u->valuestring, GUIDE_URI[i]) != 0) continue;
        const char *g = ops->guide(ops->ctx, GUIDE_MODEL[i]);
        if (!g) return NULL;
        *bad = false;
        cJSON *r = cJSON_CreateObject();
        cJSON *arr = r ? cJSON_AddArrayToObject(r, "contents") : NULL;
        cJSON *o = arr ? cJSON_CreateObject() : NULL;
        if (!o) { cJSON_Delete(r); return NULL; }
        cJSON_AddItemToArray(arr, o);
        cJSON_AddStringToObject(o, "uri", GUIDE_URI[i]);
        cJSON_AddStringToObject(o, "mimeType", "text/markdown");
        cJSON_AddStringToObject(o, "text", g);
        return r;
    }
    return NULL;
}

static int finish(cJSON *resp, char **out)
{
    char *s = resp ? cJSON_PrintUnformatted(resp) : NULL;
    cJSON_Delete(resp);
    *out = s;
    return s ? 200 : 500;
}

int rm_mcp_handle(const rm_ops_t *ops, const char *body, size_t len, char **out)
{
    *out = NULL;
    if (!body || len == 0) return finish(error_resp(NULL, -32700, "parse error"), out);
    if (!rm_json_depth_ok(body, len, RM_JSON_MAX_DEPTH)) return finish(error_resp(NULL, -32600, "invalid request: nested too deeply"), out);
    cJSON *req = rm_json_parse(body, len);
    if (!req) return finish(error_resp(NULL, -32700, "parse error"), out);
    int status;
    const cJSON *idj = cJSON_IsObject(req) ? cJSON_GetObjectItemCaseSensitive(req, "id") : NULL;
    const cJSON *ver = cJSON_IsObject(req) ? cJSON_GetObjectItemCaseSensitive(req, "jsonrpc") : NULL;
    const cJSON *method = cJSON_IsObject(req) ? cJSON_GetObjectItemCaseSensitive(req, "method") : NULL;
    bool id_ok = idj && (cJSON_IsNumber(idj) || cJSON_IsString(idj));
    if (!cJSON_IsObject(req) || !cJSON_IsString(ver) || strcmp(ver->valuestring, "2.0") != 0 ||
        !cJSON_IsString(method) || (idj && !id_ok)) {
        status = finish(error_resp(id_ok ? idj : NULL, -32600, "invalid request"), out);
        cJSON_Delete(req);
        return status;
    }
    if (!idj) {  // notification: no reply
        cJSON_Delete(req);
        return 202;
    }
    const char *m = method->valuestring;
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(req, "params");
    cJSON *resp = envelope(idj);
    cJSON *res = NULL;
    bool bad = false;
    if (!resp) {
        cJSON_Delete(req);
        return 500;
    }
    if (!strcmp(m, "initialize")) res = initialize(ops, params);
    else if (!strcmp(m, "ping")) res = cJSON_CreateObject();
    else if (!strcmp(m, "tools/list")) res = tools_list();
    else if (!strcmp(m, "tools/call")) res = tools_call(ops, params);
    else if (!strcmp(m, "resources/list")) res = resources_list();
    else if (!strcmp(m, "resources/read")) {
        res = resources_read(ops, params, &bad);
        if (bad) {
            cJSON_Delete(resp);
            resp = error_resp(idj, -32602, "unknown resource");
        }
    } else {
        cJSON_Delete(resp);
        resp = error_resp(idj, -32601, "method not found");
    }
    if (res) cJSON_AddItemToObject(resp, "result", res);
    else if (!bad && cJSON_GetObjectItem(resp, "error") == NULL) {
        cJSON_Delete(resp);
        resp = error_resp(idj, -32603, "internal error");
    }
    status = finish(resp, out);
    cJSON_Delete(req);
    return status;
}
