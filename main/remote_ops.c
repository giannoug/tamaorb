// SPDX-License-Identifier: GPL-3.0-or-later
#include "remote_ops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "emu_task.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "net.h"
#include "power.h"
#include "press_task.h"
#include "rom_store.h"
#include "rom_switch.h"
#include "saver.h"
#include "settings_store.h"

static const char *TAG = "remote";

extern const char s_guide_p1[] asm("_binary_guide_p1_md_start");  // EMBED_TXTFILES: NUL-terminated
extern const char s_guide_p2[] asm("_binary_guide_p2_md_start");

static tc_model_t s_model;
static uint8_t s_active[20];
static char s_rom_name[64];
static volatile bool s_restart_pending;  // name or ntp changed: applies after the next boot

static rm_phase_t phase_now(uint8_t *pct)
{
    emu_phase_t ph = emu_phase();
    *pct = 0;
    if (ph == EMU_RUN) return RM_PHASE_RUN;
    if (ph == EMU_BOOT) return RM_PHASE_BOOT;
    uint64_t done, total;
    uint32_t eta;
    emu_catchup_progress(&done, &total, &eta);
    *pct = total ? (uint8_t)(done >= total ? 100 : done * 100 / total) : 0;
    return RM_PHASE_CATCHUP;
}

static void op_look(void *ctx, rm_look_t *out)
{
    memset(out, 0, sizeof *out);
    tc_get_frame(&out->frame);
    tc_frame_changed(&out->seq);  // from 0: stores the current frame number
    tc_tone_t t;
    tc_get_tone(&t);
    out->buzzer = t.on;
    out->model = s_model;
    out->phase = phase_now(&out->catchup_pct);
}

static rm_err_t op_press(void *ctx, const rm_press_req_t *req) { return press_run(req); }

