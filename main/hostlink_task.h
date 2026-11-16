// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "tc_rom.h"

// Serves the hostlink protocol on the USB serial console.
void hostlink_task_start(tc_model_t active_model);
