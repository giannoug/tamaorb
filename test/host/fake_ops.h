// SPDX-License-Identifier: GPL-3.0-or-later
// Scripted rm_ops_t for the MCP and REST tests.
#pragma once
#include "remote.h"

typedef struct {
    st_settings_t settings;
    rm_status_t status;
    rm_pet_t pets[2];
    // press
    rm_err_t press_result;
    rm_press_req_t press_seen;
    int press_calls;
    // settings
    int put_calls;
    bool put_display;
    // pets / misc
    rm_err_t switch_result;
    char switched_to[64];   // "<sha1 hex>-<id>"
    int switch_calls;
    rm_err_t new_result;
    uint8_t new_sha1[20];
    int new_calls;
    rm_err_t rename_result;
    char renamed_to[32];
    int renamed_id, rename_calls;
    rm_err_t delete_result;
    int deleted_id, delete_calls;
    int export_id, import_id;
    rm_err_t resume_result, skip_result;
    char resume_choice[16];
    int skip_calls;
    rm_err_t export_result;
    uint8_t export_data[64];
    size_t export_len;
    ps_meta_t export_meta;
    rm_err_t import_result;
    rm_restore_mode_t import_mode;
    size_t import_len;
    int import_calls;
    bool wifi_result;
    char wifi_ssid[64], wifi_pass[64];
    int wifi_calls, restart_calls;
    int status_calls, restart_pending_calls;
    // pet clock
    rm_err_t sync_result;
    int32_t sync_written;
    int sync_calls;
    // debugger
    rm_debug_t debug;
    int debug_calls;
    uint8_t rom[12288];
    bool have_rom;
} fake_state_t;

extern fake_state_t fake;
extern rm_ops_t fake_ops;
void fake_reset(void);
