// SPDX-License-Identifier: GPL-3.0-or-later
// Coverage-guided fuzzer for the parsers that take input from the LAN or the USB cable:
// the REST router, the MCP dispatcher, save records, press specs, the Origin check,
// settings values and the USB line protocol.
//
// No libFuzzer needed: target sources are built with gcc's -fsanitize-coverage=trace-pc,
// and __sanitizer_cov_trace_pc below turns that into an AFL-style edge map. Inputs that
// reach new edges join the corpus. Build with ASan/UBSan so any memory error aborts.
//
//   build-host/fuzz_parsers [iterations] [seed]     (defaults: 200000, 1)
// Exit 0 when the run finishes; a sanitizer abort means a bug, with the input in fuzz-crash.bin.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sanitizer/common_interface_defs.h>

#include "fake_ops.h"
#include "hostlink.h"
#include "persist.h"
#include "remote.h"
#include "settings.h"

// ---- coverage ----
#define MAP_SIZE (1 << 16)
static uint8_t cov[MAP_SIZE];
static uintptr_t prev_loc;
void __sanitizer_cov_trace_pc(void)
{
    uintptr_t loc = (uintptr_t)__builtin_return_address(0);
    loc = (loc >> 4) ^ (loc << 8);
    cov[(loc ^ prev_loc) & (MAP_SIZE - 1)]++;
    prev_loc = loc >> 1;
}
static uint8_t seen[MAP_SIZE];
static int new_coverage(void)
{
    int fresh = 0;
    for (int i = 0; i < MAP_SIZE; i++) {
        if (cov[i]) {
            uint8_t bucket = cov[i] >= 128 ? 128 : cov[i] >= 32 ? 32 : cov[i] >= 8 ? 8 : cov[i] >= 4 ? 4 : cov[i];
            if (!(seen[i] & bucket)) { seen[i] |= bucket; fresh = 1; }
        }
    }
    return fresh;
}

// ---- rng ----
static uint64_t rng = 1;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)rng; }
static uint32_t below(uint32_t n) { return n ? rnd() % n : 0; }

// ---- targets ----
#define MAX_IN 4096
enum { T_HTTP, T_MCP, T_SAVE, T_PRESS, T_ORIGIN, T_SETTING, T_HOSTLINK, T_COUNT };
static const char *TNAME[T_COUNT] = { "http", "mcp", "save", "press", "origin", "setting", "hostlink" };

static const uint8_t P1_SHA[20] = { 0x4b, 0x49, 0x79, 0xcf, 0x92, 0xdc, 0x9d, 0x2f, 0xb6, 0xd7,
                                    0x29, 0x5a, 0x38, 0xf2, 0x09, 0xf3, 0xda, 0x14, 0x4f, 0x72 };
static bool state_ok(const uint8_t *p, size_t n) { return n > 0 && p[0] != 0xFF; }

// Splits at the first n NULs; missing fields become "".
static int split(char *buf, size_t len, char **f, int n)
{
    int k = 0;
    f[k++] = buf;
    for (size_t i = 0; i < len && k < n; i++)
        if (buf[i] == '\0') f[k++] = buf + i + 1;
    for (int i = k; i < n; i++) f[i] = buf + len;  // points at the terminator
    return k;
}

static void hl_reply(void *ctx, const char *line) { (void)ctx; (void)line; }
static hl_store_result_t hl_store(void *c, const char *n, const uint8_t *d, size_t l) { (void)c; (void)n; (void)d; return l & 1 ? HL_STORE_OK_REBOOT : HL_STORE_OK; }
static void hl_list(void *c, hl_emit_fn emit, void *e) { (void)c; emit(e, "tama.bin P1 4b4979cf92dc9d2fb6d7295a38f209f3da144f72"); }
static bool hl_rm(void *c, const char *n) { (void)c; return n[0] == 't'; }
static bool hl_wifi(void *c, const char *s, const char *p) { (void)c; (void)p; return s[0] != 'x'; }
static void hl_status(void *c, char *o, size_t cap) { (void)c; snprintf(o, cap, "rom=P1 wifi=1"); }
static st_settings_t hl_cur;
static void hl_get(void *c, st_settings_t *o) { (void)c; *o = hl_cur; }
static void hl_put(void *c, const st_settings_t *s, bool d) { (void)c; (void)d; hl_cur = *s; }
static const hl_ops_t HL_OPS = { hl_store, hl_list, hl_rm, hl_wifi, hl_status, hl_get, hl_put, NULL };

