// SPDX-License-Identifier: GPL-3.0-or-later
#include "hostlink_task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hostlink.h"
#include "net.h"
#include "pet_clock.h"
#include "emu_task.h"
#include "pet_store.h"
#include "factory_reset.h"
#include "rom_store.h"
#include "saver.h"
#include "settings_store.h"

static const char *TAG = "hostlink";
static tc_model_t s_model;
static bool s_reboot;

static hl_store_result_t op_store(void *ctx, const char *name, const uint8_t *data, size_t len)
{
    if (rom_store_sd_present()) {
        if (rom_store_write_sd(name, data, len) != ESP_OK) return HL_STORE_FAIL;
        if (s_model != TC_MODEL_UNKNOWN) return HL_STORE_OK;
        s_reboot = true;  // nothing running yet: reboot to pick it up
        return HL_STORE_OK_REBOOT;
    }
    // No SD card: the pushed ROM becomes the active one, applied by a reboot.
    if (rom_store_set_active(data, len) != ESP_OK) return HL_STORE_FAIL;
    s_reboot = true;
    return HL_STORE_OK_REBOOT;
}

static void op_list(void *ctx, hl_emit_fn emit, void *ectx)
{
    static tc_rom_entry_t roms[16];
    int n = rom_store_scan(roms, 16);
    for (int i = 0; i < n; i++) {
        char line[160], hex[41];
        for (int k = 0; k < 20; k++) sprintf(hex + 2 * k, "%02x", roms[i].sha1[k]);
        snprintf(line, sizeof line, "%.63s %.8s %.40s", roms[i].name,
                 roms[i].model == TC_MODEL_TEST_ROM ? "test" : tc_model_name(roms[i].model), hex);
        emit(ectx, line);
    }
}

static bool op_remove(void *ctx, const char *name) { return rom_store_remove_sd(name) == ESP_OK; }

static bool op_wifi(void *ctx, const char *ssid, const char *pass)
{
    if (net_set_credentials(ssid, pass) != ESP_OK) return false;
    s_reboot = true;  // net_start reads the credentials at boot
    return true;
}

static void op_status(void *ctx, char *out, size_t cap)
{
    int64_t t;
    bool trusted = net_now(&t);
    char ip[16];
    if (!net_ip(ip)) snprintf(ip, sizeof ip, "-");  // the panel's address, for when tamaorb.local doesn't resolve
    char pet[24] = "-";
    int32_t p = pet_clock_pet_s();
    if (p >= 0) snprintf(pet, sizeof pet, "%02d:%02d:%02d", (int)(p / 3600), (int)(p / 60 % 60), (int)(p % 60));
    static const char *const PHASE[] = { [EMU_BOOT] = "boot", [EMU_WAIT] = "wait", [EMU_CATCHUP] = "catch-up",
                                         [EMU_RUN] = "run" };
    char name[PS_NAME_MAX + 1] = "-";
    pet_ref_t run;
    if (saver_running(run.sha1, &run.id) && pet_store_name(&run, name) == ESP_OK) {
        for (char *c = name; *c; c++) if (*c == ' ') *c = '_';  // one token per value
    }
    snprintf(out, cap, "rom=%s wifi=%d ip=%s time=%d batt_mv=%lu usb=%d sd=%d pet_clock=%s phase=%s pet=%s",
             tc_model_name(s_model), net_connected(), ip, trusted, (unsigned long)board_battery_mv(), board_usb_present(),
             rom_store_sd_present(), pet, PHASE[emu_phase()], name);
}

static void op_get_settings(void *ctx, st_settings_t *out) { settings_get(out); }
static void op_put_settings(void *ctx, const st_settings_t *s, bool display) { settings_put(s, display); }

static hl_clock_t op_clock(void *ctx, int32_t *written_s)
{
    switch (pet_clock_sync(written_s)) {
    case PET_CLOCK_OK: return HL_CLOCK_OK;
    case PET_CLOCK_UNSUPPORTED: return HL_CLOCK_UNSUPPORTED;
    case PET_CLOCK_NO_TIME: return HL_CLOCK_NO_TIME;
    case PET_CLOCK_CATCHUP: return HL_CLOCK_CATCHUP;
    default: return HL_CLOCK_BUSY;
    }
}

static bool op_factory_reset(void *ctx) { return factory_reset_request() == ESP_OK; }

static const hl_ops_t s_ops = { .store = op_store, .list = op_list, .remove = op_remove, .wifi = op_wifi,
                                .status = op_status, .get_settings = op_get_settings, .put_settings = op_put_settings,
                                .clock = op_clock, .factory_reset = op_factory_reset, .ctx = NULL };

static void reply(void *ctx, const char *line)
{
    printf("%s\n", line);
    fflush(stdout);
}

static void hostlink_task(void *arg)
{
    hl_session_t *s = malloc(hl_session_size());
    if (!s) {
        ESP_LOGE(TAG, "no memory");
        vTaskDelete(NULL);
        return;
    }
    hl_init(s, &s_ops);
    static char line[HL_MAX_LINE];
    size_t n = 0;
    for (;;) {
        uint8_t c;
        if (usb_serial_jtag_read_bytes(&c, 1, portMAX_DELAY) != 1) continue;
        if (c == '\n' || c == '\r') {
            if (n) {
                line[n] = '\0';
                hl_feed_line(s, line, reply, NULL);
                n = 0;
            }
            if (s_reboot) {
                ESP_LOGI(TAG, "saving and rebooting to apply");
                if (!saver_request_and_wait(3000)) ESP_LOGW(TAG, "save before reboot failed; rebooting anyway");
                esp_restart();
            }
        } else if (n < sizeof line - 1) {
            line[n++] = (char)c;
        } else {
            n = 0;  // overlong line: drop it
        }
    }
}

void hostlink_task_start(tc_model_t active_model)
{
    s_model = active_model;
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = 4096;
    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "USB serial driver install failed");
        return;
    }
    usb_serial_jtag_vfs_use_driver();  // console output and our replies share the driver
    xTaskCreatePinnedToCore(hostlink_task, "hostlink", 6144, NULL, 4, NULL, 0);  // LIST (SD + FAT + SHA-1 + printf) overflowed 4096
}