static void op_status(void *ctx, rm_status_t *st)
{
    memset(st, 0, sizeof *st);
    st_settings_t cfg;
    settings_get(&cfg);
    snprintf(st->name, sizeof st->name, "%s", cfg.name);
    snprintf(st->version, sizeof st->version, "%s", esp_app_get_description()->version);
    snprintf(st->rom_name, sizeof st->rom_name, "%s", s_rom_name);
    wifi_ap_record_t ap;
    st->wifi = net_connected();
    if (st->wifi && esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
        snprintf(st->wifi_ssid, sizeof st->wifi_ssid, "%.32s", (const char *)ap.ssid);
    net_ip(st->ip);
    st->rssi = net_rssi();
    st->model = s_model;
    st->phase = phase_now(&st->catchup_pct);
    st->uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
    st->batt_mv = board_battery_mv();
    st->batt_pct = power_battery_pct(st->batt_mv);
    st->usb = board_usb_present();
    st->heap_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    st->heap_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    st->time_trusted = net_now(&st->unix_time);
    st->sd_present = rom_store_sd_present();
    st->restart_pending = s_restart_pending;
    ps_meta_t m;
    if (s_model != TC_MODEL_UNKNOWN && saver_read_newest(s_active, &m, NULL, 0, NULL) == ESP_OK && m.time_trusted) {
        st->last_save_known = true;
        st->last_save_unix = m.unix_time;
    }
    st->catchup_speed = emu_catchup_speed();
}

static void op_get_settings(void *ctx, st_settings_t *out) { settings_get(out); }

static void op_put_settings(void *ctx, const st_settings_t *s, bool display_changed)
{
    st_settings_t old;
    settings_get(&old);
    if (strcmp(old.name, s->name) || strcmp(old.ntp, s->ntp)) s_restart_pending = true;
    settings_put(s, display_changed);
}

static void fill_save(rm_pet_t *p)
{
    ps_meta_t m;
    p->has_save = saver_read_newest(p->sha1, &m, NULL, 0, NULL) == ESP_OK;
    p->save_trusted = p->has_save && m.time_trusted;
    p->save_unix = p->has_save ? m.unix_time : 0;
}

static int op_list_pets(void *ctx, rm_pet_t *out, int max)
{
    tc_rom_entry_t *roms = malloc(sizeof *roms * 16);
    if (!roms) return 0;
    int idx[16];
    int n = rom_store_scan(roms, 16);
    int m = tc_rom_distinct_valid(roms, n, idx, 16), k = 0;
    bool active_listed = false;
    for (int i = 0; i < m && k < max; i++, k++) {
        const tc_rom_entry_t *r = &roms[idx[i]];
        rm_pet_t *p = &out[k];
        memset(p, 0, sizeof *p);
        snprintf(p->name, sizeof p->name, "%s", r->name);
        p->model = r->model;
        memcpy(p->sha1, r->sha1, 20);
        p->active = s_model != TC_MODEL_UNKNOWN && !memcmp(r->sha1, s_active, 20);
        active_listed |= p->active;
        fill_save(p);
    }
    free(roms);
    // Running from the cached copy with the card out (or the file gone): still list it, without a file name.
    if (!active_listed && s_model != TC_MODEL_UNKNOWN && k < max) {
        rm_pet_t *p = &out[k++];
        memset(p, 0, sizeof *p);
        p->model = s_model;
        memcpy(p->sha1, s_active, 20);
        p->active = true;
        fill_save(p);
    }
    return k;
}

static rm_err_t op_switch_pet(void *ctx, const char *name)
{
    switch (rom_switch_to(name)) {
    case ESP_OK: return RM_OK;
    case ESP_ERR_NOT_FOUND: return RM_ERR_NOTFOUND;
    case ESP_ERR_INVALID_STATE: return RM_ERR_CATCHUP;
    case ESP_ERR_NOT_ALLOWED: return RM_ERR_BUSY;
    default: return RM_ERR_IO;
    }
}

static rm_err_t op_export_save(void *ctx, const uint8_t sha1[20], uint8_t *out, size_t cap, size_t *len, ps_meta_t *meta)
{
    esp_err_t err = saver_read_newest(sha1, meta, out, cap, len);
    if (err == ESP_OK) return RM_OK;
    return err == ESP_ERR_NOT_FOUND ? RM_ERR_NOTFOUND : RM_ERR_IO;
}

// Spec §7.3. The record lands as that pet's newest (seq = newest + 1) in the older A/B slot. For the running
// pet the saver freezes first, so nothing saves over it, and the ball restarts straight into it.
static rm_err_t op_import_save(void *ctx, const uint8_t sha1[20], uint8_t *rec, size_t len, rm_restore_mode_t mode)
{
    ps_meta_t meta;
    if (rm_save_check(rec, len, sha1, tc_state_compatible, &meta)) return RM_ERR_BAD;
    bool running = s_model != TC_MODEL_UNKNOWN && !memcmp(sha1, s_active, 20);
    emu_phase_t ph = emu_phase();
    // Rejected while catching up, like the picker; the running pet also not before its save has loaded.
    if (ph == EMU_CATCHUP || (running && ph != EMU_RUN)) return RM_ERR_CATCHUP;
    esp_err_t err = ESP_ERR_INVALID_STATE;
    for (int attempt = 0; attempt < 3 && err == ESP_ERR_INVALID_STATE; attempt++) {
        ps_meta_t newest;
        uint32_t seq = saver_read_newest(sha1, &newest, NULL, 0, NULL) == ESP_OK ? newest.seq : 0;
        bool ok;
        if (mode == RM_RESTORE_FROZEN) {
            int64_t now = 0;
            bool trusted = net_now(&now);  // untrusted: stored as such, so no catch-up later either
            ok = ps_record_restamp(rec, len, &now, trusted, seq + 1);
        } else {
            ok = ps_record_restamp(rec, len, NULL, false, seq + 1);
        }
        if (!ok) return RM_ERR_BAD;
        err = saver_import(sha1, rec, len);  // ESP_ERR_INVALID_STATE: a save landed in between
    }
    if (err != ESP_OK) return RM_ERR_IO;
    ESP_LOGW(TAG, "save restored (%s)%s", mode == RM_RESTORE_FROZEN ? "frozen" : "replay",
             running ? "; restarting into it" : "");
    if (running) app_restart_after(1000);  // no save first: the saver is frozen
    return RM_OK;
}

static void save_then_restart(void)
{
    if (!saver_request_and_wait(3000)) ESP_LOGW(TAG, "save before restart failed or skipped");
    app_restart_after(1000);
}

static bool op_set_wifi(void *ctx, const char *ssid, const char *pass)
{
    if (net_set_credentials(ssid, pass) != ESP_OK) return false;
    save_then_restart();  // net_start reads the credentials at boot
    return true;
}

static void op_restart(void *ctx) { save_then_restart(); }

static const char *op_guide(void *ctx, const char *model)
{
    if (!model) return NULL;
    if (!strcmp(model, "p1")) return s_guide_p1;
    if (!strcmp(model, "p2")) return s_guide_p2;
    return NULL;
}

static rm_ops_t s_ops = {
    .look = op_look, .press = op_press, .status = op_status, .get_settings = op_get_settings,
    .put_settings = op_put_settings, .list_pets = op_list_pets, .switch_pet = op_switch_pet,
    .export_save = op_export_save, .import_save = op_import_save, .set_wifi = op_set_wifi,
    .restart = op_restart, .guide = op_guide, .ctx = NULL,
};

const rm_ops_t *remote_ops_init(tc_model_t model, const uint8_t active_sha1[20])
{
    s_model = model;
    if (active_sha1) memcpy(s_active, active_sha1, 20);
    tc_rom_entry_t *roms = malloc(sizeof *roms * 16);
    if (roms) {
        int n = rom_store_scan(roms, 16);
        for (int i = 0; i < n; i++)
            if (!memcmp(roms[i].sha1, s_active, 20)) {
                snprintf(s_rom_name, sizeof s_rom_name, "%s", roms[i].name);
                break;
            }
        free(roms);
    }
    return &s_ops;
}
