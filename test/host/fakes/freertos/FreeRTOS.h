// SPDX-License-Identifier: GPL-3.0-or-later
// Host fake of FreeRTOS.h: the tests are single-threaded, so a mutex is a token.
#pragma once
#include <stdint.h>
typedef void *SemaphoreHandle_t;
typedef uint32_t TickType_t;
typedef int BaseType_t;
#define portMAX_DELAY 0xFFFFFFFFu
#define pdTRUE  1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) (ms)
