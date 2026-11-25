// SPDX-License-Identifier: GPL-3.0-or-later
// Host fake of esp_log.h: the arguments are evaluated (no unused warnings), nothing is printed.
#pragma once
void fake_log(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#define ESP_LOGE(tag, fmt, ...) fake_log(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) fake_log(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) fake_log(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) fake_log(tag, fmt, ##__VA_ARGS__)
