// SPDX-License-Identifier: GPL-3.0-or-later
// The Pet clock action: set the emulated pet's clock to the ball's local time (the tz setting), on request.
// The one place its rules live; the UI task, the remote API and USB all call these.
#pragma once
#include <stdint.h>
#include "tc_rom.h"

typedef enum { PET_CLOCK_OK, PET_CLOCK_UNSUPPORTED, PET_CLOCK_NO_TIME, PET_CLOCK_CATCHUP, PET_CLOCK_BUSY,
               PET_CLOCK_PENDING } pet_clock_status_t;

void pet_clock_init(tc_model_t model);
// OK, or why the action isn't available: UNSUPPORTED (no known clock), CATCHUP (emulator not running), NO_TIME.
pet_clock_status_t pet_clock_available(void);
int32_t pet_clock_pet_s(void);   // the pet's clock from the RAM snapshot, seconds of the day; -1 unknown
int32_t pet_clock_ball_s(void);  // the ball's local time of day; -1 without trusted time
// Posts the request (available() first; anything but OK is returned and nothing is posted).
pet_clock_status_t pet_clock_start(uint32_t *id);
// PENDING, OK (applied: *written_s set, a save requested) or BUSY. Any task; never blocks.
pet_clock_status_t pet_clock_poll(uint32_t id, int32_t *written_s);
// start + poll every 20 ms, at most 3 s. For the remote API and USB, which may block.
pet_clock_status_t pet_clock_sync(int32_t *written_s);
// Any task. A one-shot task waits for EMU_RUN, then sets the pet's clock (up to 5 tries, 2 s apart). why "new pet":
// first presses the ROM's setup exit (tc_pet_setup_exit), which takes a fresh egg out of its clock-setting mode.
// why must be a string literal (it outlives the call).
void pet_clock_auto(const char *why);