static void run_one(int t, const uint8_t *data, size_t len)
{
    char *buf = malloc(len + 1);  // exact size so ASan sees any overrun
    memcpy(buf, data, len);
    buf[len] = '\0';
    switch (t) {
    case T_HTTP: {
        // method byte, then path \0 query \0 origin \0 body
        if (len < 1) break;
        char *f[4];
        split(buf + 1, len - 1, f, 4);
        static const rm_method_t M[] = { RM_GET, RM_POST, RM_PUT, RM_PATCH, RM_OTHER };
        rm_req_t q = { .method = M[(uint8_t)buf[0] % 5], .path = f[0], .query = f[1][0] ? f[1] : NULL,
                       .origin = f[2][0] ? f[2] : NULL, .self_name = "tamaorb", .self_ip = "10.0.0.2",
                       .body = (const uint8_t *)f[3], .body_len = (size_t)(buf + len - f[3]), .state_ok = state_ok };
        rm_resp_t r = {0};
        fake_reset();
        rm_http_handle(&fake_ops, &q, &r);
        if (r.status == 0) abort();
        free(r.body);
        break;
    }
    case T_MCP: {
        char *out = NULL;
        fake_reset();
        int st = rm_mcp_handle(&fake_ops, buf, len, &out);
        if (st != 200 && st != 202 && st != 500) abort();
        free(out);
        break;
    }
    case T_SAVE: {
        ps_meta_t m;
        const uint8_t *pl;
        size_t pn;
        (void)rm_save_check((const uint8_t *)buf, len, P1_SHA, state_ok, &m);
        if (len >= PS_OVERHEAD) {  // again with a valid CRC, so mutations get past the checksum
            uint32_t c = ps_crc32((const uint8_t *)buf, len - 4);
            for (int i = 0; i < 4; i++) buf[len - 4 + i] = (char)(c >> (8 * i));
            (void)rm_save_check((const uint8_t *)buf, len, P1_SHA, state_ok, &m);
        }
        if (ps_record_decode((const uint8_t *)buf, len, &m, &pl, &pn) && (pl < (uint8_t *)buf || pl + pn > (uint8_t *)buf + len)) abort();
        break;
    }
    case T_PRESS: {
        rm_press_req_t r;
        rm_press_defaults(&r);
        if (rm_press_parse(&r, buf) && !rm_press_validate(&r)) {
            rm_press_t p;
            rm_press_begin(&p, &r, 1000);
            for (uint32_t ms = 1000; ms < 1000 + 12000 && !rm_press_done(&p, ms); ms += 37) (void)rm_press_mask(&p, ms);
        }
        cJSON *j = cJSON_ParseWithLength(buf, len);
        if (j) { rm_press_defaults(&r); (void)rm_json_press_req(j, &r); cJSON_Delete(j); }
        rm_restore_mode_t mode;
        (void)rm_restore_mode_parse(buf, &mode);
        break;
    }
    case T_ORIGIN: {
        char *f[3];
        split(buf, len, f, 3);
        (void)rm_origin_allowed(f[0], f[1][0] ? f[1] : "tamaorb", f[2][0] ? f[2] : "10.0.0.2");
        break;
    }
    case T_SETTING: {
        char *f[2];
        split(buf, len, f, 2);
        st_settings_t s;
        st_defaults(&s, (uint8_t)len);
        if (st_set(&s, f[0], f[1])) {
            char out[96];
            for (int i = 0; i < st_key_count(); i++)
                if (!st_get(&s, st_key(i), out, sizeof out) || strlen(out) >= sizeof out) abort();
        }
        cJSON *j = cJSON_ParseWithLength(buf, len);
        if (j) {
            bool disp;
            st_defaults(&s, 1);
            (void)rm_json_patch_settings(j, &s, &disp);
            cJSON *o = rm_json_settings(&s);
            cJSON_Delete(o);
            cJSON_Delete(j);
        }
        break;
    }
    case T_HOSTLINK: {
        hl_session_t *s = malloc(hl_session_size());
        st_defaults(&hl_cur, 1);
        hl_init(s, &HL_OPS);
        char *line = buf;
        for (size_t i = 0; i <= len; i++) {
            if (i == len || buf[i] == '\n') {
                buf[i] = '\0';
                hl_feed_line(s, line, hl_reply, NULL);
                line = buf + i + 1;
            }
        }
        free(s);
        break;
    }
    }
    free(buf);
}

