// SPDX-License-Identifier: GPL-3.0-or-later
// Portable settings table: keys, ranges, defaults, validation and write debounce.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { ST_PALETTE_CLASSIC, ST_PALETTE_NEUTRAL, ST_PALETTE_NIGHT } st_palette_t;
typedef enum { ST_AGENT_QUIET, ST_AGENT_SILENT, ST_AGENT_VISIBLE } st_agent_fx_t;

typedef struct {
    uint8_t brightness, dim_level, volume;
    uint16_t dim_usb_s, dim_battery_s, off_battery_s;
    st_palette_t palette;
    st_agent_fx_t agent_feedback;
    char name[33];
    char ntp[64];
} st_settings_t;

#define ST_WRITE_DELAY_MS 2000u

void st_defaults(st_settings_t *s, uint8_t volume_default);
int st_key_count(void);
const char *st_key(int i);
bool st_get(const st_settings_t *s, const char *key, char *out, size_t cap);
bool st_set(st_settings_t *s, const char *key, const char *value);
// All or nothing. On failure *bad is the index of the first rejected pair.
bool st_set_many(st_settings_t *s, const char *const *keys, const char *const *values, int n, int *bad);
bool st_is_numeric(const char *key);
bool st_needs_restart(const char *key);
// True iff a key that applies after a restart (name, ntp) differs between the values the ball booted with and
// the current ones. Renaming back to the boot name clears it.
bool st_restart_pending(const st_settings_t *boot, const st_settings_t *cur);
bool st_is_display(const char *key);
uint8_t st_effective_dim(const st_settings_t *s);

typedef struct { bool dirty; uint32_t changed_ms; } st_writer_t;
void st_writer_changed(st_writer_t *w, uint32_t now_ms);
bool st_writer_due(const st_writer_t *w, uint32_t now_ms);
void st_writer_done(st_writer_t *w);
