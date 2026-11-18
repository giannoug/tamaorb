// SPDX-License-Identifier: GPL-3.0-or-later
#include "rom_switch.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "emu_task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "rom_store.h"
#include "saver.h"
#include "tc_rom.h"

static const char *TAG = "switch";
static atomic_bool s_restart, s_switching;

static void restart_cb(void *arg) { esp_restart(); }

void app_restart_after(uint32_t ms)
{
    if (atomic_exchange(&s_restart, true)) return;
    ESP_LOGI(TAG, "restarting in %lu ms", (unsigned long)ms);
    const esp_timer_create_args_t args = { .callback = restart_cb, .name = "restart" };
    esp_timer_handle_t t;
    if (esp_timer_create(&args, &t) != ESP_OK || esp_timer_start_once(t, (uint64_t)ms * 1000) != ESP_OK)
        esp_restart();  // no timer: restarting now beats never restarting
}

bool app_restart_pending(void) { return atomic_load(&s_restart); }

static pet_info_t s_list[PET_STORE_MAX];  // callers are serialised (s_switching, boot)

esp_err_t pet_for_rom(const uint8_t sha1[20], tc_model_t m, pet_ref_t *out)
{
    ps_pet_seen_t seen[PS_PETS_PER_ROM] = { 0 };
    int n = pet_store_list(s_list, PET_STORE_MAX);
    for (int i = 0; i < n; i++) {
        const pet_info_t *p = &s_list[i];
        if (memcmp(p->ref.sha1, sha1, 20) || p->ref.id >= PS_PETS_PER_ROM) continue;
        seen[p->ref.id] = (ps_pet_seen_t){ .exists = true, .has_save = p->has_save, .save_trusted = p->save_trusted,
                                           .save_unix = p->save_unix, .seq = p->seq };
    }
    int id = ps_pick_for_rom(seen);
    if (id >= 0) {
        memcpy(out->sha1, sha1, 20);
        out->id = (uint8_t)id;
        return ESP_OK;
    }
    return pet_store_create(sha1, m, out);
}

static pet_ref_t s_prev;
static bool s_have_prev;

void pet_switch_boot(void)
{
    s_have_prev = pet_store_take_prev(&s_prev);
    uint8_t run[20], id;
    if (s_have_prev && saver_running(run, &id) && id == s_prev.id && !memcmp(run, s_prev.sha1, 20))
        s_have_prev = false;  // "back" to the pet that is running: nothing to go back to
}

bool pet_prev(pet_ref_t *out, char name[PS_NAME_MAX + 1])
{
    if (!s_have_prev) return false;
    *out = s_prev;
    if (name && pet_store_name(&s_prev, name) != ESP_OK) name[0] = 0;
    return true;
}

// Makes p's ROM (read from the card) the active one; on a match with the cached ROM nothing is written.
static esp_err_t activate_rom(const pet_ref_t *p)
{
    char file[64];
    if (!rom_store_find_sd(p->sha1, file)) return ESP_ERR_NOT_FOUND;
    // Internal RAM, like the boot path's buffer: it is the source of a flash write.
    uint8_t *rom = heap_caps_malloc(TC_ROM_MAX_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!rom) return ESP_ERR_NO_MEM;
    uint8_t sha1[20];
    size_t len = 0;
    esp_err_t err = ESP_ERR_NOT_FOUND;
    if (rom_store_cached(rom, &len, sha1) && !memcmp(sha1, p->sha1, 20)) {
        err = ESP_OK;
    } else if (rom_store_read_sd(file, rom, &len) && tc_rom_identify(rom, len, sha1) != TC_MODEL_UNKNOWN &&
               !memcmp(sha1, p->sha1, 20)) {
        err = rom_store_set_active(rom, len);
    }
    free(rom);
    return err;
}

static esp_err_t switch_locked(const pet_ref_t *p)
{
    uint8_t run[20], run_id;
    bool running = saver_running(run, &run_id);
    if (running && run_id == p->id && !memcmp(run, p->sha1, 20)) return ESP_OK;  // already there: nothing to do
    char file[64];
    if (!tc_model_playable(tc_model_from_sha1(p->sha1)) || !rom_store_find_sd(p->sha1, file)) return ESP_ERR_NOT_FOUND;
    if (!saver_request_and_wait(3000)) {
        ESP_LOGW(TAG, "not switching: the save failed");
        return ESP_FAIL;
    }
    esp_err_t err = activate_rom(p);
    if (err == ESP_OK && running) {
        pet_ref_t prev = { .id = run_id };
        memcpy(prev.sha1, run, 20);
        err = pet_store_set_prev(&prev);
    }
    if (err == ESP_OK) err = pet_store_set_active(p);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "switch failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "active pet is now %d of %s", p->id, file);
    app_restart_after(1000);
    return ESP_OK;
}

static esp_err_t begin(void)
{
    if (emu_phase() != EMU_RUN) return ESP_ERR_INVALID_STATE;
    if (app_restart_pending() || atomic_exchange(&s_switching, true)) return ESP_ERR_NOT_ALLOWED;
    return ESP_OK;
}

static esp_err_t end(esp_err_t err)
{
    if (err != ESP_OK) atomic_store(&s_switching, false);  // on success it stays set until the restart
    return err;
}

esp_err_t pet_switch_to(const pet_ref_t *p)
{
    esp_err_t err = begin();
    if (err != ESP_OK) return err;
    bool exists = false;
    int n = pet_store_list(s_list, PET_STORE_MAX);
    for (int i = 0; i < n && !exists; i++) exists = pet_ref_eq(&s_list[i].ref, p);
    return end(exists ? switch_locked(p) : ESP_ERR_NOT_FOUND);
}

esp_err_t pet_new(const uint8_t sha1[20], pet_ref_t *created)
{
    esp_err_t err = begin();
    if (err != ESP_OK) return err;
    char file[64];
    tc_model_t m = tc_model_from_sha1(sha1);
    if (!tc_model_playable(m) || !rom_store_find_sd(sha1, file)) return end(ESP_ERR_NOT_FOUND);
    err = pet_store_create(sha1, m, created);
    if (err == ESP_OK) err = switch_locked(created);
    return end(err);
}

esp_err_t pet_delete(const pet_ref_t *p)
{
    if (app_restart_pending()) return ESP_ERR_NOT_ALLOWED;
    return pet_store_delete(p);
}

esp_err_t pet_switch_back(void)
{
    if (emu_phase() != EMU_WAIT) return ESP_ERR_INVALID_STATE;
    if (!s_have_prev) return ESP_ERR_NOT_FOUND;
    if (app_restart_pending() || atomic_exchange(&s_switching, true)) return ESP_ERR_NOT_ALLOWED;
    esp_err_t err = activate_rom(&s_prev);
    if (err == ESP_OK) err = pet_store_set_active(&s_prev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "back failed: %s", esp_err_to_name(err));
        atomic_store(&s_switching, false);
        return err;
    }
    ESP_LOGI(TAG, "back to pet %d", s_prev.id);
    app_restart_after(1000);
    return ESP_OK;
}
