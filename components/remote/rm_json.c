// SPDX-License-Identifier: GPL-3.0-or-later
// JSON encodings shared by the MCP dispatcher and the REST router. Portable.
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "remote.h"

static void *(*s_alloc)(size_t n) = malloc;

void rm_set_alloc(void *(*alloc)(size_t n)) { s_alloc = alloc ? alloc : malloc; }
void *rm_alloc(size_t n) { return s_alloc(n); }

size_t rm_base64_encode(const uint8_t *in, size_t len, char *out)
{
    static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < len) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < len) v |= in[i + 2];
        out[o++] = A[(v >> 18) & 63];
        out[o++] = A[(v >> 12) & 63];
        out[o++] = i + 1 < len ? A[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < len ? A[v & 63] : '=';
    }
    out[o] = 0;
    return o;
}

cJSON *rm_json_settings(const st_settings_t *s)
{
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;
    for (int i = 0; i < st_key_count(); i++) {
        const char *k = st_key(i);
        char v[64];
        if (!st_get(s, k, v, sizeof v)) continue;
        if (st_is_bool(k)) cJSON_AddBoolToObject(o, k, !strcmp(v, "true"));
        else if (st_is_numeric(k)) cJSON_AddNumberToObject(o, k, atoi(v));
        else cJSON_AddStringToObject(o, k, v);
    }
    return o;
}

static bool whole(const cJSON *n, int *out)
{
    double d = n->valuedouble;
    if (d < -1e9 || d > 1e9) return false;
    int v = (int)d;
    if ((double)v != d) return false;
    *out = v;
    return true;
}

const char *rm_json_patch_settings(const cJSON *patch, st_settings_t *s, bool *display_changed)
{
    if (display_changed) *display_changed = false;
    if (!patch || !cJSON_IsObject(patch)) return "";
    st_settings_t work = *s;
    for (const cJSON *it = patch->child; it; it = it->next) {
        const char *key = it->string ? it->string : "";
        char num[16];
        const char *val;
        if (st_is_bool(key)) {  // booleans are JSON booleans only
            if (!cJSON_IsBool(it)) return key;
            val = cJSON_IsTrue(it) ? "true" : "false";
        } else if (cJSON_IsNumber(it)) {
            int n;
            if (!whole(it, &n)) return key;
            snprintf(num, sizeof num, "%d", n);
            val = num;
        } else if (cJSON_IsString(it) && it->valuestring) {
            val = it->valuestring;
        } else {
            return key;
        }
        if (!st_set(&work, key, val)) return key;  // all-or-nothing: *s is only updated below
    }
    bool disp = false;
    for (int i = 0; i < st_key_count(); i++) {
        const char *k = st_key(i);
        char a[64], b[64];
        if (!st_is_display(k)) continue;
        if (st_get(s, k, a, sizeof a) && st_get(&work, k, b, sizeof b) && strcmp(a, b) != 0) disp = true;
    }
    *s = work;
    if (display_changed) *display_changed = disp;
    return NULL;
}

static const char *model_name(tc_model_t m) { return tc_model_playable(m) ? tc_model_name(m) : "unknown"; }

void rm_json_add_clock(cJSON *o, const char *key, int32_t sec_of_day)
{
    if (sec_of_day < 0 || sec_of_day >= 86400) { cJSON_AddNullToObject(o, key); return; }
    char t[12];
    snprintf(t, sizeof t, "%02d:%02d:%02d", (int)(sec_of_day / 3600), (int)(sec_of_day / 60 % 60), (int)(sec_of_day % 60));
    cJSON_AddStringToObject(o, key, t);
}

static const char *const IRQ_NAMES[6] = { "prog_timer", "serial", "k10_k13", "k00_k03", "stopwatch", "clock_timer" };

