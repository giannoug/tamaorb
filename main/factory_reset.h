// SPDX-License-Identifier: GPL-3.0-or-later
// Factory reset: every pet, save, name, setting and the Wi-Fi credentials go (both NVS partitions), and the
// "time is trusted" marker; ROM files on the SD card stay. The erase runs at the start of the next boot, before
// anything opens NVS, so no task is writing while the partitions go.
#pragma once
#include <stdbool.h>
#include "esp_err.h"

// Any task. Stops all saving, leaves a request marker in RTC memory and restarts in 500 ms. The running pet is not
// saved. ESP_ERR_NOT_ALLOWED: a restart is already under way.
esp_err_t factory_reset_request(void);
// app_main, first thing: if the marker is set, erases both partitions and forgets the time, then clears the
// marker (a power cut mid-erase finishes the erase at the next boot). True if it erased.
bool      factory_reset_boot(void);
