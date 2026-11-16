// SPDX-License-Identifier: GPL-3.0-or-later
// Runs agent button sequences with on-ball timing.
#pragma once
#include <stdbool.h>
#include "remote.h"

void     press_task_start(void);                // core 0; ticks every 5 ms while a sequence or its settle runs
// Any task but the press task: runs req and blocks until it and its settle time are over (<= ~10 s).
// RM_ERR_BAD if req fails rm_press_validate, RM_ERR_BUSY if a sequence runs, RM_ERR_CATCHUP unless EMU_RUN.
rm_err_t press_run(const rm_press_req_t *req);
bool     press_window_active(void);             // an agent sequence or its settle time is running now
extern volatile bool g_agent_mute;              // window active && agent_feedback == silent (read by the emulator)