// ---- seeds ----
typedef struct { uint8_t *d; size_t n; int t; } input_t;
static input_t *corpus;
static size_t ncorpus, capcorpus;
static void add(int t, const uint8_t *d, size_t n)
{
    if (ncorpus == capcorpus) { capcorpus = capcorpus ? capcorpus * 2 : 256; corpus = realloc(corpus, capcorpus * sizeof *corpus); }
    corpus[ncorpus].d = malloc(n ? n : 1);
    memcpy(corpus[ncorpus].d, d, n);
    corpus[ncorpus].n = n;
    corpus[ncorpus].t = t;
    ncorpus++;
}
static void add_s(int t, const char *s, size_t n) { add(t, (const uint8_t *)s, n); }
#define SEED(t, lit) add_s(t, lit, sizeof(lit) - 1)

static void seeds(void)
{
    // REST: method byte, path \0 query \0 origin \0 body
    SEED(T_HTTP, "\0/api/status\0\0\0");
    SEED(T_HTTP, "\0/api/look\0format=png\0\0");
    SEED(T_HTTP, "\0/api/settings\0\0\0");
    SEED(T_HTTP, "\3/api/settings\0\0http://tamaorb.local\0{\"volume\":0,\"palette\":\"night\",\"name\":\"ball\"}");
    SEED(T_HTTP, "\1/api/press\0\0http://10.0.0.2\0{\"buttons\":\"A B C AC\",\"hold_ms\":120,\"gap_ms\":200}");
    SEED(T_HTTP, "\1/api/pets/switch\0\0\0{\"rom\":\"tama.bin\"}");
    SEED(T_HTTP, "\0/api/pets\0\0\0");
    SEED(T_HTTP, "\0/api/save\0sha1=4b4979cf92dc9d2fb6d7295a38f209f3da144f72\0\0");
    SEED(T_HTTP, "\2/api/save\0sha1=4b4979cf92dc9d2fb6d7295a38f209f3da144f72&mode=frozen\0\0TORB");
    SEED(T_HTTP, "\1/api/wifi\0\0\0{\"ssid\":\"net\",\"password\":\"pw\"}");
    SEED(T_HTTP, "\1/api/restart\0\0http://evil.example\0");
    // MCP
    SEED(T_MCP, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},\"clientInfo\":{\"name\":\"f\",\"version\":\"1\"}}}");
    SEED(T_MCP, "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}");
    SEED(T_MCP, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}");
    SEED(T_MCP, "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"look\",\"arguments\":{}}}");
    SEED(T_MCP, "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\"press\",\"arguments\":{\"buttons\":\"A A B\",\"hold_ms\":100}}}");
    SEED(T_MCP, "{\"jsonrpc\":\"2.0\",\"id\":\"x\",\"method\":\"tools/call\",\"params\":{\"name\":\"set_settings\",\"arguments\":{\"brightness\":40}}}");
    SEED(T_MCP, "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":{\"name\":\"switch_pet\",\"arguments\":{\"rom\":\"tamag2.bin\"}}}");
    SEED(T_MCP, "{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"resources/list\"}");
    SEED(T_MCP, "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"resources/read\",\"params\":{\"uri\":\"tamaorb://guide/P1\"}}");
    SEED(T_MCP, "[{\"jsonrpc\":\"2.0\",\"id\":8,\"method\":\"ping\"}]");
    SEED(T_MCP, "{\"jsonrpc\":\"2.0\",\"id\":9,\"result\":{}}");
    // save record: encode a valid one
    {
        uint8_t pl[64], rec[256];
        for (int i = 0; i < 64; i++) pl[i] = (uint8_t)i;
        ps_meta_t m = { .pet_id = 0, .time_trusted = true, .seq = 7, .unix_time = 1794783329 };
        memcpy(m.rom_sha1, P1_SHA, 20);
        size_t n = ps_record_encode(&m, pl, sizeof pl, rec, sizeof rec);
        if (n) add(T_SAVE, rec, n);
    }
    SEED(T_PRESS, "A B C AC");
    SEED(T_PRESS, "{\"buttons\":\"A\",\"hold_ms\":10000,\"gap_ms\":0,\"settle_ms\":500}");
    SEED(T_PRESS, "frozen");
    SEED(T_ORIGIN, "http://tamaorb.local:80\0tamaorb\0" "10.0.0.2");
    SEED(T_ORIGIN, "http://10.0.0.2\0\0");
    SEED(T_SETTING, "palette\0night");
    SEED(T_SETTING, "name\0tamaorb-2");
    SEED(T_SETTING, "ntp\0pool.ntp.org");
    SEED(T_SETTING, "{\"brightness\":100,\"dim_level\":5,\"agent_feedback\":\"silent\",\"ntp\":\"x\"}");
    SEED(T_HOSTLINK, "@TO STATUS\n@TO LIST\n@TO GET\n@TO SET volume=0 palette=classic\n@TO RM tama.bin");
    SEED(T_HOSTLINK, "@TO PUT tama.bin 4 4b4979cf92dc9d2fb6d7295a38f209f3da144f72\n@TO D 0 AAECAw==\n@TO END");
    SEED(T_HOSTLINK, "@TO WIFI bmV0 cHc=");
}

