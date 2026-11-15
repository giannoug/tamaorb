// SPDX-License-Identifier: GPL-3.0-or-later
// Types for tamalib, from upstream hal_types.h.template.
// Upstream hal.h includes "../hal_types.h", so this file sits beside upstream/.
#ifndef _HAL_TYPES_H_
#define _HAL_TYPES_H_

#include <stdint.h>

typedef uint8_t  bool_t;
typedef uint8_t  u4_t;
typedef uint8_t  u5_t;
typedef uint8_t  u8_t;
typedef uint16_t u12_t;
typedef uint16_t u13_t;
typedef uint32_t u32_t;
typedef uint32_t timestamp_t; // microseconds; wraps every ~71 min, tamalib handles the wrap

#endif /* _HAL_TYPES_H_ */
