// SPDX-License-Identifier: GPL-3.0-or-later
#include "rom_store.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "board.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "roms";
static bool s_sd;
static bool s_nvs_ok;

bool rom_store_nvs_ok(void) { return s_nvs_ok; }

esp_err_t rom_store_init(void)
{
    // Never erase this partition: it holds every pet. If it can't be opened (e.g. older firmware
    // booted on newer NVS data), run with saves off and say so.
    esp_err_t err = nvs_flash_init_partition(ROM_STORE_PARTITION);
    s_nvs_ok = err == ESP_OK;
    if (!s_nvs_ok) ESP_LOGE(TAG, "saves partition unusable (%s); saving is off", esp_err_to_name(err));
    s_sd = board_sd_mount("/sdcard") == ESP_OK;
    ESP_LOGI(TAG, "SD card %s", s_sd ? "mounted" : "not present");
    return ESP_OK;
}

bool rom_store_sd_present(void) { return s_sd; }

static bool path_for(const char *name, char *out, size_t cap)
{
    return snprintf(out, cap, "%s/%s", ROM_STORE_DIR, name) < (int)cap;
}

bool rom_store_read_sd(const char *name, uint8_t *rom)
{
    char path[128];
    if (!s_sd || !path_for(name, path, sizeof path)) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(rom, 1, TC_ROM_BYTES, f);
    int extra = fgetc(f);
    fclose(f);
    return n == TC_ROM_BYTES && extra == EOF;
}

int rom_store_scan(tc_rom_entry_t *out, int max)
{
    if (!s_sd) return 0;
    DIR *d = opendir(ROM_STORE_DIR);
    if (!d) return 0;
    static uint8_t buf[TC_ROM_BYTES];
    int n = 0;
    struct dirent *e;
    while (n < max && (e = readdir(d))) {
        size_t len = strlen(e->d_name);
        if (len < 5 || len >= sizeof out[0].name || strcasecmp(e->d_name + len - 4, ".bin") != 0) continue;
        char path[128];
        struct stat st;
        if (!path_for(e->d_name, path, sizeof path) || stat(path, &st) != 0 || st.st_size != TC_ROM_BYTES) continue;
        if (!rom_store_read_sd(e->d_name, buf)) continue;
        tc_rom_entry_t *r = &out[n++];
        snprintf(r->name, sizeof r->name, "%s", e->d_name);
        r->model = tc_rom_identify(buf, sizeof buf, r->sha1);
    }
    closedir(d);
    return n;
}

static esp_err_t open_ns(nvs_open_mode_t mode, nvs_handle_t *h)
{
    return nvs_open_from_partition(ROM_STORE_PARTITION, "rom", mode, h);
}

bool rom_store_cached(uint8_t *rom, uint8_t sha1[20])
{
    nvs_handle_t h;
    if (open_ns(NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = TC_ROM_BYTES, slen = 20;
    uint8_t want[20];
    bool ok = nvs_get_blob(h, "rom", rom, &len) == ESP_OK && len == TC_ROM_BYTES &&
              nvs_get_blob(h, "rom_sha", want, &slen) == ESP_OK && slen == 20;
    nvs_close(h);
    if (!ok) return false;
    tc_rom_identify(rom, TC_ROM_BYTES, sha1);
    return memcmp(sha1, want, 20) == 0;
}

esp_err_t rom_store_set_active(const uint8_t *rom)
{
    uint8_t sha1[20];
    tc_rom_identify(rom, TC_ROM_BYTES, sha1);
    nvs_handle_t h;
    esp_err_t err = open_ns(NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, "rom", rom, TC_ROM_BYTES);
    if (err == ESP_OK) err = nvs_set_blob(h, "rom_sha", sha1, 20);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t rom_store_write_sd(const char *name, const uint8_t *data, size_t len)
{
    char path[128];
    if (!s_sd || !path_for(name, path, sizeof path)) return ESP_ERR_INVALID_STATE;
    mkdir("/sdcard/tamaorb", 0775);
    mkdir(ROM_STORE_DIR, 0775);
    FILE *f = fopen(path, "wb");
    if (!f) return ESP_FAIL;
    size_t n = fwrite(data, 1, len, f);
    int closed = fclose(f);
    return (n == len && closed == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t rom_store_remove_sd(const char *name)
{
    char path[128];
    if (!s_sd || !path_for(name, path, sizeof path)) return ESP_ERR_INVALID_STATE;
    if (unlink(path) == 0) return ESP_OK;
    ESP_LOGW(TAG, "remove %s failed: errno %d (%s)", path, errno, strerror(errno));
    return ESP_ERR_NOT_FOUND;
}
