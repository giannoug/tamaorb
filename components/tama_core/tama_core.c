// SPDX-License-Identifier: GPL-3.0-or-later
#include <string.h>
#include "tama_core.h"
#include "tamalib.h"
#include "tc_time.h"

#define STATE_VERSION     1
#define PUBLISH_TICKS     (32768u / 30u)   // publish a frame every 1/30 s of emulated time
#define SLEEP_BATCH_US    2000             // only really sleep once this far ahead

static const tc_platform_t *g_plat;
static bool g_realtime;

static tc_frame_t g_back, g_front;
static uint8_t g_front_ram[MEM_RAM_SIZE / 2];  // packed like state->memory, copied with the frame
static uint32_t g_front_seq;
static uint32_t g_last_publish_tick;
static bool g_published_once;

static tc_tone_t g_tone;
static volatile uint8_t g_posted;
static volatile uint8_t g_remote;  // agent presses, ORed with the touch mask
static uint8_t g_applied;

static uint8_t g_boot[1024];
static size_t g_boot_len;

static void lock(void)   { if (g_plat->lock) g_plat->lock(); }
static void unlock(void) { if (g_plat->unlock) g_plat->unlock(); }

/* ---- tamalib HAL ---- */

static void *hal_malloc(u32_t size) { (void)size; return NULL; }  // breakpoints unused
static void hal_free(void *p) { (void)p; }
static void hal_halt(void) {}
static bool_t hal_is_log_enabled(log_level_t level) { (void)level; return 0; }
static void hal_log(log_level_t level, char *buff, ...) { (void)level; (void)buff; }
// tamalib timestamps are in 1/32768 s units (TC_TICK_HZ). tamalib computes each wait as
// cycles * ts_freq / cpu_freq with integer division; in microseconds that truncated every
// instruction and ran the emulator 0.35 % fast. At ts_freq == 32768 Hz the division is exact.
static uint32_t us_to_ts(uint64_t us) { return (uint32_t)(us * TC_TICK_HZ / 1000000u); }

// In fast mode (catch-up) tamalib still asks for the time after every instruction, but only keeps it as the
// reference for the next real-time wait, which tc_set_realtime(true) re-syncs anyway. Skipping the clock
// read there saves one platform call per emulated instruction.
static timestamp_t hal_get_timestamp(void) { return g_realtime ? us_to_ts(g_plat->now_us()) : 0; }

static void hal_sleep_until(timestamp_t ts)
{
    uint64_t now = g_plat->now_us();
    int32_t ahead_ticks = (int32_t)(ts - us_to_ts(now));
    if (ahead_ticks <= 0) return;
    uint64_t ahead_us = ((uint64_t)ahead_ticks * 1000000u + TC_TICK_HZ - 1) / TC_TICK_HZ;
    if (ahead_us >= SLEEP_BATCH_US) g_plat->sleep_until_us(now + ahead_us);
}

static void hal_update_screen(void) {}

static void hal_set_lcd_matrix(u8_t x, u8_t y, bool_t val)
{
    if (x >= 32 || y >= 16) return;
    if (val) g_back.rows[y] |= 1u << x;
    else     g_back.rows[y] &= ~(1u << x);
}

static void hal_set_lcd_icon(u8_t icon, bool_t val)
{
    if (icon >= 8) return;
    if (val) g_back.icons |= (uint8_t)(1u << icon);
    else     g_back.icons &= (uint8_t)~(1u << icon);
}

static bool g_quiet;  // catch-up is silent; the final buzzer state is reported when it ends

static void tone_changed(void)
{
    if (g_plat->tone && !g_quiet) g_plat->tone(g_tone.freq_dhz, g_tone.on);
}

static void hal_set_frequency(u32_t freq)
{
    lock();
    g_tone.freq_dhz = freq;
    unlock();
    tone_changed();
}

static void hal_play_frequency(bool_t en)
{
    lock();
    g_tone.on = en;
    unlock();
    tone_changed();
}

static int hal_handler(void) { return 0; }

static hal_t g_hal_impl = {
    .malloc = hal_malloc, .free = hal_free, .halt = hal_halt,
    .is_log_enabled = hal_is_log_enabled, .log = hal_log,
    .sleep_until = hal_sleep_until, .get_timestamp = hal_get_timestamp,
    .update_screen = hal_update_screen, .set_lcd_matrix = hal_set_lcd_matrix,
    .set_lcd_icon = hal_set_lcd_icon, .set_frequency = hal_set_frequency,
    .play_frequency = hal_play_frequency, .handler = hal_handler,
};

/* ---- frames and buttons ---- */

static void publish(void)
{
    lock();
    g_front = g_back;
    memcpy(g_front_ram, tamalib_get_state()->memory, sizeof g_front_ram);
    g_front_seq++;
    unlock();
    g_last_publish_tick = tc_tick_counter();
    g_published_once = true;
}