cJSON *rm_json_debug(const rm_debug_t *d)
{
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;
    const tc_cpu_t *c = &d->cpu;
    static const char *const PHASE[] = { [RM_PHASE_BOOT] = "boot", [RM_PHASE_CATCHUP] = "catch-up", [RM_PHASE_RUN] = "run",
                                         [RM_PHASE_WAIT] = "wait" };
    cJSON_AddNumberToObject(o, "seq", c->seq);
    cJSON_AddStringToObject(o, "model", model_name(d->model));
    cJSON_AddStringToObject(o, "phase", PHASE[d->phase <= RM_PHASE_WAIT ? d->phase : RM_PHASE_RUN]);
    cJSON_AddNumberToObject(o, "tick", c->tick);
    cJSON_AddNumberToObject(o, "pc", c->pc);
    cJSON_AddNumberToObject(o, "np", c->np);
    cJSON_AddNumberToObject(o, "a", c->a);
    cJSON_AddNumberToObject(o, "b", c->b);
    cJSON_AddNumberToObject(o, "x", c->x);
    cJSON_AddNumberToObject(o, "y", c->y);
    cJSON_AddNumberToObject(o, "sp", c->sp);
    cJSON_AddNumberToObject(o, "flags", c->flags);
    cJSON_AddBoolToObject(o, "halted", c->halted);
    cJSON_AddNumberToObject(o, "call_depth", c->call_depth);
    cJSON *irq = cJSON_AddArrayToObject(o, "irq");
    for (int i = 0; irq && i < 6; i++) {
        cJSON *e = cJSON_CreateObject();
        if (!e) break;
        cJSON_AddStringToObject(e, "name", IRQ_NAMES[i]);
        cJSON_AddNumberToObject(e, "mask", c->irq[i].mask);
        cJSON_AddNumberToObject(e, "factor", c->irq[i].factor);
        cJSON_AddItemToArray(irq, e);
    }
    cJSON_AddNumberToObject(o, "buttons", c->buttons);
    cJSON_AddBoolToObject(o, "buzzer", d->buzzer);
    cJSON *lcd = cJSON_AddArrayToObject(o, "lcd");
    for (int y = 0; lcd && y < 16; y++) {
        char row[9];
        snprintf(row, sizeof row, "%08lX", (unsigned long)d->frame.rows[y]);
        cJSON_AddItemToArray(lcd, cJSON_CreateString(row));
    }
    cJSON_AddNumberToObject(o, "icons", d->frame.icons);
    size_t n = d->ram_n <= TC_RAM_NIBBLES ? d->ram_n : TC_RAM_NIBBLES;
    char *hex = rm_alloc(n + 1);
    if (!hex) { cJSON_Delete(o); return NULL; }
    for (size_t a = 0; a < n; a++) hex[a] = "0123456789ABCDEF"[d->ram[a] & 15];
    hex[n] = '\0';
    cJSON_AddStringToObject(o, "ram", hex);
    free(hex);
    return o;
}

cJSON *rm_json_status(const rm_status_t *st)
{
    cJSON *o = cJSON_CreateObject();
    if (!o) return NULL;
    cJSON_AddStringToObject(o, "name", st->name);
    cJSON_AddStringToObject(o, "version", st->version);
    cJSON_AddStringToObject(o, "rom_name", st->rom_name);
    cJSON_AddStringToObject(o, "wifi_ssid", st->wifi_ssid);
    cJSON_AddStringToObject(o, "ip", st->ip);
    cJSON_AddStringToObject(o, "model", model_name(st->model));
    cJSON_AddStringToObject(o, "phase", st->phase == RM_PHASE_BOOT ? "boot" : st->phase == RM_PHASE_CATCHUP ? "catch-up"
                                        : st->phase == RM_PHASE_WAIT ? "wait" : "run");
    cJSON_AddStringToObject(o, "pet", st->pet_name);
    if (st->phase == RM_PHASE_WAIT) {
        cJSON *w = cJSON_AddObjectToObject(o, "wait");
        if (w) {
            cJSON_AddNumberToObject(w, "gap_s", (double)st->wait_gap_s);
            cJSON_AddNumberToObject(w, "ff_s", st->wait_ff_s);
            if (st->back_name[0]) cJSON_AddStringToObject(w, "back", st->back_name);
            else cJSON_AddNullToObject(w, "back");
        }
    }
    cJSON_AddNumberToObject(o, "catchup_pct", st->catchup_pct);
    cJSON_AddNumberToObject(o, "uptime_s", st->uptime_s);
    cJSON_AddNumberToObject(o, "batt_mv", st->batt_mv);
    cJSON_AddNumberToObject(o, "heap_free", st->heap_free);
    cJSON_AddNumberToObject(o, "heap_min", st->heap_min);
    cJSON_AddNumberToObject(o, "batt_pct", st->batt_pct);
    cJSON_AddNumberToObject(o, "rssi", st->rssi);
    cJSON_AddBoolToObject(o, "usb", st->usb);
    cJSON_AddBoolToObject(o, "wifi", st->wifi);
    cJSON_AddBoolToObject(o, "time_trusted", st->time_trusted);
    cJSON_AddBoolToObject(o, "sd_present", st->sd_present);
    cJSON_AddBoolToObject(o, "restart_pending", st->restart_pending);
    cJSON_AddBoolToObject(o, "last_save_known", st->last_save_known);
    cJSON_AddNumberToObject(o, "unix_time", (double)st->unix_time);
    if (st->last_save_known) cJSON_AddNumberToObject(o, "last_save_unix", (double)st->last_save_unix);
    else cJSON_AddNullToObject(o, "last_save_unix");
    cJSON_AddNumberToObject(o, "catchup_speed", st->catchup_speed);
    rm_json_add_clock(o, "pet_clock", st->pet_clock_s);
    cJSON_AddBoolToObject(o, "has_clock", st->has_clock);
    rm_json_add_clock(o, "ball_clock", st->ball_clock_s);
    return o;
}

