// SPDX-License-Identifier: GPL-3.0-or-later
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hostlink.h"
#include "tc_rom.h"

struct hl_session {
    const hl_ops_t *ops;
    bool uploading;
    char name[49];
    size_t size, got;
    uint32_t next_seq;
    uint8_t sha1[20];
    uint8_t data[TC_ROM_MAX_BYTES];
};

size_t hl_session_size(void) { return sizeof(hl_session_t); }

void hl_init(hl_session_t *s, const hl_ops_t *ops)
{
    memset(s, 0, sizeof *s);
    s->ops = ops;
}

static bool valid_name(const char *n)
{
    size_t len = strlen(n);
    if (len < 5 || len > 48 || strcmp(n + len - 4, ".bin") != 0) return false;
    for (size_t i = 0; i < len; i++)
        if (!isalnum((unsigned char)n[i]) && n[i] != '.' && n[i] != '_' && n[i] != '-') return false;
    return n[0] != '.';
}

static bool parse_hex20(const char *hex, uint8_t out[20])
{
    if (!hex || strlen(hex) != 40) return false;
    for (int i = 0; i < 20; i++) {
        char b[3] = { hex[2 * i], hex[2 * i + 1], 0 };
        if (!isxdigit((unsigned char)b[0]) || !isxdigit((unsigned char)b[1])) return false;
        out[i] = (uint8_t)strtoul(b, NULL, 16);
    }
    return true;
}

typedef struct { hl_emit_fn reply; void *rctx; } emit_ctx_t;

static void emit_item(void *e, const char *item)
{
    emit_ctx_t *c = e;
    char line[160];
    snprintf(line, sizeof line, "@TO ITEM %s", item);
    c->reply(c->rctx, line);
}

static void put(hl_session_t *s, char *args, hl_emit_fn reply, void *rctx)
{
    char *name = strtok(args, " "), *size = strtok(NULL, " "), *hex = strtok(NULL, " ");
    if (!name || !valid_name(name)) { reply(rctx, "@TO ERR name"); return; }
    unsigned long n = size ? strtoul(size, NULL, 10) : 0;
    if (!tc_rom_size_ok(n)) { reply(rctx, "@TO ERR size"); return; }
    if (!parse_hex20(hex, s->sha1)) { reply(rctx, "@TO ERR data"); return; }
    snprintf(s->name, sizeof s->name, "%s", name);
    s->size = n;
    s->got = 0;
    s->next_seq = 0;
    s->uploading = true;
    reply(rctx, "@TO OK");
}

static void chunk(hl_session_t *s, char *args, hl_emit_fn reply, void *rctx)
{
    char *seq = strtok(args, " "), *b64 = strtok(NULL, " ");
    if (!s->uploading) { reply(rctx, "@TO ERR state"); return; }
    if (!seq || strtoul(seq, NULL, 10) != s->next_seq) { reply(rctx, "@TO ERR seq"); return; }
    int n = b64 ? hl_b64_decode(b64, s->data + s->got, s->size - s->got) : -1;
    if (n < 0) { s->uploading = false; reply(rctx, "@TO ERR data"); return; }
    s->got += (size_t)n;
    char line[32];
    snprintf(line, sizeof line, "@TO OK %lu", (unsigned long)s->next_seq++);
    reply(rctx, line);
}

static void end(hl_session_t *s, hl_emit_fn reply, void *rctx)
{
    if (!s->uploading) { reply(rctx, "@TO ERR state"); return; }
    s->uploading = false;
    if (s->got != s->size) { reply(rctx, "@TO ERR size"); return; }
    uint8_t h[20];
    tc_model_t m = tc_rom_identify(s->data, s->size, h);
    if (memcmp(h, s->sha1, 20) != 0) { reply(rctx, "@TO ERR sha1"); return; }
    if (s->ops->identify) m = s->ops->identify(s->ops->ctx, s->data, s->size);
    if (!tc_model_playable(m)) { reply(rctx, "@TO ERR unknown"); return; }
    hl_store_result_t r = s->ops->store(s->ops->ctx, s->name, s->data, s->size);
    if (r == HL_STORE_FAIL) { reply(rctx, "@TO ERR io"); return; }
    char line[32];
    snprintf(line, sizeof line, "@TO OK %s%s", tc_model_name(m), r == HL_STORE_OK_REBOOT ? " REBOOT" : "");
    reply(rctx, line);
}

static void wifi(hl_session_t *s, char *args, hl_emit_fn reply, void *rctx)
{
    char *ssid64 = strtok(args, " "), *pass64 = strtok(NULL, " ");
    char ssid[33] = { 0 }, pass[65] = { 0 };
    int a = ssid64 ? hl_b64_decode(ssid64, (uint8_t *)ssid, sizeof ssid - 1) : -1;
    int b = pass64 ? hl_b64_decode(pass64, (uint8_t *)pass, sizeof pass - 1) : 0;
    if (a <= 0 || b < 0) { reply(rctx, "@TO ERR data"); return; }
    reply(rctx, s->ops->wifi(s->ops->ctx, ssid, pass) ? "@TO OK" : "@TO ERR io");
}

