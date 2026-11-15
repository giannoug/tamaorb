// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

// Initialises tama_core with the program and starts the emulator task (core 1, priority 10).
void emu_task_start(const uint16_t *program);
