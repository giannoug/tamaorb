// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "tc_rom.h"

// Starts the UI task (core 0): touch → buttons, screen timeout, rendering.
void ui_task_start(tc_model_t model);
