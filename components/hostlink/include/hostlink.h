// SPDX-License-Identifier: GPL-3.0-or-later
// Line protocol over the USB serial console. Portable: no ESP-IDF headers.
// Requests and replies start with "@TO "; every other line (logs) is ignored by both sides.
//
//   @TO PUT <name> <size> <sha1hex>   -> @TO OK | @TO ERR name|size|busy|io
//   @TO D <seq> <base64 <= 768 bytes> -> @TO OK <seq> | @TO ERR seq|size|data|state
//   @TO END                           -> @TO OK <model> [REBOOT] | @TO ERR size|sha1|unknown|io|state
//                                        (REBOOT: the device restarts to apply it; stop sending)
//   @TO LIST                          -> @TO ITEM <name> <model> <sha1hex> ... then @TO OK
//   @TO RM <name>                     -> @TO OK | @TO ERR name|notfound
//   @TO WIFI <ssid_b64> [<pass_b64>]  -> @TO OK | @TO ERR data|io
//   @TO STATUS                        -> @TO STATUS <key=value ...>
//   @TO GET                           -> @TO SET <key>=<value> per setting, then @TO OK
//   @TO SET k=v [k=v ...]             -> @TO OK | @TO ERR <first bad key> | @TO ERR data (all or nothing)
//   @TO CLOCK                         -> @TO OK <HH:MM:SS> (the pet's clock set to the ball's local time)
//                                        | @TO ERR unsupported|notime|catchup|busy
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "settings.h"
#include "tc_rom.h"

#define HL_MAX_LINE   1100
#define HL_MAX_CHUNK  768

typedef void (*hl_emit_fn)(void *ectx, const char *line);

typedef enum { HL_STORE_FAIL = 0, HL_STORE_OK, HL_STORE_OK_REBOOT } hl_store_result_t;

typedef enum { HL_CLOCK_OK, HL_CLOCK_UNSUPPORTED, HL_CLOCK_NO_TIME, HL_CLOCK_CATCHUP, HL_CLOCK_BUSY } hl_clock_t;

typedef struct {
    hl_store_result_t (*store)(void *ctx, const char *name, const uint8_t *data, size_t len);  // verified P1/P2 ROM
    void (*list)(void *ctx, hl_emit_fn emit, void *ectx);   // emit "<name> <model> <sha1hex>" per ROM
    bool (*remove)(void *ctx, const char *name);
    bool (*wifi)(void *ctx, const char *ssid, const char *pass);
    void (*status)(void *ctx, char *out, size_t cap);        // "rom=P1 wifi=1 ..."
    void (*get_settings)(void *ctx, st_settings_t *out);
    void (*put_settings)(void *ctx, const st_settings_t *s, bool display_changed);
    hl_clock_t (*clock)(void *ctx, int32_t *written_s);   // may block a few seconds; NULL: @TO ERR cmd
    // The model of an uploaded ROM; NULL (the device): tc_rom_identify's, from the known hashes. Tests set it to run
    // the upload path on made-up bytes. The SHA-1 the PUT line announced is checked either way.
    tc_model_t (*identify)(void *ctx, const uint8_t *data, size_t len);
    void *ctx;
} hl_ops_t;

typedef struct hl_session hl_session_t;
size_t hl_session_size(void);
void   hl_init(hl_session_t *s, const hl_ops_t *ops);
void   hl_feed_line(hl_session_t *s, const char *line, hl_emit_fn reply, void *rctx);

// RFC 4648 base64. Returns decoded length or -1 on malformed input / overflow.
int    hl_b64_decode(const char *in, uint8_t *out, size_t cap);
size_t hl_b64_encode(const uint8_t *in, size_t len, char *out, size_t cap);
