// SPDX-License-Identifier: GPL-3.0-or-later
// esp_http_server glue for the portable remote API, plus mDNS. Everything runs on core 0.
#include "web.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mdns.h"
#include "net.h"
#include "settings.h"
#include "tama_core.h"

#define BODY_MAX  16384  // bigger bodies get 413 before anything is read
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
    case 503: return "503 Service Unavailable";
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
    case HTTP_DELETE: return RM_DELETE;
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
    return send_plain(req, 405);  // no SSE stream
}

// ---- Slow requests off the server task ----
// A press sequence (POST /api/press, or the MCP press tool) answers only when it has run, up to ~10 s. Run on the
// server task, it held every other request up meanwhile (the panel, MCP, the debugger). These requests are handed
// to one worker task with ESP-IDF's async request handlers, so the server keeps answering; presses still run one at
// a time (the press task refuses a second one as busy) and still answer after their sequence, as before.
#define SLOW_QUEUE 4
static QueueHandle_t s_slow_q;
static volatile bool s_slow_busy;  // the worker is running a request

static void slow_worker(void *arg)
{
    for (;;) {
        httpd_req_t *req;
        if (xQueueReceive(s_slow_q, &req, portMAX_DELAY) != pdTRUE) continue;
        s_slow_busy = true;
        esp_err_t r = !strcmp(req->uri, "/mcp") ? mcp_post(req) : api_handler(req);
        // httpd doesn't read off what a handler left of the body on an async request: do it here, or close, so
        // the rest isn't taken for the next request on this connection.
        char junk[256];
        size_t drained = 0;
        int got;
        while (r == ESP_OK && drained <= DRAIN_MAX && (got = httpd_req_recv(req, junk, sizeof junk)) > 0) drained += (size_t)got;
        if (r != ESP_OK || drained > DRAIN_MAX) httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
        s_slow_busy = false;
        httpd_req_async_handler_complete(req);
    }
}

static esp_err_t send_json_status(httpd_req_t *req, int code, const char *body)
{
    httpd_resp_set_status(req, status_line(code));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
}

static esp_err_t slow_post(httpd_req_t *req)
{
    // A panel press while another request runs is refused as busy, as when the press task is running one; waiting
    // in the queue it would run seconds later instead. (MCP calls queue: an agent sends one at a time.)
    if (!strcmp(req->uri, "/api/press") && (s_slow_busy || uxQueueMessagesWaiting(s_slow_q)))
        return send_json_status(req, 409, "{\"error\":\"busy\"}");
    // Four already waiting: say so on this request (httpd reads off its body) rather than queue forever. Only this
    // task enqueues, so there is room for the send below once this check passes.
    if (uxQueueSpacesAvailable(s_slow_q) == 0) return send_json_status(req, 503, "{\"error\":\"busy\"}");
    httpd_req_t *copy = NULL;
    if (httpd_req_async_handler_begin(req, &copy) != ESP_OK) return refuse(req, 500);
    xQueueSend(s_slow_q, &copy, portMAX_DELAY);
    return ESP_OK;
}

// ---- The debugger's WebSocket: snapshots pushed while a debugger page is open ----
// At most two pages; each gets the GET /api/debug JSON whenever the emulator has published a new snapshot, at
// most every 50 ms. No page open: the push task only checks the table. Every snapshot also keeps the ball awake
// (op_debug), so it drops back to its power saving a few seconds after the last page closes.
#define WS_CLIENTS 2
#define WS_PERIOD_MS 50
static httpd_handle_t s_httpd;
static TaskHandle_t s_push_task;  // sleeps until a debugger connects
static int s_ws_fd[WS_CLIENTS] = { -1, -1 };
static SemaphoreHandle_t s_ws_mutex;

// Closes a debugger socket at once with a close frame: code (big-endian) and a reason the page can show.
static void ws_refuse(httpd_req_t *req, uint16_t code, const char *why)
{
    uint8_t reason[64];
    size_t n = strnlen(why, sizeof reason - 2);
    reason[0] = (uint8_t)(code >> 8);
    reason[1] = (uint8_t)code;
    memcpy(reason + 2, why, n);
    httpd_ws_frame_t f = { .final = true, .type = HTTPD_WS_TYPE_CLOSE, .payload = reason, .len = n + 2 };
    httpd_ws_send_frame(req, &f);  // the caller returns ESP_FAIL, and httpd closes the socket
}

