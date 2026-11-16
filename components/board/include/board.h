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

uint32_t  board_battery_mv(void);
bool      board_usb_present(void);

esp_err_t board_sd_mount(const char *mount_point);
