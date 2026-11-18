// SPDX-License-Identifier: GPL-3.0-or-later
// REST router and Origin check. Portable: the device's httpd glue builds an rm_req_t and sends the rm_resp_t.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "remote.h"
#include "tzdb.h"

#define SAVE_MAX 16384
#define PETS_MAX 16

// ---- responses ----
static void set_json(rm_resp_t *r, int status, cJSON *j)  // takes ownership
{
    r->status = status;
    r->content_type = "application/json";
    char *s = j ? cJSON_PrintUnformatted(j) : NULL;
    cJSON_Delete(j);
    if (!s) {
        r->status = 500;
        r->body = NULL;
        r->body_len = 0;
        return;
    }
    r->body_len = strlen(s);
    r->body = rm_alloc(r->body_len + 1);
    if (!r->body) { r->status = 500; r->body_len = 0; cJSON_free(s); return; }
    memcpy(r->body, s, r->body_len + 1);
    cJSON_free(s);
}

static void set_err(rm_resp_t *r, int status, const char *err, const char *key)
{
    cJSON *j = cJSON_CreateObject();
    if (j) {
        cJSON_AddStringToObject(j, "error", err);
        if (key) cJSON_AddStringToObject(j, "key", key);
    }
    set_json(r, status, j);
    r->status = status;  // keep the intended status even if the body could not be built
}

static void set_ok(rm_resp_t *r, bool restarting, bool with_restarting)
{
    cJSON *j = cJSON_CreateObject();
    if (j) {
        cJSON_AddTrueToObject(j, "ok");
        if (with_restarting) cJSON_AddBoolToObject(j, "restarting", restarting);
    }
    set_json(r, 200, j);
}

static void err_from(rm_resp_t *r, rm_err_t e)
{
    switch (e) {
    case RM_ERR_BUSY: set_err(r, 409, "busy", NULL); break;
    case RM_ERR_CATCHUP: set_err(r, 409, "catching up", NULL); break;
    case RM_ERR_NOTFOUND: set_err(r, 404, "not found", NULL); break;
    case RM_ERR_BAD: set_err(r, 400, "invalid", NULL); break;
    case RM_ERR_NOTIME: set_err(r, 409, "no time", NULL); break;
    default: set_err(r, 500, "failed", NULL); break;
    }
}

// ---- Origin ----
bool rm_origin_allowed(const char *origin, const char *self_name, const char *self_ip)
{
    if (!origin || !*origin) return true;
    if (strncasecmp(origin, "http://", 7) != 0) return false;
    const char *h = origin + 7;
    size_t hl = strcspn(h, ":/?#");
    if (hl == 0) return false;
    if (h[hl] == ':') {
        const char *p = h + hl + 1;
        if (!*p) return false;
        for (; *p; p++)
            if (!isdigit((unsigned char)*p)) return false;
    } else if (h[hl]) {
        return false;
    }
    if (self_name && *self_name) {
        size_t nl = strlen(self_name);
        if (hl == nl && strncasecmp(h, self_name, nl) == 0) return true;
        if (hl == nl + 6 && strncasecmp(h, self_name, nl) == 0 && strncasecmp(h + nl, ".local", 6) == 0) return true;
    }
    if (self_ip && *self_ip && hl == strlen(self_ip) && strncasecmp(h, self_ip, hl) == 0) return true;
    return false;
}

// ---- helpers ----
static cJSON *parse_body(const rm_req_t *q)  // NULL if absent, too deep or not JSON
{
    if (!q->body || !q->body_len) return NULL;
    return rm_json_parse((const char *)q->body, q->body_len);  // in place, by length: no copy of up to 16 KB
}

static bool query_get(const char *query, const char *key, char *out, size_t cap)
{
    size_t kl = strlen(key);
    while (query && *query) {
        size_t n = strcspn(query, "&");
        if (n > kl && !strncmp(query, key, kl) && query[kl] == '=') {
            size_t vl = n - kl - 1;
            if (vl >= cap) return false;
            memcpy(out, query + kl + 1, vl);
            out[vl] = 0;
            return true;
        }
        query += n + (query[n] == '&');
    }
    return false;
}

static bool parse_sha(const char *s, uint8_t out[20])
{
    for (int i = 0; i < 40; i++)
        if (!isxdigit((unsigned char)s[i])) return false;
    if (s[40]) return false;
    for (int i = 0; i < 20; i++) {
        char b[3] = {s[2 * i], s[2 * i + 1], 0};
        out[i] = (uint8_t)strtoul(b, NULL, 16);
    }
    return true;
}

