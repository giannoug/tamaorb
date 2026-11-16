// SPDX-License-Identifier: GPL-3.0-or-later
// Delayed restarts and the ROM switch shared by the on-device picker and the remote API.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// Any task: esp_restart() after ms (esp_timer one-shot), so a reply can go out first. Later calls are ignored.
void      app_restart_after(uint32_t ms);
bool      app_restart_pending(void);
// Any task, draws nothing. Saves the running pet (<= 3 s), reads that SD ROM, makes it the active ROM,
// then app_restart_after(1000). ESP_ERR_INVALID_STATE: not in EMU_RUN. ESP_ERR_NOT_ALLOWED: another switch
// or a restart is under way. ESP_ERR_NOT_FOUND: no such P1/P2 ROM on the card, or unreadable.
// ESP_FAIL: the save failed (or saving is frozen by an import), nothing changed. Other codes: storage errors.
esp_err_t rom_switch_to(const char *sd_name);
