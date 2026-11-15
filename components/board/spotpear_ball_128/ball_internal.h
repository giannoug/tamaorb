// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "esp_err.h"
esp_err_t ball_display_init(void);
esp_err_t ball_touch_init(void);
esp_err_t ball_audio_init(void);

// Set to 1 if touches on the left of the screen report x > 120 (checked on hardware).
#define TOUCH_MIRROR_X 0
