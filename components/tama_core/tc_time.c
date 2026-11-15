// SPDX-License-Identifier: GPL-3.0-or-later
#include "tc_time.h"

uint32_t tc_ticks_since(uint32_t start, uint32_t now) { return now - start; }

bool tc_gap_seconds(int64_t saved_unix, bool saved_trusted, int64_t now_unix, bool now_trusted, uint64_t *gap_s)
{
    if (!saved_trusted || !now_trusted) return false;
    *gap_s = now_unix > saved_unix ? (uint64_t)(now_unix - saved_unix) : 0;
    return true;
}
