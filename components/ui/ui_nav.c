// SPDX-License-Identifier: GPL-3.0-or-later
// Navigation between the pet, the glance and the settings pages, and the edits made on those pages.
#include "ui.h"
#include <stdio.h>
#include <string.h>

static void menu_open(ui_nav_t *n, ui_page_t page)
{
    n->screen = UI_SCREEN_MENU;
    n->view.page = page;
    n->view.scroll = 0;
    n->view.pressed = -1;
    n->active_ctl = -1;
}

void ui_nav_init(ui_nav_t *n)
{
    memset(n, 0, sizeof *n);
    n->view.pressed = -1;
    n->active_ctl = -1;
}

static void roms_clear(ui_menu_view_t *v)
{
    v->roms = NULL;
    v->rom_count = 0;
    v->rom_first = false;
}

void ui_nav_to_pet(ui_nav_t *n)
{
    n->screen = UI_SCREEN_PET;
    n->view.page = UI_PAGE_LIST;
    n->view.scroll = 0;
    n->view.pressed = -1;
    n->view.region = NULL;
    roms_clear(&n->view);
    n->active_ctl = -1;
}

void ui_nav_open_roms(ui_nav_t *n, bool from_pet)
{
    menu_open(n, UI_PAGE_ROM);
    n->view.region = NULL;
    roms_clear(&n->view);
    n->view.rom_current = -1;
    n->view.rom_pick = -1;
    n->rom_from_pet = from_pet;
}

static const char *CTL_KEY[] = {
    [UI_CTL_MUTE] = "muted", [UI_CTL_VOLUME] = "volume", [UI_CTL_BRIGHTNESS] = "brightness",
    [UI_CTL_DIM_LEVEL] = "dim_level", [UI_CTL_DIM_USB] = "dim_usb_s", [UI_CTL_DIM_BATTERY] = "dim_battery_s",
    [UI_CTL_OFF_BATTERY] = "off_battery_s", [UI_CTL_PALETTE] = "palette",
};

// out = cur with only `key` set. NONE when the value is rejected or changes nothing.
static ui_act_t edit(const st_settings_t *cur, st_settings_t *out, bool *display_changed, const char *key, const char *val)
{
    *out = *cur;
    if (!st_set(out, key, val) || !memcmp(out, cur, sizeof *out)) return UI_ACT_NONE;
    *display_changed = st_is_display(key);
    return UI_ACT_SETTINGS;
}

static ui_act_t edit_ctl(const st_settings_t *cur, st_settings_t *out, bool *display_changed, int ctl, int value)
{
    static const char *const PALETTES[] = { "classic", "neutral", "night" };
    char buf[16];
    if (ctl == UI_CTL_MUTE) snprintf(buf, sizeof buf, "%s", value ? "true" : "false");
    else if (ctl == UI_CTL_PALETTE) snprintf(buf, sizeof buf, "%s", PALETTES[value >= 0 && value <= 2 ? value : 0]);
    else snprintf(buf, sizeof buf, "%d", value);
    return edit(cur, out, display_changed, CTL_KEY[ctl], buf);
}

static bool is_list(ui_page_t p)
{
    return p == UI_PAGE_LIST || p == UI_PAGE_TZ_REGION || p == UI_PAGE_TZ_CITY || p == UI_PAGE_ROM;
}

// Sliders follow a drag; switches, tiles and the ROM pills take taps only.
static bool is_slider(int ctl)
{
    return ctl >= 0 && ctl != UI_CTL_MUTE && ctl != UI_CTL_PALETTE && ctl != UI_CTL_ROM_CANCEL && ctl != UI_CTL_ROM_SWITCH;
}

static ui_act_t tap_row(ui_nav_t *n, int row, const st_settings_t *cur, st_settings_t *out, bool *dc)
{
    ui_menu_view_t *v = &n->view;
    if (v->page == UI_PAGE_LIST) {
        static const ui_page_t PAGES[UI_ROW_COUNT] = {
            [UI_ROW_SOUND] = UI_PAGE_SOUND, [UI_ROW_BRIGHTNESS] = UI_PAGE_BRIGHTNESS, [UI_ROW_DIMMING] = UI_PAGE_DIMMING,
            [UI_ROW_PALETTE] = UI_PAGE_PALETTE, [UI_ROW_TZ] = UI_PAGE_TZ_REGION, [UI_ROW_PANEL] = UI_PAGE_PANEL,
        };
        if (row == UI_ROW_ROM) { ui_nav_open_roms(n, false); return UI_ACT_REDRAW; }
        if (row < 0 || row >= UI_ROW_COUNT) return UI_ACT_NONE;
        menu_open(n, PAGES[row]);
        return UI_ACT_REDRAW;
    }
    if (v->page == UI_PAGE_ROM) {
        if (v->rom_first) { v->rom_pick = row; return UI_ACT_ROM_SWITCH; }
        if (row == v->rom_current) { ui_nav_to_pet(n); return UI_ACT_REDRAW; }  // the running pet: nothing changes
        menu_open(n, UI_PAGE_ROM_CONFIRM);
        v->rom_pick = row;
        return UI_ACT_REDRAW;
    }
    if (v->page == UI_PAGE_TZ_REGION) {
        const char *r = ui_menu_region_at(row);
        if (!r) return UI_ACT_NONE;
        if (!strcmp(r, "UTC")) return edit(cur, out, dc, "tz", "Etc/UTC");
        menu_open(n, UI_PAGE_TZ_CITY);
        v->region = r;
        return UI_ACT_REDRAW;
    }
    const char *c = ui_menu_city_at(v->region, row);
    return c ? edit(cur, out, dc, "tz", c) : UI_ACT_NONE;
}

