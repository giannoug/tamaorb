// SPDX-License-Identifier: GPL-3.0-or-later
// Remote control helpers: screen as text and PNG, timed button-press sequencer. Portable.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tama_core.h"
#include "tc_rom.h"

typedef enum { RM_PHASE_BOOT, RM_PHASE_CATCHUP, RM_PHASE_RUN } rm_phase_t;
typedef struct {
    tc_frame_t frame;
    tc_model_t model;
    bool buzzer;
    rm_phase_t phase;
    uint8_t catchup_pct;
    uint32_t seq;
} rm_look_t;

extern const char *const RM_ICON_NAMES[8];

// 16 lines of 32 chars plus icons/buzzer/phase/frame lines. Returns the length, 0 if cap is too small.
size_t rm_look_text(const rm_look_t *l, char *out, size_t cap);

#define RM_PNG_W 128
#define RM_PNG_H 96
// 1-bit greyscale PNG (1 = white background, 0 = ink). Returns the length, 0 if cap is too small.
size_t rm_look_png(const rm_look_t *l, uint8_t *out, size_t cap);

typedef enum { RM_STEP_A = 1, RM_STEP_B = 2, RM_STEP_C = 4, RM_STEP_AC = 5 } rm_step_t;  // = button masks
#define RM_PRESS_MAX_STEPS 32
typedef struct {
    rm_step_t steps[RM_PRESS_MAX_STEPS];
    int n;
    uint16_t hold_ms, gap_ms, settle_ms;
} rm_press_req_t;

void rm_press_defaults(rm_press_req_t *r);
bool rm_press_add(rm_press_req_t *r, const char *token);
bool rm_press_parse(rm_press_req_t *r, const char *spec);
const char *rm_press_validate(const rm_press_req_t *r);  // NULL ok, else the offending field

typedef struct {
    rm_press_req_t req;
    uint32_t start_ms;
    bool active;
} rm_press_t;
void rm_press_begin(rm_press_t *p, const rm_press_req_t *r, uint32_t now_ms);
uint8_t rm_press_mask(const rm_press_t *p, uint32_t now_ms);
bool rm_press_done(const rm_press_t *p, uint32_t now_ms);
bool rm_press_window(const rm_press_t *p, uint32_t now_ms);

// ---- Save backup / restore (spec §7.3) ----
#include "persist.h"
typedef enum { RM_RESTORE_FROZEN, RM_RESTORE_REPLAY } rm_restore_mode_t;
bool rm_restore_mode_parse(const char *s, rm_restore_mode_t *out);  // "frozen" | "replay"
// NULL ok, else "record" (CRC/format/size) | "rom" (sha1 mismatch) | "model" (not P1/P2) | "pet" (pet_id != 0)
// | "state" (state_ok rejected the payload; state_ok may be NULL). meta_out is filled when the record decodes.
const char *rm_save_check(const uint8_t *rec, size_t len, const uint8_t sha1[20],
                          bool (*state_ok)(const uint8_t *payload, size_t len), ps_meta_t *meta_out);
// e.g. "tamaorb-P1-4b4979cf-2026-11-08.sav" (UTC date of meta->unix_time; "undated" if 0)
void rm_save_filename(tc_model_t model, const ps_meta_t *meta, char out[48]);
