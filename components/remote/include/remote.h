// SPDX-License-Identifier: GPL-3.0-or-later
// Remote control helpers: screen as text and PNG, timed button-press sequencer. Portable.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tama_core.h"
#include "tc_rom.h"

// WAIT: the pet came back after a gap and waits for the owner to choose Resume or Fast-forward.
typedef enum { RM_PHASE_BOOT, RM_PHASE_CATCHUP, RM_PHASE_RUN, RM_PHASE_WAIT } rm_phase_t;
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
// Text and base64 PNG of l, both from rm_alloc (free() them). False, with both NULL, if memory runs out.
bool rm_look_encode(const rm_look_t *l, char **text, char **png_b64);

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

// ---- Save backup / restore ----
#include "persist.h"
typedef enum { RM_RESTORE_FROZEN, RM_RESTORE_REPLAY } rm_restore_mode_t;
bool rm_restore_mode_parse(const char *s, rm_restore_mode_t *out);  // "frozen" | "replay"
// NULL ok, else "record" (CRC/format/size) | "rom" (sha1 mismatch) | "model" (not a playable model) | "pet" (another pet)
// | "state" (state_ok rejected the payload; state_ok may be NULL). meta_out is filled when the record decodes.
const char *rm_save_check(const uint8_t *rec, size_t len, const uint8_t sha1[20], uint8_t pet_id,
                          bool (*state_ok)(const uint8_t *payload, size_t len), ps_meta_t *meta_out);
// e.g. "tamaorb-P1-4b4979cf-2026-11-08.sav" (UTC date of meta->unix_time; "undated" if 0)
void rm_save_filename(tc_model_t model, const ps_meta_t *meta, char out[48]);

// ---- Operations table, JSON helpers and the MCP dispatcher ----
#include "cJSON.h"
#include "settings.h"

// The debugger's snapshot: the CPU, LCD and RAM from one publish (tc_get_snapshot), with the model and phase.
typedef struct {
    tc_model_t model;
    rm_phase_t phase;
    bool buzzer;
    tc_cpu_t cpu;
    tc_frame_t frame;
    uint8_t ram[TC_RAM_NIBBLES];
    size_t ram_n;              // nibbles valid in ram (0: nothing published yet)
} rm_debug_t;

typedef enum { RM_OK = 0, RM_ERR_BUSY, RM_ERR_CATCHUP, RM_ERR_NOTFOUND, RM_ERR_BAD, RM_ERR_IO, RM_ERR_NOTIME,
               RM_ERR_FULL, RM_ERR_TAKEN, RM_ERR_WAITING } rm_err_t;  // FULL: no free pet number / storage;
                                                                    // TAKEN: name in use; WAITING: EMU_WAIT
