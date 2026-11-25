// SPDX-License-Identifier: GPL-3.0-or-later
// Host fake of ESP-IDF's esp_err.h: the codes main/ uses (values as in ESP-IDF).
#pragma once
typedef int esp_err_t;
#define ESP_OK                0
#define ESP_FAIL              (-1)
#define ESP_ERR_NO_MEM        0x101
#define ESP_ERR_INVALID_ARG   0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE  0x104
#define ESP_ERR_NOT_FOUND     0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_TIMEOUT       0x107
#define ESP_ERR_NOT_ALLOWED   0x10D
#define ESP_ERR_NVS_BASE             0x1100
#define ESP_ERR_NVS_NOT_FOUND        (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_NOT_ENOUGH_SPACE (ESP_ERR_NVS_BASE + 0x05)
const char *esp_err_to_name(esp_err_t err);
