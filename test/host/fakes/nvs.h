// SPDX-License-Identifier: GPL-3.0-or-later
// Host fake of ESP-IDF's nvs.h: the calls main/ makes, over an in-memory store (fake_nvs.c), with ESP-IDF's
// results on the edges: a missing key or (read-only) namespace is ESP_ERR_NVS_NOT_FOUND, an iterator over
// nothing is NULL with ESP_ERR_NVS_NOT_FOUND, nvs_entry_next past the end frees it and sets it NULL.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#define NVS_KEY_NAME_MAX_SIZE 16
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
typedef enum { NVS_TYPE_BLOB = 0x42, NVS_TYPE_ANY = 0xff } nvs_type_t;
typedef struct fake_nvs_it *nvs_iterator_t;
typedef struct {
    char namespace_name[NVS_KEY_NAME_MAX_SIZE];
    char key[NVS_KEY_NAME_MAX_SIZE];
    nvs_type_t type;
} nvs_entry_info_t;
esp_err_t nvs_open_from_partition(const char *part, const char *ns, nvs_open_mode_t mode, nvs_handle_t *out);
void      nvs_close(nvs_handle_t h);
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len);
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t len);
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key);
esp_err_t nvs_commit(nvs_handle_t h);
esp_err_t nvs_entry_find(const char *part, const char *ns, nvs_type_t type, nvs_iterator_t *out);
esp_err_t nvs_entry_next(nvs_iterator_t *it);
esp_err_t nvs_entry_info(nvs_iterator_t it, nvs_entry_info_t *out);
void      nvs_release_iterator(nvs_iterator_t it);