// A pet: a ROM (SHA-1) and a pet number 0-9. Its id on the API is "<40 hex>-<digit>" (rm_pet_id_*).
typedef struct {
    uint8_t sha1[20];
    uint8_t id;
    char name[17];
    tc_model_t model;
    bool active, has_save, save_trusted, rom_on_card, egg;
    int16_t age_years;     // -1 unknown
    int64_t save_unix;
} rm_pet_t;
typedef struct { char file[64]; tc_model_t model; uint8_t sha1[20]; } rm_rom_t;  // a playable ROM on the card
#define RM_PET_ID_LEN 42
#define RM_PETS_MAX 40     // a pet list never holds more (4 models x 10)
bool rm_pet_id_parse(const char *s, uint8_t sha1[20], uint8_t *id);  // a playable model's SHA-1, id 0-9
void rm_pet_id_format(const uint8_t sha1[20], uint8_t id, char out[RM_PET_ID_LEN + 1]);
typedef struct {
    char name[33], version[32], rom_name[64], wifi_ssid[33], ip[16];
    tc_model_t model;
    rm_phase_t phase;
    uint8_t catchup_pct;
    uint32_t uptime_s, batt_mv, heap_free, heap_min;
    uint8_t batt_pct;
    int8_t rssi;
    bool usb, wifi, time_trusted, sd_present, restart_pending, last_save_known;
    bool has_clock;        // the running model has a clock the ball can set (tc_pet_has_clock)
    int64_t unix_time, last_save_unix;
    float catchup_speed;
    int32_t pet_clock_s, ball_clock_s;  // seconds of the day; -1 unknown (JSON: "HH:MM:SS" or null)
    char pet_name[17];                  // the running pet ("" none)
    // While phase is WAIT: the gap the question is about, the fast-forward's estimate, and the Back target ("" none).
    uint64_t wait_gap_s;
    uint32_t wait_ff_s;
    char back_name[17];
} rm_status_t;
typedef struct {
    void (*look)(void *ctx, rm_look_t *out);
    rm_err_t (*press)(void *ctx, const rm_press_req_t *req);  // blocks until done (<= 10 s); req is validated
    void (*status)(void *ctx, rm_status_t *out);
    void (*get_settings)(void *ctx, st_settings_t *out);
    void (*put_settings)(void *ctx, const st_settings_t *s, bool display_changed);
    int (*list_pets)(void *ctx, rm_pet_t *out, int max);
    int (*list_roms)(void *ctx, rm_rom_t *out, int max);
    rm_err_t (*switch_pet)(void *ctx, const uint8_t sha1[20], uint8_t id);  // schedules the restart itself
    rm_err_t (*new_pet)(void *ctx, const uint8_t sha1[20]);                 // creates, then switches to it
    rm_err_t (*rename_pet)(void *ctx, const uint8_t sha1[20], uint8_t id, const char *name);  // name is valid
    rm_err_t (*delete_pet)(void *ctx, const uint8_t sha1[20], uint8_t id);  // BUSY for the running pet
    rm_err_t (*export_save)(void *ctx, const uint8_t sha1[20], uint8_t id, uint8_t *out, size_t cap, size_t *len,
                            ps_meta_t *meta);
    rm_err_t (*import_save)(void *ctx, const uint8_t sha1[20], uint8_t id, uint8_t *rec, size_t len,
                            rm_restore_mode_t mode);
    bool (*set_wifi)(void *ctx, const char *ssid, const char *pass);  // schedules the restart itself
    void (*restart)(void *ctx);                                       // schedules the restart
    const char *(*guide)(void *ctx, const char *model);  // "p1"|"p2"|"angel"|"mothra" -> Markdown, NULL if none
    bool (*restart_pending)(void *ctx);  // same as status' restart_pending, without the cost of a full status
    // Sets the pet's clock to the ball's local time; *written_s = the time written (seconds of the day).
    // NOTFOUND: the model has no known clock; NOTIME: no trusted time; CATCHUP; BUSY: the ROM's clock menu is open.
    rm_err_t (*sync_clock)(void *ctx, int32_t *written_s);
    void (*debug)(void *ctx, rm_debug_t *out);                  // one snapshot (also keeps the ball awake a while)
    // The answer to the resume question: "resume" | "ff" | "back" (choice is one of these). BUSY: not waiting;
    // NOTFOUND: "back" without a previous pet. "back" restarts the ball.
    rm_err_t (*resume)(void *ctx, const char *choice);
    rm_err_t (*skip)(void *ctx);                                // stops a fast-forward; BUSY when none runs
    // Erases every pet, setting and Wi-Fi at a restart it schedules (REST only, never MCP). BUSY: a restart is pending.
    rm_err_t (*factory_reset)(void *ctx);
    bool (*rom)(void *ctx, const uint8_t **data, size_t *len);  // the running ROM's bytes; false: none
    void *ctx;
} rm_ops_t;

