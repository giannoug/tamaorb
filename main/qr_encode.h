// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdbool.h>
#include "ui.h"

// Encodes text as a QR code (ECC medium, up to version 4) with espressif/qrcode into out.
// Returns true with out filled; false with out->size = 0 if it doesn't fit or encoding fails.
bool qr_encode(const char *text, ui_qr_t *out);
