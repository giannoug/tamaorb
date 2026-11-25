// SPDX-License-Identifier: GPL-3.0-or-later
// Host fake of esp_system.h.
#pragma once
void esp_restart(void);  // counts the call (fake_restarts) and returns