// Large short-lived buffers (look text and PNG, request and save copies, pet lists, REST response bodies) come
// from rm_alloc, which is malloc unless the device sets another allocator (it picks PSRAM, keeping them out of
// internal RAM). Whatever it returns must be releasable with free(). NULL restores malloc.
void rm_set_alloc(void *(*alloc)(size_t n));
void *rm_alloc(size_t n);

// Shared by MCP and REST. Result objects are cJSON trees; the caller deletes them (NULL on out of memory).
cJSON *rm_json_settings(const st_settings_t *s);  // numbers as numbers, enums and strings as strings
// Applies a JSON object patch all-or-nothing to *s. NULL ok, else the first bad key ("" if not an object).
// *display_changed (may be NULL) is true iff a display key's value changed.
const char *rm_json_patch_settings(const cJSON *patch, st_settings_t *s, bool *display_changed);
cJSON *rm_json_status(const rm_status_t *st);
// GET /api/debug and the debugger's WebSocket: seq, model, phase, tick, pc, np, a, b, x, y, sp, flags, halted,
// call_depth, irq [{name, mask, factor}] x6, buttons, buzzer, lcd (16 x 8 hex digits, bit x = pixel x), icons,
// ram (ram_n upper-case hex digits).
cJSON *rm_json_debug(const rm_debug_t *d);
// key: "HH:MM:SS" for 0-86399 seconds of the day, else null.
void rm_json_add_clock(cJSON *o, const char *key, int32_t sec_of_day);
cJSON *rm_json_pets(const rm_pet_t *p, int n);
cJSON *rm_json_roms(const rm_rom_t *r, int n);
// buttons (string "A AC" or array), hold_ms, gap_ms, settle_ms. NULL ok, else the bad field.
// Does not range-check: run rm_press_validate on the result.
const char *rm_json_press_req(const cJSON *args, rm_press_req_t *out);
// Standard base64 with padding. Needs 4*ceil(len/3)+1 bytes in out; returns the length.
size_t rm_base64_encode(const uint8_t *in, size_t len, char *out);

#define RM_JSON_MAX_DEPTH 16
// False if any {/[ nesting (outside strings, honouring backslash escapes) exceeds max_depth.
// Run before cJSON_Parse: cJSON recurses per level, which would overflow a small task stack.
bool rm_json_depth_ok(const char *body, size_t len, int max_depth);
// Depth check, then parse exactly len bytes: NULL if too deep, not JSON, or followed by anything but whitespace.
cJSON *rm_json_parse(const char *body, size_t len);

// Returns the HTTP status: 200 with *out a malloc'd JSON body, 202 (notification, *out NULL),
// or 500 with *out NULL when memory runs out.
int rm_mcp_handle(const rm_ops_t *ops, const char *body, size_t len, char **out);

// ---- REST router and Origin check ----
typedef enum { RM_GET, RM_POST, RM_PUT, RM_PATCH, RM_DELETE, RM_OTHER } rm_method_t;
typedef struct {
    rm_method_t method;
    const char *path;
    const char *query;   // without '?', may be NULL
    const char *origin;  // NULL if absent
    const char *self_name, *self_ip;  // for the Origin check
    const uint8_t *body;
    size_t body_len;
    bool (*state_ok)(const uint8_t *payload, size_t len);  // passed through to rm_save_check
} rm_req_t;
typedef struct {
    int status;  // never 0 after rm_http_handle
    const char *content_type;
    char disposition[96];  // Content-Disposition value, empty if none
    uint8_t *body;         // from rm_alloc, owned by the caller (free()); NULL if empty
    size_t body_len;
} rm_resp_t;

// NULL or empty origin: allowed. Otherwise http://<name>, http://<name>.local or http://<ip>, optional :port, any case.
bool rm_origin_allowed(const char *origin, const char *self_name, const char *self_ip);
// Applies the Origin check to every non-GET. Bodies are assumed already capped by the caller (16384 bytes).
void rm_http_handle(const rm_ops_t *ops, const rm_req_t *req, rm_resp_t *resp);