// After the handshake (ESP-IDF 6 no longer calls the handler for it): take a slot or say why not.
static esp_err_t ws_connected(httpd_req_t *req)
{
    // WebSockets skip the browser's same-origin rules: a page from another site could otherwise read the stream,
    // hold both slots and keep the ball awake. Same rule as the API's POSTs (no Origin, e.g. a script: allowed).
    char origin[128], ip[16];
    net_ip(ip);
    if (!rm_origin_allowed(get_origin(req, origin, sizeof origin), s_host, ip)) {
        ESP_LOGW(TAG, "debugger refused: foreign origin");
        ws_refuse(req, 1008, "origin");
        return ESP_FAIL;
    }
    int fd = httpd_req_to_sockfd(req), slot = -1;
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (int i = 0; i < WS_CLIENTS && slot < 0; i++)
        if (s_ws_fd[i] == fd) slot = i;  // already ours (an IDF that also calls the handler on the handshake)
    for (int i = 0; i < WS_CLIENTS && slot < 0; i++)
        if (s_ws_fd[i] < 0 || httpd_ws_get_fd_info(s_httpd, s_ws_fd[i]) != HTTPD_WS_CLIENT_WEBSOCKET) slot = i;
    if (slot >= 0) s_ws_fd[slot] = fd;
    xSemaphoreGive(s_ws_mutex);
    if (slot < 0) {
        ESP_LOGI(TAG, "debugger refused: %d already open", WS_CLIENTS);
        ws_refuse(req, 1013, "two debuggers already open");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "debugger %d connected", slot);
    if (s_push_task) xTaskNotifyGive(s_push_task);
    return ESP_OK;
}

static esp_err_t ws_debug(httpd_req_t *req)
{
    if (req->method == HTTP_GET) return ws_connected(req);  // older IDFs call the handler for the handshake
    // The page sends nothing; read off and drop anything that comes (control frames are handled by httpd).
    httpd_ws_frame_t f = { 0 };
    if (httpd_ws_recv_frame(req, &f, 0) != ESP_OK) return ESP_FAIL;
    if (f.len > 1024) return ESP_FAIL;  // the page sends one byte a second; anything big is not a debugger
    if (f.len) {
        uint8_t *buf = ps_malloc(f.len);
        if (!buf) return ESP_FAIL;
        f.payload = buf;
        esp_err_t err = httpd_ws_recv_frame(req, &f, f.len);
        free(buf);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

// httpd's session close: a debugger's slot is cleared before its fd is closed (and can be reused), under the lock
// the push task sends under, so a snapshot never goes to a socket that has since become someone else's.
static void web_close(httpd_handle_t hd, int fd)
{
    if (s_ws_mutex) {
        xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
        for (int i = 0; i < WS_CLIENTS; i++)
            if (s_ws_fd[i] == fd) {
                s_ws_fd[i] = -1;
                ESP_LOGI(TAG, "debugger %d left", i);
            }
        xSemaphoreGive(s_ws_mutex);
    }
    close(fd);
}

// Under s_ws_mutex: sends to every open slot. A failed send frees the slot; httpd closes the dead socket itself
// when its read fails. (No httpd_sess_trigger_close here: it waits on the server task, which may be waiting
// for this mutex in web_close.)
static void ws_send_all(const char *text)
{
    httpd_ws_frame_t f = { .final = true, .type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)text, .len = strlen(text) };
    for (int i = 0; i < WS_CLIENTS; i++)
        if (s_ws_fd[i] >= 0 && httpd_ws_send_frame_async(s_httpd, s_ws_fd[i], &f) != ESP_OK) {
            ESP_LOGI(TAG, "debugger %d: send failed, dropped", i);
            s_ws_fd[i] = -1;
        }
}

static void ws_push_task(void *arg)
{
    uint32_t seq = 0;
    int quiet = -1;  // push periods since the last frame sent; -1: asleep (no debugger)
    rm_debug_t *d = ps_malloc(sizeof *d);
    if (!d) {
        ESP_LOGE(TAG, "debugger push: no memory");
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(WS_PERIOD_MS));
        int n = 0;
        xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
        for (int i = 0; i < WS_CLIENTS; i++) n += s_ws_fd[i] >= 0;
        xSemaphoreGive(s_ws_mutex);
        if (!n) {  // nobody watching: sleep until a debugger connects (no 50 ms wakeups meanwhile)
            if (quiet >= 0) ESP_LOGI(TAG, "debugger push: %u bytes of stack never used", (unsigned)uxTaskGetStackHighWaterMark(NULL));
            quiet = -1;
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            quiet = 0;
            continue;
        }
        if (!tc_frame_changed(&seq)) {
            // Nothing new (no ROM, catching up): once a second say "alive", so the page can tell a quiet ball
            // from a dead socket (it reconnects after 3 s of silence).
            if (++quiet * WS_PERIOD_MS < 1000) continue;
            quiet = 0;
            xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
            ws_send_all("{\"alive\":true}");
            xSemaphoreGive(s_ws_mutex);
            continue;
        }
        quiet = 0;
        s_ops->debug(s_ops->ctx, d);
        cJSON *j = rm_json_debug(d);
        char *text = j ? cJSON_PrintUnformatted(j) : NULL;  // encoded outside the lock
        cJSON_Delete(j);
        if (!text) continue;
        xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
        ws_send_all(text);
        xSemaphoreGive(s_ws_mutex);
        free(text);
    }
}

extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[] asm("_binary_index_html_gz_end");
extern const uint8_t debug_html_gz_start[] asm("_binary_debug_html_gz_start");
extern const uint8_t debug_html_gz_end[] asm("_binary_debug_html_gz_end");

static esp_err_t send_page(httpd_req_t *req, const uint8_t *start, const uint8_t *end)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)start, end - start);
}

static esp_err_t page_get(httpd_req_t *req) { return send_page(req, index_html_gz_start, index_html_gz_end); }
static esp_err_t debug_page_get(httpd_req_t *req) { return send_page(req, debug_html_gz_start, debug_html_gz_end); }

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
    rm_set_alloc(ps_malloc);  // remote's look, save and response buffers: PSRAM, not internal RAM

    esp_err_t err = mdns_init();
    if (err == ESP_OK) err = mdns_hostname_set(s_host);
    if (err == ESP_OK) err = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    if (err != ESP_OK) ESP_LOGW(TAG, "mDNS: %s", esp_err_to_name(err));  // the IP still works

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.core_id = 0;
    // A panel, a debugger (page + WebSocket), an agent and a press waiting in the worker at once. lwIP keeps 3 of
    // its CONFIG_LWIP_MAX_SOCKETS (10) for itself and httpd's control socket.
    cfg.max_open_sockets = 7;
    cfg.lru_purge_enable = true;
    cfg.close_fn = web_close;  // frees a debugger's slot before its fd can be reused
    // 6 KB overflowed under load before remote's big buffers left the stack. Now about 4.4 KB stays free in the
    // worst case measured; the margin covers the NVS and SD paths (ADR 0005).
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 13;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    s_slow_q = xQueueCreate(SLOW_QUEUE, sizeof(httpd_req_t *));
    s_ws_mutex = xSemaphoreCreateMutex();
    if (!s_slow_q || !s_ws_mutex) return ESP_ERR_NO_MEM;
    httpd_handle_t h;
    err = httpd_start(&h, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd: %s", esp_err_to_name(err));
        return err;
    }
    s_httpd = h;
    xTaskCreatePinnedToCore(slow_worker, "web_slow", 8192, NULL, cfg.task_priority, NULL, 0);
    xTaskCreatePinnedToCore(ws_push_task, "web_ws", 4096, NULL, tskIDLE_PRIORITY + 2, &s_push_task, 0);
    static const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = page_get },
        { .uri = "/debug", .method = HTTP_GET, .handler = debug_page_get },
        { .uri = "/ws/debug", .method = HTTP_GET, .handler = ws_debug, .is_websocket = true,
          .ws_post_handshake_cb = ws_connected },
        { .uri = "/mcp", .method = HTTP_POST, .handler = slow_post },
        { .uri = "/api/press", .method = HTTP_POST, .handler = slow_post },  // before /api/*: first match wins
        { .uri = "/mcp", .method = HTTP_GET, .handler = mcp_get },
        { .uri = "/api/*", .method = HTTP_GET, .handler = api_handler },
        { .uri = "/api/*", .method = HTTP_POST, .handler = api_handler },
        { .uri = "/api/*", .method = HTTP_PUT, .handler = api_handler },
        { .uri = "/api/*", .method = HTTP_PATCH, .handler = api_handler },
        { .uri = "/api/*", .method = HTTP_DELETE, .handler = api_handler },
    };
    for (size_t i = 0; i < sizeof uris / sizeof *uris; i++) {
        esp_err_t re = httpd_register_uri_handler(h, &uris[i]);
        if (re != ESP_OK) ESP_LOGE(TAG, "register %s: %s", uris[i].uri, esp_err_to_name(re));
    }
    ESP_LOGI(TAG, "serving http://%s.local/", s_host);
    return ESP_OK;
}