cJSON *rm_json_pets(const rm_pet_t *p, int n)
{
    cJSON *a = cJSON_CreateArray();
    if (!a) return NULL;
    for (int i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        if (!o) { cJSON_Delete(a); return NULL; }
        cJSON_AddItemToArray(a, o);
        char id[RM_PET_ID_LEN + 1], hex[41];
        rm_pet_id_format(p[i].sha1, p[i].id, id);
        for (int j = 0; j < 20; j++) snprintf(hex + j * 2, 3, "%02x", p[i].sha1[j]);
        cJSON_AddStringToObject(o, "id", id);
        cJSON_AddStringToObject(o, "name", p[i].name);
        cJSON_AddStringToObject(o, "model", model_name(p[i].model));
        cJSON_AddStringToObject(o, "sha1", hex);
        cJSON_AddBoolToObject(o, "active", p[i].active);
        cJSON_AddBoolToObject(o, "has_save", p[i].has_save);
        cJSON_AddBoolToObject(o, "save_trusted", p[i].save_trusted);
        if (p[i].has_save) cJSON_AddNumberToObject(o, "save_unix", (double)p[i].save_unix);
        else cJSON_AddNullToObject(o, "save_unix");
        cJSON_AddBoolToObject(o, "rom_on_card", p[i].rom_on_card);
        cJSON_AddBoolToObject(o, "egg", p[i].egg);
        if (p[i].age_years >= 0) cJSON_AddNumberToObject(o, "age", p[i].age_years);
        else cJSON_AddNullToObject(o, "age");
    }
    return a;
}

cJSON *rm_json_roms(const rm_rom_t *r, int n)
{
    cJSON *a = cJSON_CreateArray();
    if (!a) return NULL;
    for (int i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        if (!o) { cJSON_Delete(a); return NULL; }
        cJSON_AddItemToArray(a, o);
        char hex[41];
        for (int j = 0; j < 20; j++) snprintf(hex + j * 2, 3, "%02x", r[i].sha1[j]);
        cJSON_AddStringToObject(o, "file", r[i].file);
        cJSON_AddStringToObject(o, "model", model_name(r[i].model));
        cJSON_AddStringToObject(o, "sha1", hex);
    }
    return a;
}

static bool ms_field(const cJSON *args, const char *key, uint16_t *out)
{
    const cJSON *n = cJSON_GetObjectItemCaseSensitive(args, key);
    if (!n) return true;  // absent keeps the default
    if (!cJSON_IsNumber(n)) return false;
    int v;
    if (!whole(n, &v) || v < 0 || v > 65535) return false;
    *out = (uint16_t)v;
    return true;
}

const char *rm_json_press_req(const cJSON *args, rm_press_req_t *out)
{
    rm_press_defaults(out);
    if (!args || !cJSON_IsObject(args)) return "buttons";
    const cJSON *b = cJSON_GetObjectItemCaseSensitive(args, "buttons");
    if (cJSON_IsString(b)) {
        if (!rm_press_parse(out, b->valuestring)) return "buttons";
    } else if (cJSON_IsArray(b)) {
        for (const cJSON *e = b->child; e; e = e->next)
            if (!cJSON_IsString(e) || !rm_press_add(out, e->valuestring)) return "buttons";
        if (out->n == 0) return "buttons";
    } else {
        return "buttons";
    }
    if (!ms_field(args, "hold_ms", &out->hold_ms)) return "hold_ms";
    if (!ms_field(args, "gap_ms", &out->gap_ms)) return "gap_ms";
    if (!ms_field(args, "settle_ms", &out->settle_ms)) return "settle_ms";
    return NULL;
}

bool rm_json_depth_ok(const char *body, size_t len, int max_depth)
{
    int depth = 0;
    bool in_str = false;
    for (size_t i = 0; i < len; i++) {
        char c = body[i];
        if (in_str) {
            if (c == '\\') i++;
            else if (c == '"') in_str = false;
        } else if (c == '"') {
            in_str = true;
        } else if (c == '{' || c == '[') {
            if (++depth > max_depth) return false;
        } else if ((c == '}' || c == ']') && depth > 0) {
            depth--;
        }
    }
    return true;
}

cJSON *rm_json_parse(const char *body, size_t len)
{
    if (!body || !len || !rm_json_depth_ok(body, len, RM_JSON_MAX_DEPTH)) return NULL;
    const char *end = NULL;
    cJSON *j = cJSON_ParseWithLengthOpts(body, len, &end, false);
    if (!j) return NULL;
    while (end < body + len && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) end++;
    if (end != body + len) {
        cJSON_Delete(j);
        return NULL;
    }
    return j;
}
