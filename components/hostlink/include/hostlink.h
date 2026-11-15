// SPDX-License-Identifier: GPL-3.0-or-later
// Line protocol over the USB serial console (spec §7.4). Portable: no ESP-IDF headers.
// Requests and replies start with "@TO "; every other line (logs) is ignored by both sides.
//
//   @TO PUT <name> <size> <sha1hex>   -> @TO OK | @TO ERR name|size|busy|io
//   @TO D <seq> <base64 <= 768 bytes> -> @TO OK <seq> | @TO ERR seq|size|data|state
//   @TO END                           -> @TO OK <model> | @TO ERR size|sha1|unknown|io|state
//   @TO LIST                          -> @TO ITEM <name> <model> <sha1hex> ... then @TO OK
//   @TO RM <name>                     -> @TO OK | @TO ERR name|notfound
//   @TO WIFI <ssid_b64> [<pass_b64>]  -> @TO OK | @TO ERR data|io
//   @TO STATUS                        -> @TO STATUS <key=value ...>
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HL_MAX_LINE   1100
#define HL_MAX_CHUNK  768

typedef void (*hl_emit_fn)(void *ectx, const char *line);

typedef struct {
    bool (*store)(void *ctx, const char *name, const uint8_t *data, size_t len);  // verified P1/P2 ROM
    void (*list)(void *ctx, hl_emit_fn emit, void *ectx);   // emit "<name> <model> <sha1hex>" per ROM
    bool (*remove)(void *ctx, const char *name);
    bool (*wifi)(void *ctx, const char *ssid, const char *pass);
    void (*status)(void *ctx, char *out, size_t cap);        // "rom=P1 wifi=1 ..."
    void *ctx;
} hl_ops_t;

typedef struct hl_session hl_session_t;
size_t hl_session_size(void);
void   hl_init(hl_session_t *s, const hl_ops_t *ops);
void   hl_feed_line(hl_session_t *s, const char *line, hl_emit_fn reply, void *rctx);

// RFC 4648 base64. Returns decoded length or -1 on malformed input / overflow.
int    hl_b64_decode(const char *in, uint8_t *out, size_t cap);
size_t hl_b64_encode(const uint8_t *in, size_t len, char *out, size_t cap);
