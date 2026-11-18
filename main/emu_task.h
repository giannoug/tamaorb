// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

// WAIT: the pet came back after a gap of a minute or more and waits for Resume or Fast-forward; it doesn't run.
typedef enum { EMU_BOOT, EMU_WAIT, EMU_CATCHUP, EMU_RUN } emu_phase_t;
typedef enum { EMU_ANSWER_RESUME, EMU_ANSWER_FF } emu_answer_t;

bool emu_init(const uint16_t *program);  // tama_core init; restore a save between this and emu_start()
// Starts the emulator task (core 1, priority 10). With catchup_s > 0 it first replays that many
// seconds at full speed, saving progress every 2 minutes stamped with virtual time. ask: it first waits in
// EMU_WAIT for emu_answer (Resume: no replay, saved at once with the current time, then the pet clock is set;
// Fast-forward: the replay).
void emu_start(uint64_t catchup_s, int64_t saved_unix, bool saved_trusted, bool ask);
bool emu_answer(emu_answer_t a);   // any task; false unless EMU_WAIT, or when answered already (the first answer wins)
// Any task; false unless EMU_CATCHUP. The replay stops at its next callback; the pet goes on from there, saved
// with the current time, and its clock is set.
bool emu_skip(void);
uint64_t emu_wait_gap_s(void);     // the gap the question is about; 0 when not waiting
emu_phase_t emu_phase(void);
// While catching up: seconds replayed, total, and estimated seconds left.
void emu_catchup_progress(uint64_t *done_s, uint64_t *total_s, uint32_t *eta_s);
// Last measured catch-up speed (x real time) this boot, else 24.0f (the measured v1 figure).
float emu_catchup_speed(void);
