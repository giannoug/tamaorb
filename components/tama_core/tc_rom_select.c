// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "tc_rom.h"

static bool valid(tc_model_t m) { return m == TC_MODEL_P1 || m == TC_MODEL_P2; }

tc_sel_action_t tc_rom_select(bool have_cached, const tc_rom_entry_t *sd, int n_sd, int *sd_index_out)
{
    if (have_cached) return TC_SEL_USE_CACHED;
    int first = -1, distinct = 0;
    for (int i = 0; i < n_sd; i++) {
        if (!valid(sd[i].model)) continue;
        bool dup = false;
        for (int j = 0; j < i && !dup; j++) dup = valid(sd[j].model) && memcmp(sd[i].sha1, sd[j].sha1, 20) == 0;
        if (dup) continue;
        if (first < 0) first = i;
        distinct++;
    }
    if (distinct == 0) return TC_SEL_NO_ROM;
    if (distinct > 1) return TC_SEL_PICKER;
    *sd_index_out = first;
    return TC_SEL_USE_SD;
}