static const char *DICT[] = { "{", "}", "[", "]", "\"", ":", ",", "\\u0000", "\\", "null", "true", "-1", "1e999",
                              "4294967296", "\"jsonrpc\"", "\"2.0\"", "\"id\"", "\"method\"", "\"params\"", "\"arguments\"",
                              "\"name\"", "/api/", "..", "%00", "%", "&", "=", "sha1=", "mode=", "@TO ", " ", "\n",
                              "http://", ":65536", "TORB", "AC", "hold_ms", "\xff", "\xc3\x28", "0" };

static size_t mutate(uint8_t *d, size_t n, size_t cap)
{
    int rounds = 1 + (int)below(6);
    for (int r = 0; r < rounds; r++) {
        switch (below(8)) {
        case 0: if (n) d[below((uint32_t)n)] ^= (uint8_t)(1u << below(8)); break;
        case 1: if (n) d[below((uint32_t)n)] = (uint8_t)rnd(); break;
        case 2: {  // an interesting byte
            static const uint8_t I[] = { 0, 1, 0x7f, 0x80, 0xff, '"', '\\', '0', '9', '{' };
            if (n) d[below((uint32_t)n)] = I[below(sizeof I)];
            break;
        }
        case 3: if (n > 1) { size_t a = below((uint32_t)n), k = 1 + below((uint32_t)(n - a)); memmove(d + a, d + a + k, n - a - k); n -= k; } break;
        case 4: {  // insert a dictionary token
            const char *t = DICT[below(sizeof DICT / sizeof *DICT)];
            size_t k = strlen(t), a = below((uint32_t)n + 1);
            if (n + k > cap) break;
            memmove(d + a + k, d + a, n - a);
            memcpy(d + a, t, k);
            n += k;
            break;
        }
        case 5: {  // duplicate a run of bytes
            if (!n) break;
            size_t a = below((uint32_t)n), k = 1 + below((uint32_t)(n - a));
            if (k > 64) k = 64;
            size_t at = below((uint32_t)n + 1);
            if (n + k > cap) break;
            memmove(d + at + k, d + at, n - at);
            memmove(d + at, d + a + (at <= a ? k : 0), k);
            n += k;
            break;
        }
        case 6: {  // splice in bytes from another corpus entry
            const input_t *o = &corpus[below((uint32_t)ncorpus)];
            size_t a = below((uint32_t)n + 1), k = below((uint32_t)o->n + 1);
            if (k > 128) k = 128;
            if (a + k > cap) break;
            memcpy(d + a, o->d + below((uint32_t)(o->n - k + 1)), k);
            if (a + k > n) n = a + k;
            break;
        }
        case 7: if (n && n + 64 <= cap) { size_t a = below((uint32_t)n + 1), k = 1 + below(64); memmove(d + a + k, d + a, n - a); memset(d + a, d[a ? a - 1 : 0], k); n += k; } break;
        }
    }
    return n;
}

