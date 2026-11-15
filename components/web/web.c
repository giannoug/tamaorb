// SPDX-License-Identifier: GPL-3.0-or-later
// esp_http_server glue for the portable remote API, plus mDNS. Everything runs on core 0.
#include "web.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "mdns.h"
#include "net.h"
#include "settings.h"
#include "tama_core.h"

#define BODY_MAX  16384  // spec §7: bigger bodies get 413 before anything is read
#define DRAIN_MAX 65536  // a refused body up to this size is read off so the client sees the reply; beyond, close

static const char *TAG = "web";
static const rm_ops_t *s_ops;
static char s_host[sizeof ((st_settings_t *)0)->name];  // the name mDNS and DHCP announce this boot

static void *ps_malloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM); }

static const char *status_line(int code)
{
    switch (code) {
    case 200: return "200 OK";
    case 202: return "202 Accepted";
    case 400: return "400 Bad Request";
    case 403: return "403 Forbidden";
    case 404: return "404 Not Found";
    case 405: return "405 Method Not Allowed";
    case 409: return "409 Conflict";
    case 413: return "413 Content Too Large";
    default: return "500 Internal Server Error";
    }
}

static esp_err_t send_plain(httpd_req_t *req, int code)
{
    const char *line = status_line(code);
    httpd_resp_set_status(req, line);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, line + 4);  // the reason phrase
}

// 0 and a NUL-terminated PSRAM copy of the body in *out (caller frees), or an HTTP status to send instead.
// -1: the connection failed mid-body.
static int read_body(httpd_req_t *req, char **out)
{
    *out = NULL;
    size_t n = req->content_len;
    if (n > BODY_MAX) return 413;
    char *buf = ps_malloc(n + 1);
    if (!buf) return 500;
    for (size_t got = 0; got < n;) {
        int r = httpd_req_recv(req, buf + got, n - got);
        if (r <= 0) {
            free(buf);
            return -1;
        }
        got += r;
    }
    buf[n] = '\0';
    *out = buf;
    return 0;
}

static esp_err_t refuse(httpd_req_t *req, int code)
{
    if (code < 0) return ESP_FAIL;  // close the socket
    esp_err_t err = send_plain(req, code);
    if (code == 413 && req->content_len > DRAIN_MAX) return ESP_FAIL;
    return err;  // httpd reads off whatever body is left
}

// A truncated value can't match: the longest allowed origin is far shorter than the buffer.
static const char *get_origin(httpd_req_t *req, char *buf, size_t cap)
{
    esp_err_t err = httpd_req_get_hdr_value_str(req, "Origin", buf, cap);
    return err == ESP_OK || err == ESP_ERR_HTTPD_RESULT_TRUNC ? buf : NULL;
}

static rm_method_t method_of(int m)
{
    switch (m) {
    case HTTP_GET: return RM_GET;
    case HTTP_POST: return RM_POST;
    case HTTP_PUT: return RM_PUT;
    case HTTP_PATCH: return RM_PATCH;
    default: return RM_OTHER;
    }
}

static esp_err_t api_handler(httpd_req_t *req)
{
    char *body;
    int st = read_body(req, &body);  // 413 before reading anything
    if (st) return refuse(req, st);

    char origin[128], ip[16];
    char *path = ps_malloc(strlen(req->uri) + 1);  // the URI may be 512 bytes: kept off the stack
    if (!path) {
        free(body);
        return refuse(req, 500);
    }
    strcpy(path, req->uri);
    char *q = strchr(path, '?');
    if (q) *q++ = '\0';
    net_ip(ip);
    rm_req_t rq = {
        .method = method_of(req->method),
        .path = path,
        .query = q,
        .origin = get_origin(req, origin, sizeof origin),
        .self_name = s_host,
        .self_ip = ip,
        .body = (const uint8_t *)body,
        .body_len = req->content_len,
        .state_ok = tc_state_compatible,
    };
    rm_resp_t rs;
    rm_http_handle(s_ops, &rq, &rs);
    free(body);
    free(path);

    httpd_resp_set_status(req, status_line(rs.status));
    httpd_resp_set_type(req, rs.content_type ? rs.content_type : "application/json");
    if (rs.disposition[0]) httpd_resp_set_hdr(req, "Content-Disposition", rs.disposition);
    esp_err_t err = httpd_resp_send(req, rs.body ? (const char *)rs.body : "", rs.body_len);
    free(rs.body);
    return err;
}

static esp_err_t mcp_post(httpd_req_t *req)
{
    if (req->content_len > BODY_MAX) return refuse(req, 413);
    char origin[128], ip[16];
    net_ip(ip);
    if (!rm_origin_allowed(get_origin(req, origin, sizeof origin), s_host, ip)) return refuse(req, 403);
    char *body;
    int st = read_body(req, &body);
    if (st) return refuse(req, st);
    char *out = NULL;
    int code = rm_mcp_handle(s_ops, body, req->content_len, &out);
    free(body);
    if (code == 202) {
        httpd_resp_set_status(req, status_line(202));
        return httpd_resp_send(req, "", 0);
    }
    if (code != 200 || !out) {
        free(out);
        return refuse(req, 500);
    }
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, out);
    free(out);
    return err;
}

static esp_err_t mcp_get(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Allow", "POST");
    return send_plain(req, 405);  // no SSE stream (spec §8.1)
}

extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[] asm("_binary_index_html_gz_end");

static esp_err_t page_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)index_html_gz_start, index_html_gz_end - index_html_gz_start);
}

esp_err_t web_start(const rm_ops_t *ops, const char *hostname)
{
    if (!net_configured()) {
        ESP_LOGI(TAG, "no Wi-Fi; not serving");
        return ESP_OK;
    }
    s_ops = ops;
    snprintf(s_host, sizeof s_host, "%s", hostname);
    cJSON_Hooks hooks = { .malloc_fn = ps_malloc, .free_fn = free };
    cJSON_InitHooks(&hooks);

    esp_err_t err = mdns_init();
    if (err == ESP_OK) err = mdns_hostname_set(s_host);
    if (err == ESP_OK) err = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    if (err != ESP_OK) ESP_LOGW(TAG, "mDNS: %s", esp_err_to_name(err));  // the IP still works

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.core_id = 0;
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;
    // The spike's 6 KB overflowed under load: MCP look (1 KB text + 1.6 KB PNG raster on the stack) left 56 bytes.
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 12;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    httpd_handle_t h;
    err = httpd_start(&h, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd: %s", esp_err_to_name(err));
        return err;
    }
    static const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = page_get },
        { .uri = "/mcp", .method = HTTP_POST, .handler = mcp_post },
        { .uri = "/mcp", .method = HTTP_GET, .handler = mcp_get },
        { .uri = "/api/*", .method = HTTP_GET, .handler = api_handler },
        { .uri = "/api/*", .method = HTTP_POST, .handler = api_handler },
        { .uri = "/api/*", .method = HTTP_PUT, .handler = api_handler },
        { .uri = "/api/*", .method = HTTP_PATCH, .handler = api_handler },
    };
    for (size_t i = 0; i < sizeof uris / sizeof *uris; i++) {
        esp_err_t re = httpd_register_uri_handler(h, &uris[i]);
        if (re != ESP_OK) ESP_LOGE(TAG, "register %s: %s", uris[i].uri, esp_err_to_name(re));
    }
    ESP_LOGI(TAG, "serving http://%s.local/", s_host);
    return ESP_OK;
}
