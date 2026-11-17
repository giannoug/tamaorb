// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>
#include <string.h>
#include "fake_ops.h"

fake_state_t fake;

static void f_look(void *ctx, rm_look_t *out)
{
    memset(out, 0, sizeof *out);
    out->model = TC_MODEL_P1;
    out->phase = RM_PHASE_RUN;
    out->frame.rows[0] = 0x1;
    out->frame.icons = 0x1;
    out->seq = 7;
}
static rm_err_t f_press(void *ctx, const rm_press_req_t *req)
{
    fake.press_seen = *req;
    fake.press_calls++;
    return fake.press_result;
}
static void f_status(void *ctx, rm_status_t *out)
{
    fake.status_calls++;
    *out = fake.status;
}
static void f_get(void *ctx, st_settings_t *out) { *out = fake.settings; }
static void f_put(void *ctx, const st_settings_t *s, bool display)
{
    fake.settings = *s;
    fake.put_calls++;
    fake.put_display = display;
}
static int f_pets(void *ctx, rm_pet_t *out, int max)
{
    int n = max < 2 ? max : 2;
    for (int i = 0; i < n; i++) out[i] = fake.pets[i];
    return n;
}
static rm_err_t f_switch(void *ctx, const char *name)
{
    fake.switch_calls++;
    snprintf(fake.switched_to, sizeof fake.switched_to, "%s", name);
    return fake.switch_result;
}
static rm_err_t f_export(void *ctx, const uint8_t sha1[20], uint8_t *out, size_t cap, size_t *len, ps_meta_t *meta)
{
    if (fake.export_result != RM_OK) return fake.export_result;
    if (cap < fake.export_len) return RM_ERR_IO;
    memcpy(out, fake.export_data, fake.export_len);
    *len = fake.export_len;
    *meta = fake.export_meta;
    return RM_OK;
}
static rm_err_t f_import(void *ctx, const uint8_t sha1[20], uint8_t *rec, size_t len, rm_restore_mode_t mode)
{
    fake.import_calls++;
    fake.import_len = len;
    fake.import_mode = mode;
    return fake.import_result;
}
static bool f_wifi(void *ctx, const char *ssid, const char *pass)
{
    fake.wifi_calls++;
    snprintf(fake.wifi_ssid, sizeof fake.wifi_ssid, "%s", ssid);
    snprintf(fake.wifi_pass, sizeof fake.wifi_pass, "%s", pass);
    return fake.wifi_result;
}
static void f_restart(void *ctx) { fake.restart_calls++; }
static const char *f_guide(void *ctx, const char *model)
{
    return strcmp(model, "p1") == 0 ? "# P1 guide" : NULL;
}

static bool f_restart_pending(void *ctx)
{
    fake.restart_pending_calls++;
    return fake.status.restart_pending;
}

static rm_err_t f_sync_clock(void *ctx, int32_t *written_s)
{
    fake.sync_calls++;
    if (fake.sync_result == RM_OK) *written_s = fake.sync_written;
    return fake.sync_result;
}
rm_ops_t fake_ops = {.look = f_look, .press = f_press, .status = f_status, .get_settings = f_get, .put_settings = f_put,
                     .list_pets = f_pets, .switch_pet = f_switch, .export_save = f_export, .import_save = f_import,
                     .set_wifi = f_wifi, .restart = f_restart, .guide = f_guide, .restart_pending = f_restart_pending,
                     .sync_clock = f_sync_clock, .ctx = NULL};

void fake_reset(void)
{
    memset(&fake, 0, sizeof fake);
    st_defaults(&fake.settings, 50);
    fake.settings.brightness = 80;
    snprintf(fake.status.name, sizeof fake.status.name, "tamaorb");
    snprintf(fake.status.version, sizeof fake.status.version, "test");
    snprintf(fake.status.rom_name, sizeof fake.status.rom_name, "tama.bin");
    snprintf(fake.status.ip, sizeof fake.status.ip, "10.0.0.2");
    fake.status.model = TC_MODEL_P1;
    fake.status.phase = RM_PHASE_RUN;
    fake.status.catchup_speed = 24.0f;
    fake.status.pet_clock_s = -1;
    fake.status.ball_clock_s = -1;
    snprintf(fake.pets[0].name, sizeof fake.pets[0].name, "tama.bin");
    fake.pets[0].model = TC_MODEL_P1;
    fake.pets[0].active = true;
    for (int i = 0; i < 20; i++) fake.pets[0].sha1[i] = (uint8_t)(0xA0 + i);
    snprintf(fake.pets[1].name, sizeof fake.pets[1].name, "other.bin");
    fake.pets[1].model = TC_MODEL_P2;
    fake.pets[1].has_save = true;
    fake.pets[1].save_trusted = true;
    fake.pets[1].save_unix = 1700000000;
    for (int i = 0; i < 20; i++) fake.pets[1].sha1[i] = (uint8_t)i;
    fake_ops.ctx = NULL;
}
