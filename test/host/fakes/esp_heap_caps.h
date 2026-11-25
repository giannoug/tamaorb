// SPDX-License-Identifier: GPL-3.0-or-later
// Host fake of esp_heap_caps.h: plain malloc.
#pragma once
#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_INTERNAL (1u << 11)
#define MALLOC_CAP_8BIT     (1u << 2)
void *heap_caps_malloc(size_t size, uint32_t caps);
