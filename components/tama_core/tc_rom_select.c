// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "tc_rom.h"

int tc_rom_distinct_valid(const tc_rom_entry_t *sd, int n_sd, int *idx_out, int max)
{
    int n = 0;
    for (int i = 0; i < n_sd && n < max; i++) {
        if (!tc_model_playable(sd[i].model)) continue;
        bool dup = false;
        for (int k = 0; k < n && !dup; k++) dup = memcmp(sd[i].sha1, sd[idx_out[k]].sha1, 20) == 0;
        if (!dup) idx_out[n++] = i;
    }
    return n;
}

tc_sel_action_t tc_rom_select(bool have_cached, const tc_rom_entry_t *sd, int n_sd, int *sd_index_out)
{
    if (have_cached) return TC_SEL_USE_CACHED;
    int idx[2];
    int distinct = tc_rom_distinct_valid(sd, n_sd, idx, 2);
    if (distinct == 0) return TC_SEL_NO_ROM;
    if (distinct > 1) return TC_SEL_PICKER;
    *sd_index_out = idx[0];
    return TC_SEL_USE_SD;
}
