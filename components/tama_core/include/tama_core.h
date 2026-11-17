// SPDX-License-Identifier: GPL-3.0-or-later
// tamalib wrapper. Portable: no ESP-IDF headers.
// Threading: tc_post_buttons, tc_post_remote_buttons, tc_get_frame, tc_get_ram, tc_get_snapshot, tc_frame_changed, tc_get_tone,
// tc_clock_request, tc_clock_poll and tc_state_compatible may be called from any task. Everything else must run on the emulator task.
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
void     tc_post_remote_buttons(uint8_t mask);  // any task; applied mask = touch mask | remote mask
void     tc_get_frame(tc_frame_t *out);
#define TC_RAM_NIBBLES 0x300
// Any task. Copies up to n RAM nibbles (one per byte, 0-15, address order from 0x000) from the last
// published snapshot; returns the count (0 before the first publish, at most the build's RAM size).
size_t   tc_get_ram(uint8_t *out, size_t n);
bool     tc_frame_changed(uint32_t *seq);

// The CPU at the last publish, which also copies the frame and RAM (30 times per emulated second).
typedef struct { uint8_t factor, mask; } tc_irq_t;
typedef struct {
    uint32_t seq;          // publish count, as tc_frame_changed counts
    uint32_t tick;         // tick counter at the publish
    uint16_t pc;           // 13 bits: bank (bit 12), page (11-8), step (7-0)
    uint16_t x, y;         // 12 bits each: P (11-8), H (7-4), L (3-0)
    uint8_t a, b, np, sp, flags;  // flags: bit 0 C, 1 Z, 2 D, 3 I
    bool halted;
    uint32_t call_depth;
    tc_irq_t irq[6];       // tamalib's slot order: prog timer, serial, K10-K13, K00-K03, stopwatch, clock timer
    uint8_t buttons;       // the mask applied to tamalib (touch | remote)
} tc_cpu_t;
// Any task. One instant: the CPU, frame and up to n RAM nibbles from the same publish. Returns the nibbles
// copied (0 before the first publish; cpu and frame untouched then). cpu, frame or ram may be NULL.
size_t   tc_get_snapshot(tc_cpu_t *cpu, tc_frame_t *frame, uint8_t *ram, size_t n);
void     tc_get_tone(tc_tone_t *out);
uint32_t tc_tick_counter(void);
size_t   tc_state_size(void);
size_t   tc_state_save(uint8_t *buf, size_t cap);
bool     tc_state_load(const uint8_t *buf, size_t len);
// Version byte and length match this build; no load. Any task, after tc_init.
bool     tc_state_compatible(const uint8_t *buf, size_t len);

typedef bool (*tc_catchup_cb)(uint64_t done_ticks, uint64_t total_ticks, void *ctx);  // return false to stop early
// Runs at full speed until `ticks` emulated ticks have passed (wrap-safe), calling cb every
// cb_every_ticks (0 = never). Restores real-time mode. Returns ticks actually run.
uint64_t tc_catchup(uint64_t ticks, tc_catchup_cb cb, void *ctx, uint32_t cb_every_ticks);

uint8_t  tc_applied_buttons(void);  // emulator task: the mask currently applied to tamalib

// Like tc_catchup, then replays the wall time each pass took, until less than one second is left,
// so the pet ends up at the current time. cb sees cumulative done and the growing total.
uint64_t tc_catchup_converge(uint64_t ticks, uint64_t (*wall_us)(void), tc_catchup_cb cb, void *ctx,
                             uint32_t cb_every_ticks);

// The pet's clock (RAM 0x10-0x15). Asks the emulator task to set it to the local time ms_of_day (0-86399999,
// the time now); which models have a clock there is the caller's business (tc_pet_clock). The emulator task
// accepts the request at its next instruction and stamps the tick counter, then waits for the ROM's next seconds
// tick (0x10 changing) and writes TC_CLOCK_QUIET_TICKS after it, when the ROM's carry into 0x11-0x15 (~150 ticks)
// is long done: the real second at the moment of that tick, rounded to the nearest (within half a second).
// No tick within TC_CLOCK_TIMEOUT_TICKS (the ROM's own clock menu stops the clock): dropped as busy. A newer
// request replaces a pending one, and an older id then reports the newer one's state and result (one write).
#define TC_CLOCK_QUIET_TICKS   1638u   // ~50 ms
#define TC_CLOCK_TIMEOUT_TICKS 65536u  // 2 s
typedef enum { TC_CLOCK_PENDING, TC_CLOCK_APPLIED, TC_CLOCK_BUSY } tc_clock_state_t;
uint32_t tc_clock_request_ms(uint32_t ms_of_day);  // returns the request id, never 0
// APPLIED fills *written_s (may be NULL) with the value written. BUSY: timed out, or an unknown id.
tc_clock_state_t tc_clock_poll(uint32_t id, uint32_t *written_s);

#ifdef TC_TESTING
uint8_t  tc_debug_applied_buttons(void);
void     tc_debug_set_tick_source(uint32_t (*fn)(void));  // NULL = tamalib's counter
void     tc_debug_set_step_hook(void (*fn)(void));        // called after every tamalib step
#endif