ui_act_t ui_nav_step(ui_nav_t *n, const ui_gevent_t *ev, bool boot_short, const st_settings_t *cur,
                     st_settings_t *out, bool *display_changed)
{
    ui_menu_view_t *v = &n->view;
    v->cfg = cur;
    *display_changed = false;

    switch (n->screen) {
    case UI_SCREEN_PET:
        if (ev->kind == UI_G_SWIPE_DOWN) { n->screen = UI_SCREEN_GLANCE; return UI_ACT_REDRAW; }
        if (ev->kind == UI_G_SWIPE_UP) { menu_open(n, UI_PAGE_LIST); v->region = NULL; return UI_ACT_REDRAW; }
        return UI_ACT_NONE;
    case UI_SCREEN_GLANCE:
        if (boot_short || ev->kind == UI_G_SWIPE_UP) { ui_nav_to_pet(n); return UI_ACT_REDRAW; }
        return UI_ACT_NONE;
    case UI_SCREEN_MENU: break;
    }

    if (boot_short || ev->kind == UI_G_SWIPE_DOWN) {
        if (v->page == UI_PAGE_ROM && v->rom_first) return UI_ACT_NONE;             // nothing to go back to
        if (v->page == UI_PAGE_ROM_CONFIRM) { menu_open(n, UI_PAGE_ROM); return UI_ACT_REDRAW; }  // cancel
        ui_nav_to_pet(n);
        return UI_ACT_REDRAW;
    }

    // A drag belongs to the control under the point where its touch went down, or to none (a touch that starts
    // in an edge band or between controls). Set on each touch's first event, so a touch whose release never
    // arrived (swipe, swallowed or inert touch) can't leave its lock on the next one.
    if (ev->first)
        n->active_ctl = ev->kind == UI_G_DRAG && !is_list(v->page) ? ui_menu_control_at(v, ev->sx, ev->sy, NULL) : -1;

    switch (ev->kind) {
    case UI_G_TAP: {
        v->pressed = -1;
        if (ui_menu_back_at(v, ev->x, ev->y)) {
            if (v->page == UI_PAGE_TZ_CITY) { menu_open(n, UI_PAGE_TZ_REGION); v->region = NULL; }
            else if (v->page == UI_PAGE_ROM && n->rom_from_pet) ui_nav_to_pet(n);
            else { roms_clear(v); menu_open(n, UI_PAGE_LIST); }
            return UI_ACT_REDRAW;
        }
        if (is_list(v->page)) {
            int row = ui_menu_row_at(v, ev->x, ev->y);
            return row < 0 ? UI_ACT_NONE : tap_row(n, row, cur, out, display_changed);
        }
        int val, ctl = ui_menu_control_at(v, ev->x, ev->y, &val);
        if (ctl == UI_CTL_ROM_CANCEL) { menu_open(n, UI_PAGE_ROM); return UI_ACT_REDRAW; }
        if (ctl == UI_CTL_ROM_SWITCH) return UI_ACT_ROM_SWITCH;
        return ctl < 0 ? UI_ACT_NONE : edit_ctl(cur, out, display_changed, ctl, val);
    }
    case UI_G_DRAG: {
        bool changed = v->pressed != -1;
        v->pressed = -1;
        if (is_list(v->page)) {
            int max = ui_menu_scroll_max(v), s = v->scroll - ev->dy;
            s = s < 0 ? 0 : s > max ? max : s;
            if (s != v->scroll) { v->scroll = s; changed = true; }
            return changed ? UI_ACT_REDRAW : UI_ACT_NONE;
        }
        int val, ctl = ui_menu_control_at(v, ev->x, ev->y, &val);
        // Only sliders follow a drag (switches, tiles and pills take taps), and only while the finger is on its own one.
        bool follow = is_slider(ctl) && ctl == n->active_ctl;
        ui_act_t a = follow ? edit_ctl(cur, out, display_changed, ctl, val) : changed ? UI_ACT_REDRAW : UI_ACT_NONE;
        if (ev->released) n->active_ctl = -1;
        return a;
    }
    default:
        return UI_ACT_NONE;
    }
}
