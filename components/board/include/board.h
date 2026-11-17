// SPDX-License-Identifier: GPL-3.0-or-later
// Board interface: the only layer that knows pins and chips. One implementation per device.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t board_init(void);                        // power hold, amp off, buses, display, backlight
void      board_display_flush(const uint16_t *fb); // 240x240 RGB565, host byte order; blocks until sent
void      board_display_flush_rows(const uint16_t *fb, int y0, int y1);  // only rows [y0, y1) of the same frame
void      board_backlight_set(uint8_t pct);        // 0 = off

typedef struct { uint8_t count; struct { int16_t x, y; } pt[2]; } board_touch_t;
bool      board_touch_read(board_touch_t *t);      // false on I2C error (t->count = 0)
bool      board_boot_down(void);

void      board_tone(uint32_t freq_hz, bool on);
void      board_volume_set(uint8_t pct);
// Screen off (true): the speaker (I2S channel and amp) switches off once nothing has sounded for BOARD_AUDIO_HOLD_MS,
// so the ball can light-sleep (an enabled I2S channel blocks it), and back on at board_audio_prepare or a tone-on.
// Screen on (false, the default): on whenever not muted, as before. Any task.
#define BOARD_AUDIO_HOLD_MS 3000
void      board_audio_idle_off(bool allow);
// A beep is due within ~20 ms: start the speaker now, so the amplifier's start-up overlaps the wait. No-op when muted.
void      board_audio_prepare(void);

uint32_t  board_battery_mv(void);
bool      board_usb_present(void);

esp_err_t board_sd_mount(const char *mount_point);
