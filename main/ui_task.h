// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "tc_rom.h"

esp_err_t ui_init(void);                                   // framebuffer; call after board_init
void      ui_message(const char *title, const char *body); // full-screen message, backlight on
// First start: blocking touch list of the valid entries (one per distinct dump); a tap picks. Call before
// ui_task_start. Returns the chosen index into roms[], or -1 if there is nothing to pick.
int       ui_pick_rom(const tc_rom_entry_t *roms, int n);
void      ui_task_start(tc_model_t model);                 // touch → buttons, screen timeout, rendering
void      ui_task_show_low_battery(void);                  // any task: low-battery screen until reset