// Matches /api/pets/<40 hex>/save. 1 = match (sha filled), 0 = no.
static int match_save_path(const char *path, uint8_t sha[20])
{
    static const char P[] = "/api/pets/";
    if (strncmp(path, P, sizeof P - 1)) return 0;
    const char *h = path + sizeof P - 1;
    if (strlen(h) != 40 + 5 || strcmp(h + 40, "/save")) return 0;
    char hex[41];
    memcpy(hex, h, 40);
    hex[40] = 0;
    return parse_sha(hex, sha);
}

static int find_pet(const rm_ops_t *ops, const uint8_t sha[20], rm_pet_t *out)  // 1 found, 0 not, -1 oom
{
    rm_pet_t *pets = rm_alloc(sizeof *pets * PETS_MAX);
    if (!pets) return -1;
    int n = ops->list_pets(ops->ctx, pets, PETS_MAX);
    int found = 0;
    for (int i = 0; i < n && i < PETS_MAX; i++)
        if (!memcmp(pets[i].sha1, sha, 20)) { *out = pets[i]; found = 1; break; }
    free(pets);
    return found;
}

// ---- handlers ----
static void settings_reply(const rm_ops_t *ops, rm_resp_t *r)
{
    st_settings_t s;
    ops->get_settings(ops->ctx, &s);
    cJSON *j = cJSON_CreateObject();
    cJSON *sj = rm_json_settings(&s);
    if (!j || !sj) { cJSON_Delete(j); cJSON_Delete(sj); set_json(r, 500, NULL); return; }
    cJSON_AddItemToObject(j, "settings", sj);
    // Not ops->status: that reads NVS under the saver mutex, and a dragged slider PATCHes ~10 times a second.
    cJSON_AddBoolToObject(j, "restart_pending", ops->restart_pending(ops->ctx));
    set_json(r, 200, j);
}

static void h_status(const rm_ops_t *ops, rm_resp_t *r)
{
    rm_status_t st;
    ops->status(ops->ctx, &st);
    set_json(r, 200, rm_json_status(&st));
}

static void h_patch_settings(const rm_ops_t *ops, const rm_req_t *q, rm_resp_t *r)
{
    cJSON *p = parse_body(q);
    if (!p || !cJSON_IsObject(p)) { cJSON_Delete(p); set_err(r, 400, "invalid", NULL); return; }
    st_settings_t s;
    ops->get_settings(ops->ctx, &s);
    bool disp = false;
    const char *bad = rm_json_patch_settings(p, &s, &disp);
    if (bad) { set_err(r, 400, "invalid", *bad ? bad : NULL); cJSON_Delete(p); return; }  // bad points into p
    cJSON_Delete(p);
    ops->put_settings(ops->ctx, &s, disp);
    settings_reply(ops, r);
}

static void h_timezones(rm_resp_t *r)
{
    cJSON *j = cJSON_CreateObject(), *a = cJSON_CreateArray();
    if (!j || !a) { cJSON_Delete(j); cJSON_Delete(a); set_json(r, 500, NULL); return; }
    cJSON_AddItemToObject(j, "zones", a);
    for (int i = 0; i < tzdb_count(); i++) {
        cJSON *n = cJSON_CreateString(tzdb_at(i)->name);
        if (!n) { cJSON_Delete(j); set_json(r, 500, NULL); return; }
        cJSON_AddItemToArray(a, n);
    }
    set_json(r, 200, j);
}

static void h_wifi(const rm_ops_t *ops, const rm_req_t *q, rm_resp_t *r)
{
    cJSON *p = parse_body(q);
    if (!p || !cJSON_IsObject(p)) { cJSON_Delete(p); set_err(r, 400, "invalid", NULL); return; }
    const cJSON *ss = cJSON_GetObjectItem(p, "ssid"), *pw = cJSON_GetObjectItem(p, "password");
    const char *bad = NULL;
    if (!cJSON_IsString(ss) || !ss->valuestring[0] || strlen(ss->valuestring) > 32) bad = "ssid";
    else if (pw && !cJSON_IsString(pw)) bad = "password";
    else if (pw && pw->valuestring[0] && (strlen(pw->valuestring) < 8 || strlen(pw->valuestring) > 63)) bad = "password";
    if (bad) { cJSON_Delete(p); set_err(r, 400, "invalid", bad); return; }
    bool ok = ops->set_wifi(ops->ctx, ss->valuestring, pw ? pw->valuestring : "");
    cJSON_Delete(p);
    if (!ok) { set_err(r, 500, "failed", NULL); return; }
    set_ok(r, true, true);
}

