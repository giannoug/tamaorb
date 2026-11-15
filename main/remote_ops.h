// SPDX-License-Identifier: GPL-3.0-or-later
// The device side of the remote API: rm_ops_t on top of the emulator, saver, ROM store, net and power.
#pragma once
#include <stdint.h>
#include "remote.h"

// Call once the active ROM is known (and before the HTTP server starts). The table lives forever.
const rm_ops_t *remote_ops_init(tc_model_t model, const uint8_t active_sha1[20]);