static void apply_buttons(void)
{
    lock();
    uint8_t want = g_posted | g_remote;
    unlock();
    uint8_t diff = want ^ g_applied;
    for (int b = 0; b < 3; b++) {
        if (diff & (1u << b))
            tamalib_set_button((button_t)b, (want & (1u << b)) ? BTN_STATE_PRESSED : BTN_STATE_RELEASED);
    }
    g_applied = want;
}

bool tc_init(const uint16_t *program, const tc_platform_t *plat)
{
    g_plat = plat;
    tamalib_register_hal(&g_hal_impl);
    if (tamalib_init(program, NULL, TC_TICK_HZ)) return false;
    tamalib_set_exec_mode(EXEC_MODE_RUN);
    tc_set_realtime(true);
    g_boot_len = tc_state_save(g_boot, sizeof g_boot);
    return g_boot_len > 0;
}

void tc_reset_to_boot(void)
{
    if (g_boot_len) tc_state_load(g_boot, g_boot_len);
}

void tc_set_realtime(bool realtime)
{
    g_realtime = realtime;
    tamalib_set_speed(realtime ? 1 : 0);
    tamalib_set_exec_mode(EXEC_MODE_RUN);  // re-syncs tamalib's reference timestamp
}

#ifdef TC_TESTING
static uint32_t (*g_tick_source)(void);
static void (*g_step_hook)(void);
void tc_debug_set_tick_source(uint32_t (*fn)(void)) { g_tick_source = fn; }
void tc_debug_set_step_hook(void (*fn)(void)) { g_step_hook = fn; }
static uint32_t ticks_now(void) { return g_tick_source ? g_tick_source() : tc_tick_counter(); }
#else
static uint32_t ticks_now(void) { return tc_tick_counter(); }
#endif

static void step_once(void)
{
    tamalib_step();
#ifdef TC_TESTING
    if (g_step_hook) g_step_hook();
#endif
    if (!g_published_once || (uint32_t)(tc_tick_counter() - g_last_publish_tick) >= PUBLISH_TICKS) publish();
}

void tc_step(uint32_t n)
{
    apply_buttons();
    while (n--) step_once();
}

uint64_t tc_catchup(uint64_t ticks, tc_catchup_cb cb, void *ctx, uint32_t cb_every_ticks)
{
    tc_set_realtime(false);
    g_quiet = true;
    uint64_t done = 0;
    uint64_t next_cb = cb_every_ticks ? cb_every_ticks : UINT64_MAX;
    uint32_t prev = ticks_now();
    while (done < ticks) {  // input is ignored during catch-up
        step_once();
        uint32_t now = ticks_now();
        done += tc_ticks_since(prev, now);
        prev = now;
        if (cb && done >= next_cb) {
            if (!cb(done, ticks, ctx)) break;
            next_cb += cb_every_ticks;
        }
    }
    g_quiet = false;
    tone_changed();
    tc_set_realtime(true);
    return done;
}

void tc_post_buttons(uint8_t mask)
{
    lock();
    g_posted = mask & 0x07;
    unlock();
}

void tc_post_remote_buttons(uint8_t mask)
{
    lock();
    g_remote = mask & 0x07;
    unlock();
}

size_t tc_get_ram(uint8_t *out, size_t n)
{
    if (!g_plat) return 0;
    if (n > MEM_RAM_SIZE) n = MEM_RAM_SIZE;
    // Only the packed bytes are copied under the lock (core 1 takes it to publish); they're unpacked after.
    uint8_t packed[sizeof g_front_ram];
    size_t bytes = (n + 1) / 2;  // the bytes holding nibbles 0..n-1, two per byte from address 0
    lock();
    if (g_front_seq == 0) n = bytes = 0;  // nothing published yet
    memcpy(packed, g_front_ram, bytes);
    unlock();
    for (size_t a = 0; a < n; a++) out[a] = GET_RAM_MEMORY(packed, a);
    return n;
}

void tc_get_frame(tc_frame_t *out)
{
    lock();
    *out = g_front;
    unlock();
}

bool tc_frame_changed(uint32_t *seq)
{
    lock();
    uint32_t cur = g_front_seq;
    unlock();
    if (cur == *seq) return false;
    *seq = cur;
    return true;
}

void tc_get_tone(tc_tone_t *out)
{
    lock();
    *out = g_tone;
    unlock();
}

uint32_t tc_tick_counter(void) { return *tamalib_get_state()->tick_counter; }

/* ---- save states: field order and encoding follow TamaTool src/state.c ---- */

typedef struct { uint8_t *p; size_t n, cap; bool ok; } wr_t;
typedef struct { const uint8_t *p; size_t n, len; bool ok; } rd_t;