static void h_pets(const rm_ops_t *ops, rm_resp_t *r)
{
    rm_pet_t *pets = rm_alloc(sizeof *pets * PETS_MAX);
    if (!pets) { set_json(r, 500, NULL); return; }
    int n = ops->list_pets(ops->ctx, pets, PETS_MAX);
    cJSON *j = rm_json_pets(pets, n);
    free(pets);
    set_json(r, 200, j);
}

static void h_switch(const rm_ops_t *ops, const rm_req_t *q, rm_resp_t *r)
{
    cJSON *p = parse_body(q);
    if (!p || !cJSON_IsObject(p)) { cJSON_Delete(p); set_err(r, 400, "invalid", NULL); return; }
    const cJSON *n = cJSON_GetObjectItem(p, "name");
    if (!cJSON_IsString(n) || !n->valuestring[0]) { cJSON_Delete(p); set_err(r, 400, "invalid", "name"); return; }
    rm_err_t e = ops->switch_pet(ops->ctx, n->valuestring);
    cJSON_Delete(p);
    if (e != RM_OK) { err_from(r, e); return; }
    set_ok(r, true, true);
}

static void look_reply(const rm_ops_t *ops, rm_resp_t *r)
{
    rm_look_t l;
    ops->look(ops->ctx, &l);
    char *text, *b64;
    cJSON *j = NULL;
    if (rm_look_encode(&l, &text, &b64) && (j = cJSON_CreateObject())) {
        cJSON_AddStringToObject(j, "text", text);
        cJSON_AddStringToObject(j, "png", b64);
    }
    free(text);
    free(b64);
    set_json(r, j ? 200 : 500, j);
}

static void h_press(const rm_ops_t *ops, const rm_req_t *q, rm_resp_t *r)
{
    cJSON *p = parse_body(q);
    if (!p || !cJSON_IsObject(p)) { cJSON_Delete(p); set_err(r, 400, "invalid", NULL); return; }
    rm_press_req_t req;
    const char *bad = rm_json_press_req(p, &req);
    cJSON_Delete(p);
    if (!bad) bad = rm_press_validate(&req);
    if (bad) { set_err(r, 400, "invalid", bad); return; }
    rm_err_t e = ops->press(ops->ctx, &req);
    if (e != RM_OK) { err_from(r, e); return; }
    look_reply(ops, r);
}

static void h_save_get(const rm_ops_t *ops, const uint8_t sha[20], rm_resp_t *r)
{
    // Ruling R11: any pet with a save, listed or not (a restored save whose ROM left the card). The model comes
    // from the hash; only saves of playable models exist in a form a ball can restore.
    tc_model_t model = tc_model_from_sha1(sha);
    if (!tc_model_playable(model)) { set_err(r, 404, "not found", NULL); return; }
    uint8_t *buf = rm_alloc(SAVE_MAX);
    if (!buf) { set_json(r, 500, NULL); return; }
    size_t len = 0;
    ps_meta_t meta;
    memset(&meta, 0, sizeof meta);
    rm_err_t e = ops->export_save(ops->ctx, sha, buf, SAVE_MAX, &len, &meta);
    if (e != RM_OK) { free(buf); err_from(r, e); return; }
    char fn[48];
    rm_save_filename(model, &meta, fn);
    snprintf(r->disposition, sizeof r->disposition, "attachment; filename=\"%s\"", fn);
    r->status = 200;
    r->content_type = "application/octet-stream";
    r->body = buf;
    r->body_len = len;
}

static void h_save_put(const rm_ops_t *ops, const rm_req_t *q, const uint8_t sha[20], rm_resp_t *r)
{
    char m[16];
    rm_restore_mode_t mode;
    if (!query_get(q->query, "mode", m, sizeof m) || !rm_restore_mode_parse(m, &mode)) {
        set_err(r, 400, "invalid", "mode");
        return;
    }
    if (!q->body || !q->body_len || q->body_len > SAVE_MAX) { set_err(r, 400, "invalid", "record"); return; }
    ps_meta_t meta;
    const char *bad = rm_save_check(q->body, q->body_len, sha, q->state_ok, &meta);
    if (bad) { set_err(r, 400, "invalid", bad); return; }
    // The ROM need not be on the card — the pet list only decides "restarting".
    rm_pet_t pet;
    int f = find_pet(ops, sha, &pet);
    if (f < 0) { set_json(r, 500, NULL); return; }
    uint8_t *rec = rm_alloc(q->body_len);  // import_save re-stamps it in place
    if (!rec) { set_json(r, 500, NULL); return; }
    memcpy(rec, q->body, q->body_len);
    rm_err_t e = ops->import_save(ops->ctx, sha, rec, q->body_len, mode);
    free(rec);
    if (e != RM_OK) { err_from(r, e); return; }
    set_ok(r, f && pet.active, true);
}

