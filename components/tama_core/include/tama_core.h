// SPDX-License-Identifier: GPL-3.0-or-later
// tamalib wrapper. Portable: no ESP-IDF headers.
// Threading: tc_post_buttons, tc_get_frame, tc_frame_changed and tc_get_tone may be called
// from any task. Everything else must run on the emulator task.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { TC_BTN_A = 0, TC_BTN_B = 1, TC_BTN_C = 2 } tc_button_t;  // tamalib BTN_LEFT/MIDDLE/RIGHT
#define TC_BTN_MASK(b) (1u << (b))

typedef struct { uint32_t rows[16]; uint8_t icons; } tc_frame_t;  // bit x of rows[y] = pixel (x,y); icons bit i = icon i
typedef struct { uint32_t freq_dhz; bool on; } tc_tone_t;

typedef struct {
    uint64_t (*now_us)(void);                 // monotonic microseconds
    void     (*sleep_until_us)(uint64_t ts);  // only called in real-time mode
    void     (*tone)(uint32_t freq_dhz, bool on);  // buzzer changes, called on the emulator task; may be NULL
    void     (*lock)(void);                   // may be NULL (single-threaded hosts)
    void     (*unlock)(void);
} tc_platform_t;

bool     tc_init(const uint16_t *program, const tc_platform_t *plat);  // once per process; captures the boot snapshot
void     tc_reset_to_boot(void);
void     tc_set_realtime(bool realtime);
void     tc_step(uint32_t n);
void     tc_post_buttons(uint8_t mask);
void     tc_get_frame(tc_frame_t *out);
bool     tc_frame_changed(uint32_t *seq);
void     tc_get_tone(tc_tone_t *out);
uint32_t tc_tick_counter(void);
size_t   tc_state_size(void);
size_t   tc_state_save(uint8_t *buf, size_t cap);
bool     tc_state_load(const uint8_t *buf, size_t len);

typedef bool (*tc_catchup_cb)(uint64_t done_ticks, uint64_t total_ticks, void *ctx);  // return false to stop early
// Runs at full speed until `ticks` emulated ticks have passed (wrap-safe), calling cb every
// cb_every_ticks (0 = never). Restores real-time mode. Returns ticks actually run.
uint64_t tc_catchup(uint64_t ticks, tc_catchup_cb cb, void *ctx, uint32_t cb_every_ticks);

uint8_t  tc_applied_buttons(void);  // emulator task: the mask currently applied to tamalib

#ifdef TC_TESTING
uint8_t  tc_debug_applied_buttons(void);
void     tc_debug_set_tick_source(uint32_t (*fn)(void));  // NULL = tamalib's counter
void     tc_debug_set_step_hook(void (*fn)(void));        // called after every tamalib step
#endif
