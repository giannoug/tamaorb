// SPDX-License-Identifier: GPL-3.0-or-later
#include "net.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs.h"

#define TRUST_MAGIC 0x54494D45u  // "TIME"

static const char *TAG = "net";

// Survives software resets and deep sleep, not power loss ("known time").
static RTC_NOINIT_ATTR uint32_t s_time_trust;
static volatile bool s_connected, s_configured;
static bool s_started;
static esp_netif_t *s_sta;
static char s_ntp[64];  // lwIP's SNTP keeps the pointer, not a copy

static void on_sync(struct timeval *tv)
{
    s_time_trust = TRUST_MAGIC;
    ESP_LOGI(TAG, "time synced: %lld", (long long)tv->tv_sec);
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        esp_wifi_connect();  // keep trying; the driver backs off between attempts
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        ESP_LOGI(TAG, "connected");
    }
}

static bool load_credentials(wifi_config_t *cfg)
{
    nvs_handle_t h;
    if (nvs_open("net", NVS_READONLY, &h) != ESP_OK) return false;
    // NVS strings carry a terminator; a 32-char SSID / 64-char password fill wifi_config exactly.
    char ssid[33] = { 0 }, pass[65] = { 0 };
    size_t sl = sizeof ssid, pl = sizeof pass;
    bool ok = nvs_get_str(h, "ssid", ssid, &sl) == ESP_OK && sl > 1;
    if (ok && nvs_get_str(h, "pass", pass, &pl) != ESP_OK) pass[0] = '\0';
    nvs_close(h);
    memcpy(cfg->sta.ssid, ssid, strnlen(ssid, sizeof cfg->sta.ssid));
    memcpy(cfg->sta.password, pass, strnlen(pass, sizeof cfg->sta.password));
    return ok;
}

esp_err_t net_start(const char *hostname, const char *ntp_server)
{
    // Whitelist: only resets that keep the RTC running keep the time trusted.
    switch (esp_reset_reason()) {
    case ESP_RST_SW: case ESP_RST_PANIC: case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT:
    case ESP_RST_DEEPSLEEP: case ESP_RST_USB: case ESP_RST_JTAG:
        break;
    default:
        s_time_trust = 0;
    }

    wifi_config_t cfg = { 0 };
    s_configured = load_credentials(&cfg);
    if (!s_configured) {
        ESP_LOGI(TAG, "no Wi-Fi credentials; running offline");
        return ESP_OK;
    }
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    s_sta = esp_netif_create_default_wifi_sta();
    if (hostname && esp_netif_set_hostname(s_sta, hostname) != ESP_OK)  // before connecting: DHCP sends it
        ESP_LOGW(TAG, "hostname not set");
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    snprintf(s_ntp, sizeof s_ntp, "%s", ntp_server && *ntp_server ? ntp_server : "pool.ntp.org");
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG(s_ntp);
    sntp.sync_cb = on_sync;
    esp_netif_sntp_init(&sntp);
    s_started = true;
    ESP_LOGI(TAG, "connecting to \"%s\"", (const char *)cfg.sta.ssid);
    return ESP_OK;
}

esp_err_t net_set_credentials(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("net", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, "ssid", ssid);
    if (err == ESP_OK) err = nvs_set_str(h, "pass", pass ? pass : "");
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

bool net_now(int64_t *unix_s)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *unix_s = tv.tv_sec;
    return s_time_trust == TRUST_MAGIC;
}

bool net_wait_time(uint32_t timeout_ms)
{
    if (s_time_trust == TRUST_MAGIC) return true;
    if (!s_started) return false;
    return esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms)) == ESP_OK && s_time_trust == TRUST_MAGIC;
}

bool net_wait_fresh_sync(uint32_t timeout_ms)
{
    return s_started && esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms)) == ESP_OK;
}

bool net_connected(void) { return s_connected; }
bool net_configured(void) { return s_configured; }

bool net_ip(char out[16])
{
    esp_netif_ip_info_t ip;
    out[0] = '\0';
    if (!s_sta || esp_netif_get_ip_info(s_sta, &ip) != ESP_OK || ip.ip.addr == 0) return false;
    snprintf(out, 16, IPSTR, IP2STR(&ip.ip));
    return true;
}

int8_t net_rssi(void)
{
    int rssi = 0;
    if (!s_connected || esp_wifi_sta_get_rssi(&rssi) != ESP_OK) return 0;
    return (int8_t)rssi;
}
