// SPDX-License-Identifier: GPL-3.0-or-later
// Tick and wall-clock arithmetic. Pure functions, portable.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define TC_TICK_HZ 32768u

// Wrap-safe ticks elapsed from start to now (tamalib's 32-bit counter wraps every ~36.4 h).
uint32_t tc_ticks_since(uint32_t start, uint32_t now);
// Gap to replay after an outage. Returns false when either time is untrusted
// (resume frozen). A clock that went backwards yields a gap of 0.
bool     tc_gap_seconds(int64_t saved_unix, bool saved_trusted, int64_t now_unix, bool now_trusted, uint64_t *gap_s);
// Wall-clock time a catch-up progress save stands for: the original save time plus the
// emulated time replayed so far.
int64_t  tc_progress_unix(int64_t saved_unix, uint64_t done_ticks);

// When a pet comes back with a gap: nothing (time unknown, or no gap), fast-forward without asking (under a
// minute: a restart), or ask the owner (Resume / Fast-forward).
#define TC_ASK_GAP_S 60u
typedef enum { TC_RESUME_NONE, TC_RESUME_SILENT_FF, TC_RESUME_ASK } tc_resume_t;
tc_resume_t tc_resume_rule(bool gap_known, uint64_t gap_s);
// Seconds a fast-forward of gap_s takes at speed (x real time), rounded up; 75x when speed isn't known (<= 0).
uint32_t    tc_ff_estimate_s(uint64_t gap_s, float speed);
