// SPDX-License-Identifier: GPL-3.0-or-later
#include "tc_time.h"

uint32_t tc_ticks_since(uint32_t start, uint32_t now) { return now - start; }

bool tc_gap_seconds(int64_t saved_unix, bool saved_trusted, int64_t now_unix, bool now_trusted, uint64_t *gap_s)
{
    if (!saved_trusted || !now_trusted) return false;
    *gap_s = now_unix > saved_unix ? (uint64_t)(now_unix - saved_unix) : 0;
    return true;
}

int64_t tc_progress_unix(int64_t saved_unix, uint64_t done_ticks)
{
    return saved_unix + (int64_t)(done_ticks / TC_TICK_HZ);
}

tc_resume_t tc_resume_rule(bool gap_known, uint64_t gap_s)
{
    if (!gap_known || gap_s == 0) return TC_RESUME_NONE;
    return gap_s < TC_ASK_GAP_S ? TC_RESUME_SILENT_FF : TC_RESUME_ASK;
}

uint32_t tc_ff_estimate_s(uint64_t gap_s, float speed)
{
    if (speed <= 0) speed = 75.0f;
    double s = (double)gap_s / speed;
    uint64_t r = (uint64_t)s;
    if ((double)r < s) r++;
    return r > UINT32_MAX ? UINT32_MAX : (uint32_t)r;
}