static void w8(wr_t *w, uint32_t v) { if (w->n < w->cap) w->p[w->n] = (uint8_t)v; else w->ok = false; w->n++; }
static void w16(wr_t *w, uint32_t v) { w8(w, v); w8(w, v >> 8); }
static void w32(wr_t *w, uint32_t v) { w16(w, v); w16(w, v >> 16); }
static uint32_t r8(rd_t *r) { if (r->n < r->len) return r->p[r->n++]; r->ok = false; return 0; }
static uint32_t r16(rd_t *r) { uint32_t v = r8(r); return v | r8(r) << 8; }
static uint32_t r32(rd_t *r) { uint32_t v = r16(r); return v | r16(r) << 16; }

// One routine for both directions keeps save and load in lockstep.
static void walk(state_t *s, wr_t *w, rd_t *r)
{
#define U8(f, m)  do { if (w) w8(w, *(f) & (m)); else *(f) = r8(r) & (m); } while (0)
#define U16(f, m) do { if (w) w16(w, *(f) & (m)); else *(f) = r16(r) & (m); } while (0)
#define U32(f)    do { if (w) w32(w, *(f)); else *(f) = r32(r); } while (0)
    U16(s->pc, 0x1FFF); U16(s->x, 0xFFF); U16(s->y, 0xFFF);
    U8(s->a, 0xF); U8(s->b, 0xF); U8(s->np, 0x1F); U8(s->sp, 0xFF); U8(s->flags, 0xF);
    U32(s->tick_counter);
    U32(s->clk_timer_2hz_timestamp); U32(s->clk_timer_4hz_timestamp); U32(s->clk_timer_8hz_timestamp);
    U32(s->clk_timer_16hz_timestamp); U32(s->clk_timer_32hz_timestamp); U32(s->clk_timer_64hz_timestamp);
    U32(s->clk_timer_128hz_timestamp); U32(s->clk_timer_256hz_timestamp);
    U32(s->prog_timer_timestamp);
    U8(s->prog_timer_enabled, 0x1); U8(s->prog_timer_data, 0xFF); U8(s->prog_timer_rld, 0xFF);
    U32(s->call_depth);
    for (int i = 0; i < INT_SLOT_NUM; i++) {
        U8(&s->interrupts[i].factor_flag_reg, 0xF);
        U8(&s->interrupts[i].mask_reg, 0xF);
        U8(&s->interrupts[i].triggered, 0x1);
        U8(&s->interrupts[i].vector, 0xFF);
    }
    U8(s->cpu_halted, 0x1);
    for (int i = 0; i < MEM_BUFFER_SIZE; i++) U8(&s->memory[i], 0xFF);
#undef U8
#undef U16
#undef U32
}

size_t tc_state_size(void)
{
    static uint8_t scratch[1024];
    wr_t w = { scratch, 0, sizeof scratch, true };
    w8(&w, STATE_VERSION);
    walk(tamalib_get_state(), &w, NULL);
    return w.n;
}

size_t tc_state_save(uint8_t *buf, size_t cap)
{
    wr_t w = { buf, 0, cap, true };
    w8(&w, STATE_VERSION);
    walk(tamalib_get_state(), &w, NULL);
    return w.ok ? w.n : 0;
}

bool tc_state_load(const uint8_t *buf, size_t len)
{
    if (len != tc_state_size() || buf[0] != STATE_VERSION) return false;
    rd_t r = { buf, 1, len, true };
    walk(tamalib_get_state(), NULL, &r);
    if (!r.ok) return false;
    tamalib_refresh_hw();                 // redraw the LCD and buzzer from the restored memory
    tamalib_set_exec_mode(EXEC_MODE_RUN); // re-sync timing
    publish();
    return true;
}

// The state length is fixed for a build; tc_init measured it (g_boot_len), so no walk over live state here.
bool tc_state_compatible(const uint8_t *buf, size_t len)
{
    return buf && g_boot_len && len == g_boot_len && buf[0] == STATE_VERSION;
}

uint8_t tc_applied_buttons(void) { return g_applied; }

#ifdef TC_TESTING
uint8_t tc_debug_applied_buttons(void) { return g_applied; }
#endif

typedef struct {
    tc_catchup_cb cb;
    void *ctx;
    uint64_t offset, total;
    bool stopped;
} converge_ctx_t;

static bool converge_cb(uint64_t done, uint64_t total, void *ctx)
{
    converge_ctx_t *c = ctx;
    if (!c->cb) return true;
    bool go = c->cb(c->offset + done, c->total, c->ctx);
    c->stopped = !go;
    return go;
}

uint64_t tc_catchup_converge(uint64_t ticks, uint64_t (*wall_us)(void), tc_catchup_cb cb, void *ctx,
                             uint32_t cb_every_ticks)
{
    converge_ctx_t c = { .cb = cb, .ctx = ctx, .total = ticks };
    uint64_t pass = ticks;
    while (pass >= TC_TICK_HZ) {
        uint64_t t0 = wall_us();
        c.offset += tc_catchup(pass, converge_cb, &c, cb_every_ticks);
        if (c.stopped) break;
        pass = (wall_us() - t0) * TC_TICK_HZ / 1000000u;  // the time this pass took, still to replay
        c.total = c.offset + pass;
    }
    return c.offset;
}
