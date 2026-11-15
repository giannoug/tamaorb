// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool emu_init(const uint16_t *program);  // tama_core init; restore a save between this and emu_start()
void emu_start(void);                    // emulator task on core 1, priority 10