static void get_all(hl_session_t *s, hl_emit_fn reply, void *rctx)
{
    st_settings_t cur;
    s->ops->get_settings(s->ops->ctx, &cur);
    for (int i = 0; i < st_key_count(); i++) {
        char val[80], line[160];
        if (!st_get(&cur, st_key(i), val, sizeof val)) continue;
        snprintf(line, sizeof line, "@TO SET %s=%s", st_key(i), val);
        reply(rctx, line);
    }
    reply(rctx, "@TO OK");
}

#define HL_MAX_PAIRS 16

static void set_many(hl_session_t *s, char *args, hl_emit_fn reply, void *rctx)
{
    const char *keys[HL_MAX_PAIRS], *vals[HL_MAX_PAIRS];
    int n = 0;
    for (char *tok = strtok(args, " "); tok; tok = strtok(NULL, " ")) {
        char *eq = strchr(tok, '=');
        if (!eq || eq == tok || n == HL_MAX_PAIRS) { reply(rctx, "@TO ERR data"); return; }
        *eq = '\0';
        keys[n] = tok;
        vals[n++] = eq + 1;
    }
    if (n == 0) { reply(rctx, "@TO ERR data"); return; }
    st_settings_t before, after;
    s->ops->get_settings(s->ops->ctx, &before);
    after = before;
    int bad = 0;
    if (!st_set_many(&after, keys, vals, n, &bad)) {
        char line[96];
        snprintf(line, sizeof line, "@TO ERR %.64s", keys[bad]);
        reply(rctx, line);
        return;
    }
    // Compare through st_get: st_set leaves stale bytes after strings, so no memcmp of structs.
    bool display = false;
    for (int i = 0; i < st_key_count() && !display; i++) {
        char a[80], b[80];
        if (!st_is_display(st_key(i))) continue;
        if (st_get(&before, st_key(i), a, sizeof a) && st_get(&after, st_key(i), b, sizeof b) && strcmp(a, b) != 0)
            display = true;
    }
    s->ops->put_settings(s->ops->ctx, &after, display);
    reply(rctx, "@TO OK");
}

void hl_feed_line(hl_session_t *s, const char *line, hl_emit_fn reply, void *rctx)
{
    if (strncmp(line, "@TO ", 4) != 0) return;
    char buf[HL_MAX_LINE];
    snprintf(buf, sizeof buf, "%s", line + 4);
    buf[strcspn(buf, "\r\n")] = '\0';
    char *rest = strchr(buf, ' ');
    if (rest) *rest++ = '\0';
    else rest = buf + strlen(buf);

    if (!strcmp(buf, "PUT")) put(s, rest, reply, rctx);
    else if (!strcmp(buf, "D")) chunk(s, rest, reply, rctx);
    else if (!strcmp(buf, "END")) end(s, reply, rctx);
    else if (!strcmp(buf, "LIST")) {
        emit_ctx_t c = { reply, rctx };
        s->ops->list(s->ops->ctx, emit_item, &c);
        reply(rctx, "@TO OK");
    } else if (!strcmp(buf, "RM")) {
        if (!valid_name(rest)) reply(rctx, "@TO ERR name");
        else reply(rctx, s->ops->remove(s->ops->ctx, rest) ? "@TO OK" : "@TO ERR notfound");
    } else if (!strcmp(buf, "WIFI")) wifi(s, rest, reply, rctx);
    else if (!strcmp(buf, "GET")) get_all(s, reply, rctx);
    else if (!strcmp(buf, "SET")) set_many(s, rest, reply, rctx);
    else if (!strcmp(buf, "STATUS")) {
        char st[160], out[180];
        s->ops->status(s->ops->ctx, st, sizeof st);
        snprintf(out, sizeof out, "@TO STATUS %s", st);
        reply(rctx, out);
    } else if (!strcmp(buf, "CLOCK") && s->ops->clock) {
        static const char *const ERR[] = { [HL_CLOCK_UNSUPPORTED] = "@TO ERR unsupported", [HL_CLOCK_NO_TIME] = "@TO ERR notime",
                                           [HL_CLOCK_CATCHUP] = "@TO ERR catchup", [HL_CLOCK_BUSY] = "@TO ERR busy" };
        int32_t w = -1;
        hl_clock_t r = s->ops->clock(s->ops->ctx, &w);
        if (r == HL_CLOCK_OK && w >= 0 && w < 86400) {
            char out[24];
            snprintf(out, sizeof out, "@TO OK %02d:%02d:%02d", (int)(w / 3600), (int)(w / 60 % 60), (int)(w % 60));
            reply(rctx, out);
        } else {
            reply(rctx, r > HL_CLOCK_OK && r <= HL_CLOCK_BUSY ? ERR[r] : "@TO ERR busy");
        }
    } else reply(rctx, "@TO ERR cmd");
}
