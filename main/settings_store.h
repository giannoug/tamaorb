// SPDX-License-Identifier: GPL-3.0-or-later
// Device settings: RAM copy, NVS persistence (namespace "settings", one string per key).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "settings.h"

// After nvs_flash_init: loads defaults plus stored values and starts the writer task (core 0).
esp_err_t settings_init(void);
void settings_get(st_settings_t *out);
// Stores in RAM, schedules the NVS write, bumps the sequence numbers.
void settings_put(const st_settings_t *s, bool display_changed);
uint32_t settings_seq(void);          // bumps on every put
uint32_t settings_display_seq(void);  // bumps only when display_changed
