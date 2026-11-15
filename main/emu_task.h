// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { EMU_BOOT, EMU_CATCHUP, EMU_RUN } emu_phase_t;

bool emu_init(const uint16_t *program);  // tama_core init; restore a save between this and emu_start()
// Starts the emulator task (core 1, priority 10). With catchup_s > 0 it first replays that many
// seconds at full speed (spec §6.4), saving progress every 2 minutes stamped with virtual time.
void emu_start(uint64_t catchup_s, int64_t saved_unix, bool saved_trusted);
emu_phase_t emu_phase(void);
// While catching up: seconds replayed, total, and estimated seconds left.
void emu_catchup_progress(uint64_t *done_s, uint64_t *total_s, uint32_t *eta_s);
