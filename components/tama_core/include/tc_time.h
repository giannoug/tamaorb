// SPDX-License-Identifier: GPL-3.0-or-later
// Tick and wall-clock arithmetic. Pure functions, portable.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define TC_TICK_HZ 32768u

// Wrap-safe ticks elapsed from start to now (tamalib's 32-bit counter wraps every ~36.4 h).
uint32_t tc_ticks_since(uint32_t start, uint32_t now);
// Gap to replay after an outage (spec §6.4). Returns false when either time is untrusted
// (resume frozen). A clock that went backwards yields a gap of 0.
bool     tc_gap_seconds(int64_t saved_unix, bool saved_trusted, int64_t now_unix, bool now_trusted, uint64_t *gap_s);
// Wall-clock time a catch-up progress save stands for: the original save time plus the
// emulated time replayed so far (spec §6.4).
int64_t  tc_progress_unix(int64_t saved_unix, uint64_t done_ticks);