static int cur_t = -1;
static const uint8_t *cur_d;
static size_t cur_n;
static void dump_crash(void)
{
    if (cur_t < 0) return;
    FILE *f = fopen("fuzz-crash.bin", "wb");  // first byte: target, then the input
    if (f) { fputc(cur_t, f); fwrite(cur_d, 1, cur_n, f); fclose(f); }
    fprintf(stderr, "fuzz_parsers: crashing input (target %s, %zu bytes) written to fuzz-crash.bin\n", TNAME[cur_t], cur_n);
}
static void on_abort(int sig) { dump_crash(); signal(sig, SIG_DFL); raise(sig); }

// Replays one saved crash: fuzz_parsers --replay fuzz-crash.bin
static int replay(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 2; }
    static uint8_t d[MAX_IN + 1];
    size_t n = fread(d, 1, sizeof d, f);
    fclose(f);
    if (n < 1 || d[0] >= T_COUNT) return 2;
    run_one(d[0], d + 1, n - 1);
    printf("replay: no crash\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 2 && strcmp(argv[1], "--replay") == 0) return replay(argv[2]);
    __sanitizer_set_death_callback(dump_crash);
    signal(SIGABRT, on_abort);
    signal(SIGSEGV, on_abort);
    long iters = argc > 1 ? atol(argv[1]) : 200000;
    rng = argc > 2 ? (uint64_t)atoll(argv[2]) | 1 : 1;
    seeds();
    for (size_t i = 0; i < ncorpus; i++) { memset(cov, 0, sizeof cov); run_one(corpus[i].t, corpus[i].d, corpus[i].n); new_coverage(); }
    size_t start = ncorpus;
    static uint8_t buf[MAX_IN];
    int per_t[T_COUNT] = {0};
    for (long it = 0; it < iters; it++) {
        const input_t *base = &corpus[below((uint32_t)ncorpus)];
        int t = base->t;  // add() may realloc the corpus, so don't keep base
        size_t n = base->n < MAX_IN ? base->n : MAX_IN;
        memcpy(buf, base->d, n);
        n = mutate(buf, n, MAX_IN);
        cur_t = t; cur_d = buf; cur_n = n;
        memset(cov, 0, sizeof cov);
        prev_loc = 0;
        run_one(t, buf, n);
        if (new_coverage()) { add(t, buf, n); per_t[t]++; }
    }
    printf("fuzz_parsers: %ld runs, corpus %zu -> %zu (", iters, start, ncorpus);
    for (int t = 0; t < T_COUNT; t++) printf("%s%s +%d", t ? ", " : "", TNAME[t], per_t[t]);
    printf("), no crashes\n");
    return 0;
}
