// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { K_NUM, K_ZERO_OR_NUM, K_PALETTE, K_AGENT, K_NAME, K_NTP } kind_t;

typedef struct {
    const char *key;
    kind_t kind;
    int min, max;     // numbers; string length for name and ntp
} entry_t;

static const entry_t TABLE[] = {
    {"brightness", K_NUM, 1, 100},
    {"dim_level", K_NUM, 0, 100},
    {"dim_usb_s", K_ZERO_OR_NUM, 5, 3600},
    {"dim_battery_s", K_ZERO_OR_NUM, 5, 3600},
    {"off_battery_s", K_NUM, 10, 3600},
    {"volume", K_NUM, 0, 100},
    {"palette", K_PALETTE, 0, 0},
    {"name", K_NAME, 1, 32},
    {"ntp", K_NTP, 1, 63},
    {"agent_feedback", K_AGENT, 0, 0},
};
#define N_KEYS ((int)(sizeof TABLE / sizeof TABLE[0]))

static const char *const PALETTES[] = {"classic", "neutral", "night"};
static const char *const AGENTS[] = {"quiet", "silent", "visible"};

static const entry_t *find(const char *key)
{
    if (!key) return NULL;
    for (int i = 0; i < N_KEYS; i++)
        if (strcmp(TABLE[i].key, key) == 0) return &TABLE[i];
    return NULL;
}

static bool in_list(const char *const *list, const char *v, int *idx)
{
    for (int i = 0; i < 3; i++)
        if (strcmp(list[i], v) == 0) { *idx = i; return true; }
    return false;
}

static bool parse_num(const char *v, int *out)
{
    if (!v || *v < '0' || *v > '9') return false;   // rejects empty, space, sign
    char *end;
    long n = strtol(v, &end, 10);
    if (*end || n > 100000) return false;
    *out = (int)n;
    return true;
}

static bool is_lower_alnum(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

static bool valid_name(const char *v, size_t len)
{
    if (v[0] == '-' || v[len - 1] == '-') return false;
    for (size_t i = 0; i < len; i++)
        if (!is_lower_alnum(v[i]) && v[i] != '-') return false;
    return true;
}

static bool valid_ntp(const char *v, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        char c = v[i];
        if (!is_lower_alnum(c) && !(c >= 'A' && c <= 'Z') && c != '.' && c != '-') return false;
    }
    return true;
}

void st_defaults(st_settings_t *s, uint8_t volume_default)
{
    memset(s, 0, sizeof *s);
    s->brightness = 80;
    s->dim_level = 5;
    s->volume = volume_default;
    s->dim_usb_s = 30;
    s->dim_battery_s = 15;
    s->off_battery_s = 30;
    s->palette = ST_PALETTE_CLASSIC;
    s->agent_feedback = ST_AGENT_QUIET;
    strcpy(s->name, "tamaorb");
    strcpy(s->ntp, "pool.ntp.org");
}

int st_key_count(void) { return N_KEYS; }
const char *st_key(int i) { return (i >= 0 && i < N_KEYS) ? TABLE[i].key : NULL; }

bool st_get(const st_settings_t *s, const char *key, char *out, size_t cap)
{
    const entry_t *e = find(key);
    if (!e || !out || !cap) return false;
    int n;
    if (strcmp(key, "brightness") == 0) n = snprintf(out, cap, "%u", s->brightness);
    else if (strcmp(key, "dim_level") == 0) n = snprintf(out, cap, "%u", s->dim_level);
    else if (strcmp(key, "dim_usb_s") == 0) n = snprintf(out, cap, "%u", s->dim_usb_s);
    else if (strcmp(key, "dim_battery_s") == 0) n = snprintf(out, cap, "%u", s->dim_battery_s);
    else if (strcmp(key, "off_battery_s") == 0) n = snprintf(out, cap, "%u", s->off_battery_s);
    else if (strcmp(key, "volume") == 0) n = snprintf(out, cap, "%u", s->volume);
    else if (strcmp(key, "palette") == 0) n = snprintf(out, cap, "%s", PALETTES[s->palette]);
    else if (strcmp(key, "agent_feedback") == 0) n = snprintf(out, cap, "%s", AGENTS[s->agent_feedback]);
    else if (strcmp(key, "name") == 0) n = snprintf(out, cap, "%s", s->name);
    else n = snprintf(out, cap, "%s", s->ntp);
    return n >= 0 && (size_t)n < cap;
}

bool st_set(st_settings_t *s, const char *key, const char *value)
{
    const entry_t *e = find(key);
    if (!e || !value) return false;
    int n = 0, idx = 0;
    switch (e->kind) {
    case K_NUM:
    case K_ZERO_OR_NUM:
        if (!parse_num(value, &n)) return false;
        if (!(e->kind == K_ZERO_OR_NUM && n == 0) && (n < e->min || n > e->max)) return false;
        if (strcmp(key, "brightness") == 0) s->brightness = (uint8_t)n;
        else if (strcmp(key, "dim_level") == 0) s->dim_level = (uint8_t)n;
        else if (strcmp(key, "volume") == 0) s->volume = (uint8_t)n;
        else if (strcmp(key, "dim_usb_s") == 0) s->dim_usb_s = (uint16_t)n;
        else if (strcmp(key, "dim_battery_s") == 0) s->dim_battery_s = (uint16_t)n;
        else s->off_battery_s = (uint16_t)n;
        return true;
    case K_PALETTE:
        if (!in_list(PALETTES, value, &idx)) return false;
        s->palette = (st_palette_t)idx;
        return true;
    case K_AGENT:
        if (!in_list(AGENTS, value, &idx)) return false;
        s->agent_feedback = (st_agent_fx_t)idx;
        return true;
    case K_NAME:
    case K_NTP: {
        size_t len = strlen(value);
        if (len < (size_t)e->min || len > (size_t)e->max) return false;
        if (e->kind == K_NAME ? !valid_name(value, len) : !valid_ntp(value, len)) return false;
        memcpy(e->kind == K_NAME ? s->name : s->ntp, value, len + 1);
        return true;
    }
    }
    return false;
}

bool st_set_many(st_settings_t *s, const char *const *keys, const char *const *values, int n, int *bad)
{
    st_settings_t t = *s;
    for (int i = 0; i < n; i++) {
        if (!st_set(&t, keys[i], values[i])) {
            if (bad) *bad = i;
            return false;
        }
    }
    *s = t;
    return true;
}

bool st_is_numeric(const char *key)
{
    const entry_t *e = find(key);
    return e && (e->kind == K_NUM || e->kind == K_ZERO_OR_NUM);
}

bool st_needs_restart(const char *key)
{
    const entry_t *e = find(key);
    return e && (e->kind == K_NAME || e->kind == K_NTP);
}

bool st_is_display(const char *key)
{
    return key && (strcmp(key, "brightness") == 0 || strcmp(key, "dim_level") == 0 || strcmp(key, "palette") == 0);
}

uint8_t st_effective_dim(const st_settings_t *s)
{
    return s->dim_level < s->brightness ? s->dim_level : s->brightness;
}

void st_writer_changed(st_writer_t *w, uint32_t now_ms)
{
    w->dirty = true;
    w->changed_ms = now_ms;
}

bool st_writer_due(const st_writer_t *w, uint32_t now_ms)
{
    return w->dirty && (uint32_t)(now_ms - w->changed_ms) >= ST_WRITE_DELAY_MS;
}

void st_writer_done(st_writer_t *w) { w->dirty = false; }
