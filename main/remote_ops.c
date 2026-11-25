// SPDX-License-Identifier: GPL-3.0-or-later
#include "remote_ops.h"
#include "tc_pet.h"
#include "tc_time.h"
#include "factory_reset.h"
#include "pet_store.h"
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
#include "pet_clock.h"
#include "power.h"
#include "press_task.h"
#include "rom_store.h"
#include "rom_switch.h"
#include "saver.h"
#include "settings_store.h"

static const char *TAG = "remote";

extern const char s_guide_p1[] asm("_binary_guide_p1_md_start");  // EMBED_TXTFILES: NUL-terminated
extern const char s_guide_p2[] asm("_binary_guide_p2_md_start");
extern const char s_guide_angel[] asm("_binary_guide_angel_md_start");
extern const char s_guide_mothra[] asm("_binary_guide_mothra_md_start");

static tc_model_t s_model;
static uint8_t s_active[20];
static char s_rom_name[64];
static const uint8_t *s_rom;
static size_t s_rom_len;

static rm_phase_t phase_now(uint8_t *pct)
{
    emu_phase_t ph = emu_phase();
    *pct = 0;
    if (ph == EMU_RUN) return RM_PHASE_RUN;
    if (ph == EMU_BOOT) return RM_PHASE_BOOT;
    if (ph == EMU_WAIT) return RM_PHASE_WAIT;
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
    st->restart_pending = settings_restart_pending();
    ps_meta_t m;
    uint8_t run_sha[20], run_id;
    if (saver_running(run_sha, &run_id) && saver_read_newest(run_sha, run_id, &m, NULL, 0, NULL) == ESP_OK &&
        m.time_trusted) {
        st->last_save_known = true;
        st->last_save_unix = m.unix_time;
    }
    st->catchup_speed = emu_catchup_speed();
    st->has_clock = tc_pet_has_clock(s_model);
    pet_ref_t run;
    if (saver_running(run.sha1, &run.id) && pet_store_name(&run, st->pet_name) != ESP_OK) st->pet_name[0] = 0;
    if (st->phase == RM_PHASE_WAIT) {
        st->wait_gap_s = emu_wait_gap_s();
        st->wait_ff_s = tc_ff_estimate_s(st->wait_gap_s, emu_catchup_speed());
        pet_ref_t prev;
        if (!pet_prev(&prev, st->back_name)) st->back_name[0] = 0;
    }
    st->pet_clock_s = pet_clock_pet_s();
    st->ball_clock_s = pet_clock_ball_s();
}

static void op_get_settings(void *ctx, st_settings_t *out) { settings_get(out); }

static void op_put_settings(void *ctx, const st_settings_t *s, bool display_changed) { settings_put(s, display_changed); }
static bool op_restart_pending(void *ctx) { return settings_restart_pending(); }

static bool is_running(const uint8_t sha1[20], uint8_t id)
{
    uint8_t run[20], rid;
    return saver_running(run, &rid) && rid == id && !memcmp(run, sha1, 20);
}

// Per call (rm_alloc): REST runs on httpd and MCP on its own worker, so two lists can overlap.
static int op_list_pets(void *ctx, rm_pet_t *out, int max)
{
    pet_info_t *pets = rm_alloc(sizeof *pets * PET_STORE_MAX);
    if (!pets) return 0;
    int n = pet_store_list(pets, PET_STORE_MAX), k = 0;
    for (int i = 0; i < n && k < max; i++, k++) {
        const pet_info_t *s = &pets[i];
        rm_pet_t *p = &out[k];
        memset(p, 0, sizeof *p);
        memcpy(p->sha1, s->ref.sha1, 20);
        p->id = s->ref.id;
        snprintf(p->name, sizeof p->name, "%s", s->name);
        p->model = s->model;
        p->active = is_running(s->ref.sha1, s->ref.id);
        p->has_save = s->has_save;
        p->save_trusted = s->save_trusted;
        p->save_unix = s->has_save ? s->save_unix : 0;
        p->rom_on_card = s->rom_on_card;
        p->egg = s->egg;
        p->dead = s->dead;
        p->age_years = s->age_years;
    }
    free(pets);
    return k;
}