static void h_pet_clock_sync(const rm_ops_t *ops, rm_resp_t *r)
{
    int32_t written = -1;
    rm_err_t e = ops->sync_clock(ops->ctx, &written);
    if (e != RM_OK) { err_from(r, e); return; }
    cJSON *j = cJSON_CreateObject();
    if (j) rm_json_add_clock(j, "pet_clock", written);
    set_json(r, 200, j);
}

static void h_debug(const rm_ops_t *ops, rm_resp_t *r)
{
    rm_debug_t *d = rm_alloc(sizeof *d);  // ~1 KB: off the httpd stack
    if (!d) { set_json(r, 500, NULL); return; }
    ops->debug(ops->ctx, d);
    set_json(r, 200, rm_json_debug(d));
    free(d);
}

static void h_rom(const rm_ops_t *ops, rm_resp_t *r)
{
    const uint8_t *data;
    size_t len;
    if (!ops->rom(ops->ctx, &data, &len)) { set_err(r, 404, "not found", NULL); return; }
    uint8_t *buf = rm_alloc(len);
    if (!buf) { set_json(r, 500, NULL); return; }
    memcpy(buf, data, len);
    r->status = 200;
    r->content_type = "application/octet-stream";
    r->body = buf;
    r->body_len = len;
}

// ---- router ----
void rm_http_handle(const rm_ops_t *ops, const rm_req_t *q, rm_resp_t *r)
{
    r->status = 500;
    r->content_type = "application/json";
    r->disposition[0] = 0;
    r->body = NULL;
    r->body_len = 0;
    const char *path = q->path ? q->path : "";

    if (q->method != RM_GET && !rm_origin_allowed(q->origin, q->self_name, q->self_ip)) {
        set_err(r, 403, "origin", NULL);
        return;
    }

    uint8_t sha[20];
    typedef struct { const char *path; unsigned allow; } route_t;  // allow: bit per rm_method_t
#define M(m) (1u << (m))
    static const route_t ROUTES[] = {
        {"/api/status", M(RM_GET)},        {"/api/settings", M(RM_GET) | M(RM_PATCH)},
        {"/api/wifi", M(RM_POST)},         {"/api/restart", M(RM_POST)},
        {"/api/pets", M(RM_GET)},          {"/api/pets/switch", M(RM_POST)},
        {"/api/look", M(RM_GET)},          {"/api/press", M(RM_POST)},
        {"/api/timezones", M(RM_GET)},     {"/api/pet-clock/sync", M(RM_POST)},
        {"/api/debug", M(RM_GET)},         {"/api/rom", M(RM_GET)},
    };
    if (match_save_path(path, sha)) {
        if (q->method == RM_GET) h_save_get(ops, sha, r);
        else if (q->method == RM_PUT) h_save_put(ops, q, sha, r);
        else set_err(r, 405, "method", NULL);
        return;
    }
    for (size_t i = 0; i < sizeof ROUTES / sizeof *ROUTES; i++) {
        if (strcmp(path, ROUTES[i].path)) continue;
        if (!(ROUTES[i].allow & M(q->method))) { set_err(r, 405, "method", NULL); return; }
        switch (i) {
        case 0: h_status(ops, r); break;
        case 1: if (q->method == RM_GET) settings_reply(ops, r); else h_patch_settings(ops, q, r); break;
        case 2: h_wifi(ops, q, r); break;
        case 3: ops->restart(ops->ctx); set_ok(r, true, true); break;
        case 4: h_pets(ops, r); break;
        case 5: h_switch(ops, q, r); break;
        case 6: look_reply(ops, r); break;
        case 7: h_press(ops, q, r); break;
        case 8: h_timezones(r); break;
        case 9: h_pet_clock_sync(ops, r); break;
        case 10: h_debug(ops, r); break;
        case 11: h_rom(ops, r); break;
        }
        return;
    }
    set_err(r, 404, "not found", NULL);
}
