// SPDX-License-Identifier: GPL-3.0-or-later
// Test controls for the in-memory NVS (fakes/nvs.h).
#pragma once
#include <stdbool.h>

void fake_nvs_reset(void);                             // empty store, no failures, counters at 0
void fake_nvs_fail_writes(bool fail);                  // nvs_set_blob returns ESP_ERR_NVS_NOT_ENOUGH_SPACE
int  fake_nvs_writes(void);                            // successful nvs_set_blob + nvs_erase_key calls
int  fake_nvs_reads(void);                             // nvs_get_blob + nvs_entry_find calls
bool fake_nvs_has(const char *ns, const char *key);    // in any partition