static int op_list_roms(void *ctx, rm_rom_t *out, int max)
{
    tc_rom_entry_t *roms = malloc(sizeof *roms * 16);
    if (!roms) return 0;
    int idx[16];
    int n = rom_store_scan(roms, 16), m = tc_rom_distinct_valid(roms, n, idx, 16), k = 0;
    for (int i = 0; i < m && k < max; i++, k++) {
        memset(&out[k], 0, sizeof out[k]);
        snprintf(out[k].file, sizeof out[k].file, "%s", roms[idx[i]].name);
        out[k].model = roms[idx[i]].model;
        memcpy(out[k].sha1, roms[idx[i]].sha1, 20);
    }
    free(roms);
    return k;
}

static rm_err_t switch_err(esp_err_t e)
{
    switch (e) {
    case ESP_OK: return RM_OK;
    case ESP_ERR_NOT_FOUND: return RM_ERR_NOTFOUND;
    case ESP_ERR_INVALID_STATE: return emu_phase() == EMU_WAIT ? RM_ERR_WAITING : RM_ERR_CATCHUP;
    case ESP_ERR_NOT_ALLOWED: return RM_ERR_BUSY;
    case ESP_ERR_NO_MEM: return RM_ERR_FULL;
    default: return RM_ERR_IO;
    }
}

static rm_err_t op_switch_pet(void *ctx, const uint8_t sha1[20], uint8_t id)
{
    pet_ref_t p = { .id = id };
    memcpy(p.sha1, sha1, 20);
    return switch_err(pet_switch_to(&p));
}

static rm_err_t op_new_pet(void *ctx, const uint8_t sha1[20])
{
    pet_ref_t created;
    return switch_err(pet_new(sha1, &created));
}

static rm_err_t op_rename_pet(void *ctx, const uint8_t sha1[20], uint8_t id, const char *name)
{
    pet_ref_t p = { .id = id };
    memcpy(p.sha1, sha1, 20);
    switch (pet_store_rename(&p, name)) {
    case ESP_OK: return RM_OK;
    case ESP_ERR_INVALID_ARG: return RM_ERR_BAD;
    case ESP_ERR_INVALID_STATE: return RM_ERR_TAKEN;
    case ESP_ERR_NOT_FOUND: return RM_ERR_NOTFOUND;
    default: return RM_ERR_IO;
    }
}

static rm_err_t op_delete_pet(void *ctx, const uint8_t sha1[20], uint8_t id)
{
    pet_ref_t p = { .id = id };
    memcpy(p.sha1, sha1, 20);
    switch (pet_delete(&p)) {
    case ESP_OK: return RM_OK;
    case ESP_ERR_INVALID_STATE: case ESP_ERR_NOT_ALLOWED: return RM_ERR_BUSY;
    case ESP_ERR_NOT_FOUND: return RM_ERR_NOTFOUND;
    default: return RM_ERR_IO;
    }
}

static rm_err_t op_export_save(void *ctx, const uint8_t sha1[20], uint8_t id, uint8_t *out, size_t cap, size_t *len,
                               ps_meta_t *meta)
{
    esp_err_t err = saver_read_newest(sha1, id, meta, out, cap, len);
    if (err == ESP_OK) return RM_OK;
    return err == ESP_ERR_NOT_FOUND ? RM_ERR_NOTFOUND : RM_ERR_IO;
}

// Spec §7.3. The record lands as that pet's newest (seq = newest + 1) in the older A/B slot. For the running
// pet the saver freezes first, so nothing saves over it, and the ball restarts straight into it.
static rm_err_t op_import_save(void *ctx, const uint8_t sha1[20], uint8_t id, uint8_t *rec, size_t len,
                               rm_restore_mode_t mode)
{
    ps_meta_t meta;
    if (rm_save_check(rec, len, sha1, tc_state_compatible, &meta) || !ps_record_set_pet(rec, len, id)) return RM_ERR_BAD;
    bool running = is_running(sha1, id);
    emu_phase_t ph = emu_phase();
    // Rejected while catching up, like the picker; the running pet also not before its save has loaded.
    if (ph == EMU_CATCHUP || (running && ph != EMU_RUN)) return RM_ERR_CATCHUP;
    esp_err_t err = ESP_ERR_INVALID_STATE;
    for (int attempt = 0; attempt < 3 && err == ESP_ERR_INVALID_STATE; attempt++) {
        ps_meta_t newest;
        uint32_t seq = saver_read_newest(sha1, id, &newest, NULL, 0, NULL) == ESP_OK ? newest.seq : 0;
        bool ok;
        if (mode == RM_RESTORE_FROZEN) {
            int64_t now = 0;
            bool trusted = net_now(&now);  // untrusted: stored as such, so no catch-up later either
            ok = ps_record_restamp(rec, len, &now, trusted, seq + 1);
        } else {
            ok = ps_record_restamp(rec, len, NULL, false, seq + 1);
        }
        if (!ok) return RM_ERR_BAD;
        err = saver_import(sha1, id, rec, len);  // ESP_ERR_INVALID_STATE: a save landed in between
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
    if (!strcmp(model, "angel")) return s_guide_angel;
    if (!strcmp(model, "mothra")) return s_guide_mothra;
    return NULL;
}

static rm_err_t op_resume(void *ctx, const char *choice)
{
    if (emu_phase() != EMU_WAIT) return RM_ERR_BUSY;
    if (!strcmp(choice, "back")) {
        esp_err_t e = pet_switch_back();
        return e == ESP_OK ? RM_OK : e == ESP_ERR_NOT_FOUND ? RM_ERR_NOTFOUND : RM_ERR_BUSY;
    }
    return emu_answer(!strcmp(choice, "resume") ? EMU_ANSWER_RESUME : EMU_ANSWER_FF) ? RM_OK : RM_ERR_BUSY;
}

static rm_err_t op_skip(void *ctx) { return emu_skip() ? RM_OK : RM_ERR_BUSY; }

static rm_err_t op_factory_reset(void *ctx) { return factory_reset_request() == ESP_OK ? RM_OK : RM_ERR_BUSY; }

static rm_err_t op_sync_clock(void *ctx, int32_t *written_s)
{
    if (emu_phase() == EMU_WAIT) return RM_ERR_WAITING;
    switch (pet_clock_sync(written_s)) {
    case PET_CLOCK_OK: return RM_OK;
    case PET_CLOCK_UNSUPPORTED: return RM_ERR_NOTFOUND;
    case PET_CLOCK_NO_TIME: return RM_ERR_NOTIME;
    case PET_CLOCK_CATCHUP: return RM_ERR_CATCHUP;
    default: return RM_ERR_BUSY;
    }
}

// The debugger: one snapshot, and the ball stays awake (no light sleep, Wi-Fi power save off) for 5 s more.
static void op_debug(void *ctx, rm_debug_t *out)
{
    memset(out, 0, sizeof *out);
    out->ram_n = tc_get_snapshot(&out->cpu, &out->frame, out->ram, sizeof out->ram);
    out->model = s_model;
    uint8_t pct;
    out->phase = phase_now(&pct);
    tc_tone_t t;
    tc_get_tone(&t);
    out->buzzer = t.on;
    power_keep_awake(5000);
}

static bool op_rom(void *ctx, const uint8_t **data, size_t *len)
{
    if (!s_rom || s_model == TC_MODEL_UNKNOWN) return false;
    *data = s_rom;
    *len = s_rom_len;
    return true;
}

static rm_ops_t s_ops = {
    .look = op_look, .press = op_press, .status = op_status, .get_settings = op_get_settings,
    .put_settings = op_put_settings, .list_pets = op_list_pets, .list_roms = op_list_roms,
    .switch_pet = op_switch_pet, .new_pet = op_new_pet, .rename_pet = op_rename_pet, .delete_pet = op_delete_pet,
    .export_save = op_export_save, .import_save = op_import_save, .set_wifi = op_set_wifi,
    .restart = op_restart, .guide = op_guide, .restart_pending = op_restart_pending,
    .sync_clock = op_sync_clock, .debug = op_debug, .rom = op_rom, .resume = op_resume, .skip = op_skip,
    .factory_reset = op_factory_reset, .ctx = NULL,
};

const rm_ops_t *remote_ops_init(tc_model_t model, const uint8_t active_sha1[20], const uint8_t *rom, size_t rom_len)
{
    s_model = model;
    s_rom = rom;
    s_rom_len = rom_len;
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
