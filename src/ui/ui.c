#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ui.h"
#include "ui_internal.h"
#include "../control.h"
#include "../util.h"

#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

ui_ctx_t g_ui;

const char *const VIEW_NAMES[VIEW_COUNT] = {
    "Power", "Fans", "Display & Light", "Snapshots", "Telemetry"
};

/* ---- footer flash -------------------------------------------------------- */

static struct notcurses *s_nc;
static struct ncplane *s_stdn;
static bool s_in_modal;

void ui_flash(const char *msg)
{
    if (!s_nc || !s_stdn)
        return;
    unsigned dimy = 0, dimx = 0;
    ncplane_dim_yx(s_stdn, &dimy, &dimx);
    if (dimy < 4 || dimx < 12)
        return;
    const palette_t *pal = ui_palette(g_prefs.theme);
    s_in_modal = false;
    ui_putln(s_stdn, 1, (int)dimy - 2, (int)dimx - 2, msg, pal->accent, true);
    notcurses_render(s_nc);
}

/* ---- palette ----------------------------------------------------------- */

const char *const THEME_NAMES[] = { "Native", "Slate", "Gruvbox", "Mono", "Light" };

static const palette_t s_themes[THEME_COUNT] = {
    /* 0: Native — respects terminal default background / transparency */
    [0] = {
        .default_bg = true,  .light = false,
        .accent = 0x88C0D0,  .accent2 = 0x81A1C1,
        .warn   = 0xEBCB8B,  .ok = 0xA3BE8C,  .err = 0xBF616A,
        .bg     = 0x161A20,  .card = 0x242A34, .border = 0x3B4252,
        .text   = 0xD8DEE9,  .muted = 0x7B8898,
    },
    /* 1: Slate — restrained matte dark */
    [1] = {
        .default_bg = false, .light = false,
        .accent = 0x9CC2CF,  .accent2 = 0x7FA4B8,
        .warn   = 0xD9A85C,  .ok = 0x88B378,  .err = 0xD46A6A,
        .bg     = 0x141619,  .card = 0x22262D, .border = 0x30363F,
        .text   = 0xD4D7DC,  .muted = 0x747C87,
    },
    /* 2: Gruvbox — warm dark */
    [2] = {
        .default_bg = false, .light = false,
        .accent = 0xD79921,  .accent2 = 0x83A598,
        .warn   = 0xFE8019,  .ok = 0xB8BB26,  .err = 0xFB4934,
        .bg     = 0x1D2021,  .card = 0x32302F, .border = 0x504945,
        .text   = 0xEBDBB2,  .muted = 0x928374,
    },
    /* 3: Mono — transparent terminal + monochrome typography */
    [3] = {
        .default_bg = true,  .light = false,
        .accent = 0xEAEAEA,  .accent2 = 0xA8A8A8,
        .warn   = 0xD9A85C,  .ok = 0xB0B0B0,  .err = 0xD46A6A,
        .bg     = 0x141414,  .card = 0x262626, .border = 0x444444,
        .text   = 0xD4D4D4,  .muted = 0x767676,
    },
    /* 4: Light — clean paper tone */
    [4] = {
        .default_bg = false, .light = true,
        .accent = 0x0969DA,  .accent2 = 0x1F6FEB,
        .warn   = 0x9A6700,  .ok = 0x1A7F37,  .err = 0xCF222E,
        .bg     = 0xF6F5F2,  .card = 0xE4E1DA, .border = 0xC8C3B9,
        .text   = 0x1F2328,  .muted = 0x666C75,
    },
};

const palette_t *ui_palette(int theme)
{
    return &s_themes[ut_clamp_i(theme, 0, THEME_COUNT - 1)];
}

uint64_t ui_ch(uint32_t fg, bool sel_bg)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    uint64_t ch = 0;
    ncchannels_set_fg_rgb(&ch, fg);
    if (sel_bg)
        ncchannels_set_bg_rgb(&ch, pal->card);
    else if (!s_in_modal && pal->default_bg)
        ncchannels_set_bg_default(&ch);
    else
        ncchannels_set_bg_rgb(&ch, pal->bg);
    return ch;
}

void ui_modal_end(void)
{
    s_in_modal = false;
}

/* ---- draw helpers ------------------------------------------------------- */

void ui_trunc(char *s, int maxlen_chars)
{
    if (maxlen_chars <= 0) {
        s[0] = '\0';
        return;
    }
    int cells = 0;
    for (char *p = s; *p; p++) {
        if ((*p & 0xC0) != 0x80)
            cells++;
        if (cells > maxlen_chars) {
            *p = '\0';
            return;
        }
    }
}

void ui_putln(struct ncplane *n, int x, int y, int w,
              const char *s, uint32_t fg, bool bold)
{
    if (w <= 0)
        return;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", s ? s : "");
    ui_trunc(buf, w);
    ncplane_set_channels(n, ui_ch(fg, false));
    ncplane_set_styles(n, bold ? NCSTYLE_BOLD : NCSTYLE_NONE);
    ncplane_putstr_yx(n, y, x, buf);
    ncplane_set_styles(n, NCSTYLE_NONE);
}

void ui_hline(struct ncplane *n, int x, int y, int w, uint32_t fg)
{
    if (w <= 0)
        return;
    ncplane_set_channels(n, ui_ch(fg, false));
    ncplane_cursor_move_yx(n, y, x);
    for (int i = 0; i < w; i++)
        ncplane_putstr(n, "─");
}

void ui_vline(struct ncplane *n, int x, int y, int h, uint32_t fg)
{
    if (h <= 0)
        return;
    ncplane_set_channels(n, ui_ch(fg, false));
    for (int i = 0; i < h; i++)
        ncplane_putstr_yx(n, y + i, x, "│");
}

void ui_box(struct ncplane *n, const rect_t *r, const char *title, bool focused)
{
    if (r->w < 4 || r->h < 3)
        return;
    const palette_t *pal = ui_palette(g_prefs.theme);

    uint64_t chb = ui_ch(focused ? pal->accent : pal->border, false);
    uint64_t cht = ui_ch(focused ? pal->accent : pal->muted, false);

    ncplane_set_channels(n, chb);
    ncplane_putstr_yx(n, r->y, r->x, "┌");
    for (int x = 1; x < r->w - 1; x++)
        ncplane_putstr(n, "─");
    ncplane_putstr(n, "┐");
    for (int y = 1; y < r->h - 1; y++) {
        ncplane_putstr_yx(n, r->y + y, r->x, "│");
        ncplane_putstr_yx(n, r->y + y, r->x + r->w - 1, "│");
    }
    ncplane_putstr_yx(n, r->y + r->h - 1, r->x, "└");
    for (int x = 1; x < r->w - 1; x++)
        ncplane_putstr(n, "─");
    ncplane_putstr(n, "┘");

    if (title) {
        ncplane_set_channels(n, cht);
        ncplane_set_styles(n, focused ? NCSTYLE_BOLD : NCSTYLE_NONE);
        char t[64];
        snprintf(t, sizeof(t), " %s ", title);
        ui_trunc(t, r->w - 4);
        ncplane_putstr_yx(n, r->y, r->x + 2, t);
        ncplane_set_styles(n, NCSTYLE_NONE);
    }
}

/* Clean single-line floating modal (solid pal->bg interior so background
 * text never bleeds through, no neon double-lines or muddy shadows). */
void ui_window(struct ncplane *n, const rect_t *r, const char *title)
{
    if (r->w < 8 || r->h < 4)
        return;

    s_in_modal = true;
    const palette_t *pal = ui_palette(g_prefs.theme);
    uint64_t chb = ui_ch(pal->accent, false);
    uint64_t cht = ui_ch(pal->accent, false);

    ncplane_set_channels(n, chb);
    for (int y = 1; y < r->h - 1; y++) {
        ncplane_cursor_move_yx(n, r->y + y, r->x + 1);
        for (int x = 1; x < r->w - 1; x++)
            ncplane_putchar(n, ' ');
    }
    ncplane_putstr_yx(n, r->y, r->x, "┌");
    for (int x = 1; x < r->w - 1; x++)
        ncplane_putstr(n, "─");
    ncplane_putstr(n, "┐");
    for (int y = 1; y < r->h - 1; y++) {
        ncplane_putstr_yx(n, r->y + y, r->x, "│");
        ncplane_putstr_yx(n, r->y + y, r->x + r->w - 1, "│");
    }
    ncplane_putstr_yx(n, r->y + r->h - 1, r->x, "└");
    for (int x = 1; x < r->w - 1; x++)
        ncplane_putstr(n, "─");
    ncplane_putstr(n, "┘");

    if (title) {
        char t[64];
        snprintf(t, sizeof(t), " %s ", title);
        ui_trunc(t, r->w - 4);
        ncplane_set_channels(n, cht);
        ncplane_set_styles(n, NCSTYLE_BOLD);
        ncplane_putstr_yx(n, r->y, r->x + 2, t);
        ncplane_set_styles(n, NCSTYLE_NONE);
    }
}

void ui_btn(struct ncplane *n, int x, int y, const char *label,
            bool active, bool focused, int id)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    uint64_t ch = 0;
    uint32_t inv_fg = pal->light ? 0xFFFFFF : 0x101418;
    if (focused && active) {
        ncchannels_set_fg_rgb(&ch, inv_fg);
        ncchannels_set_bg_rgb(&ch, pal->accent);
        ncplane_set_styles(n, NCSTYLE_BOLD | NCSTYLE_UNDERLINE);
    } else if (active) {
        ncchannels_set_fg_rgb(&ch, inv_fg);
        ncchannels_set_bg_rgb(&ch, pal->accent);
        ncplane_set_styles(n, NCSTYLE_BOLD);
    } else if (focused) {
        ch = ui_ch(pal->accent, true);
        ncplane_set_styles(n, NCSTYLE_BOLD | NCSTYLE_UNDERLINE);
    } else {
        ch = ui_ch(pal->muted, true);
        ncplane_set_styles(n, NCSTYLE_NONE);
    }
    ncplane_set_channels(n, ch);
    ncplane_putstr_yx(n, y, x, label);
    ncplane_set_styles(n, NCSTYLE_NONE);
    tgt_register(x, y, ut_cells(label), 1, id);
}

int ui_btn_row(struct ncplane *n, int y, int x, int w,
               const ui_btndef_t *btns, int count)
{
    int cx = x;
    for (int i = 0; i < count; i++) {
        int len = ut_cells(btns[i].label);
        if (cx + len > x + w)
            break;
        ui_btn(n, cx, y, btns[i].label, btns[i].active, false, btns[i].id);
        cx += len + 1;
    }
    return cx;
}

void ui_row(struct ncplane *n, int x, int y, int w,
            const char *label, const char *value, bool selected)
{
    if (w <= 2)
        return;
    const palette_t *pal = ui_palette(g_prefs.theme);
    uint64_t ch = ui_ch(selected ? pal->accent : pal->text, selected);
    ncplane_set_channels(n, ch);
    ncplane_set_styles(n, selected ? NCSTYLE_BOLD : NCSTYLE_NONE);

    /* paint full-width selection bar so rows look clean and cohesive */
    if (selected) {
        ncplane_cursor_move_yx(n, y, x);
        for (int i = 0; i < w; i++)
            ncplane_putchar(n, ' ');
    }

    char lb[128], vb[128];
    snprintf(lb, sizeof(lb), "%s%s ", selected ? "▸ " : "  ", label);
    snprintf(vb, sizeof(vb), "%s", value ? value : "");
    int lw = w * 42 / 100;
    if (lw < 18)
        lw = 18;
    if (lw > 28)
        lw = 28;
    if (lw > w - 4)
        lw = w / 2;
    ui_trunc(lb, lw);

    char line[256];
    ut_cell_join(line, sizeof(line), lb, lw, vb);
    ui_trunc(line, w);
    ncplane_putstr_yx(n, y, x, line);
    ncplane_set_styles(n, NCSTYLE_NONE);
}

void ui_keybar(struct ncplane *n, int x, int y, int w,
               const ui_hint_t *hints, int count)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int cx = x;
    int end = x + w;
    for (int i = 0; i < count; i++) {
        int kw = ut_cells(hints[i].key);
        int lw = ut_cells(hints[i].label);
        int need = kw + 1 + lw + (i > 0 ? 2 : 0);
        if (cx + need > end)
            break;
        if (i > 0)
            cx += 2;
        uint32_t kfg = hints[i].hi ? pal->warn : pal->text;
        ncplane_set_channels(n, ui_ch(kfg, false));
        ncplane_set_styles(n, NCSTYLE_BOLD);
        ncplane_putstr_yx(n, y, cx, hints[i].key);
        cx += kw + 1;
        ncplane_set_channels(n, ui_ch(pal->muted, false));
        ncplane_set_styles(n, NCSTYLE_NONE);
        ncplane_putstr_yx(n, y, cx, hints[i].label);
        cx += lw;
    }
}

/* ---- mouse targets ------------------------------------------------------ */

#define MAX_TARGETS 256
static struct { int x1, y1, x2, y2, id; } s_tg[MAX_TARGETS];
static int s_tg_n;

void tgt_clear(void) { s_tg_n = 0; }

void tgt_register(int x, int y, int w, int h, int encoded)
{
    if (s_tg_n >= MAX_TARGETS || w <= 0 || h <= 0)
        return;
    s_tg[s_tg_n].x1 = x;
    s_tg[s_tg_n].y1 = y;
    s_tg[s_tg_n].x2 = x + w - 1;
    s_tg[s_tg_n].y2 = y + h - 1;
    s_tg[s_tg_n].id = encoded;
    s_tg_n++;
}

int tgt_find(int x, int y)
{
    for (int i = s_tg_n - 1; i >= 0; i--)
        if (x >= s_tg[i].x1 && x <= s_tg[i].x2 &&
            y >= s_tg[i].y1 && y <= s_tg[i].y2)
            return s_tg[i].id;
    return 0;
}

/* ---- text input (notcurses key bridge over src/tinput.c) ---------------- */

static uint32_t nc_to_tin_op(uint32_t key)
{
    switch (key) {
    case NCKEY_LEFT:      return TIN_KEY_LEFT;
    case NCKEY_RIGHT:     return TIN_KEY_RIGHT;
    case NCKEY_HOME:      return TIN_KEY_HOME;
    case NCKEY_END:       return TIN_KEY_END;
    case NCKEY_BACKSPACE: return TIN_KEY_BACKSPACE;
    case NCKEY_DEL:       return TIN_KEY_DEL;
    default:              return key;
    }
}

bool tin_is_edit_key(uint32_t key)
{
    return tin_is_edit_op(nc_to_tin_op(key));
}

bool tin_key(tinput_t *t, uint32_t key)
{
    return tin_op(t, nc_to_tin_op(key));
}

/* ---- view switching ----------------------------------------------------- */

void ui_set_view(ui_view_t v)
{
    if (v >= VIEW_COUNT)
        return;
    if (g_ui.view == v)
        return;
    g_ui.pw_typing = false;
    editor_corefreq_close();
    g_ui.sys_bat_typing = false;
    g_ui.lt_hex_active = 0;
    g_ui.fe_input = 0;
    g_ui.prof_input = 0;
    g_ui.prof_del_arm = -1;
    g_ui.prof_upd_arm = -1;
    g_ui.prof_in_edit = false;
    g_ui.prof_val_typing = false;
    g_ui.prof_edit_dirty = false;
    g_ui.fc_edit_dirty = false;
    g_ui.pw_msg_ms = 0;
    if (v == VIEW_TELEM)
        g_ui.prev_view = g_ui.view;
    g_ui.view = v;

    /* entering POWER, FANS or SYSTEM: pick up external changes unless dirty */
    if (v == VIEW_POWER && !g_ui.pw_dirty) {
        hw_refresh_live(g_ui.hw);
        pw_sync_from_hw();
    } else if (v == VIEW_FANS) {
        hw_refresh_live(g_ui.hw);
        panel_profiles_reload();
        editor_fan_sync_bufs();
    } else if (v == VIEW_SYSTEM && !g_ui.sys_dirty) {
        hw_refresh_live(g_ui.hw);
        sys_sync_from_hw();
    } else if (v == VIEW_SNAPSHOTS) {
        hw_refresh_live(g_ui.hw);
        panel_profiles_reload();
    }
}

/* ---- event log & help overlays ----------------------------------------- */

static bool log_is_err(const char *s)
{
    return strstr(s, "FAILED") || strstr(s, "failed") ||
           strstr(s, "error") || strstr(s, "denied");
}

static void draw_log_overlay(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int x = r->x + 2, w = r->w - 4;
    if (w < 12 || r->h < 6)
        return;

    ui_window(n, r, "EVENT LOG");

    int count = ut_log_count();
    int y0 = r->y + 3;
    int vis = (r->y + r->h - 1) - y0;
    if (vis < 1)
        vis = 1;
    int max_scr = count > vis ? count - vis : 0;
    if (g_ui.log_scroll > max_scr)
        g_ui.log_scroll = max_scr;
    if (g_ui.log_scroll < 0)
        g_ui.log_scroll = 0;

    char hdr[128];
    snprintf(hdr, sizeof(hdr),
             "j/k scroll · g/G top/end · Esc/L close · newest first (%d/%d)%s%s",
             count, UT_LOG_ENTRIES,
             g_ui.log_scroll > 0 ? " ▲" : "",
             g_ui.log_scroll < max_scr ? " ▼" : "");
    ui_putln(n, x, r->y + 1, w, hdr, pal->muted, false);

    if (count == 0) {
        ui_putln(n, x, y0, w, "(no events logged yet)", pal->muted, false);
        ui_modal_end();
        return;
    }

    for (int i = 0; i < vis && g_ui.log_scroll + i < count; i++) {
        int idx = g_ui.log_scroll + i;
        const char *msg = ut_log_get(idx);
        bool err = log_is_err(msg);
        uint32_t fg = err ? pal->err : (idx == 0 ? pal->accent : pal->text);
        ui_putln(n, x, y0 + i, w, msg, fg, err || idx == 0);
    }
    ui_modal_end();
}

static void draw_help_overlay(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int x = r->x + 2, w = r->w - 4;
    if (w < 16 || r->h < 8)
        return;

    ui_window(n, r, "HELP · KEYMAP");
    static const char *const lines[] = {
        "GLOBAL",
        "  1..5 / Tab   switch view: Power · Fans · Display & Light · Snapshots · Telemetry",
        "  t            jump to Telemetry (again returns to previous view)",
        "  Esc          back / cancel / close overlay (never opens a menu)",
        "  ,            Preferences & CLI shortcuts (modes.ini, startup tab, theme)",
        "  L            Event log overlay (last 24 entries; or click bottom log)",
        "  ?            this keymap help · q quit (asks twice if edits staged)",
        "",
        "POWER (1) & DISPLAY/LIGHT (3)",
        "  j / k        select row · h / l step value (staged, nothing writes yet)",
        "  e            type exact value (watts, MHz, battery %, hex color)",
        "  w / Enter    apply all staged edits · r revert to live values",
        "  c / b        focus inline Core Limits (Power) · daeboard (Display & Light)",
        "",
        "FANS (2)",
        "  c / g        CPU / GPU curve · < > select point · h/l/j/k nudge (J/K ±5%)",
        "  e / p        exact Temp / PWM (or %) · + / - add/remove · i guides · m presets",
        "  o / w / r    toggle custom on/off · write to EC · revert · s save curve",
        "",
        "SNAPSHOTS & FAN CURVES (4)",
        "  f            switch left list between Snapshots (top) & Fan Curves (bottom)",
        "  Enter        apply · e / l edit · s / n save · u u update · r rename · d d del",
        "  In editor:   h/l step (selects saved fan curves!) · e exact · x skip · w save",
        "  c (ceiling)  per-core grid for the snapshot · o (fan curve) open in 2 Fans",
        "",
        "TELEMETRY (5)",
        "  h j k l      walk 8-section grid · Enter detail (1-8 jump inside detail)",
        "  r / s        reset stats · toggle 60 s window vs since-reset",
    };
    int y = r->y + 1;
    int ymax = r->y + r->h - 1;
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]) && y < ymax; i++, y++) {
        bool head = lines[i][0] && lines[i][1] != ' ';
        ui_putln(n, x, y, w, lines[i], head ? pal->accent : pal->text, head);
    }
    ui_modal_end();
}

/* ---- Konsept C layout & chrome ----------------------------------------- */

static void ui_layout(unsigned dimy, unsigned dimx)
{
    int W = (int)dimx, H = (int)dimy;
    int top = 2; /* row 0 header + row 1 hairline */
    int bot = 3; /* H-3 hairline + H-2 status/log + H-1 keybar */
    int body_h = H - top - bot;
    if (body_h < 4) {
        top = 1;
        bot = 1;
        body_h = H - top - bot;
        if (body_h < 1)
            body_h = 1;
    }

    if (W >= 96) {
        int rw = 22;
        g_ui.rc_rail = (rect_t){ 0, top, rw, body_h };
        g_ui.rc_ws   = (rect_t){ rw + 2, top, W - (rw + 2) - 1, body_h };
    } else {
        g_ui.rc_rail = (rect_t){ 0, top, 0, 0 };
        g_ui.rc_ws   = (rect_t){ 1, top, W > 2 ? W - 2 : W, body_h };
    }

    if (g_ui.db_overlay) {
        int ow = W - 4, oh = H - 3;
        if (ow < 20) ow = W;
        if (oh < 8)  oh = H - 1;
        g_ui.rc_overlay = (rect_t){ (W - ow) / 2, 1, ow, oh };
    } else if (g_ui.fe_presets_overlay) {
        int ow = 72, oh = 20;
        if (ow > W - 4) ow = W - 4;
        if (ow < 20)    ow = W;
        if (oh > H - 4) oh = H - 4;
        if (oh < 8)     oh = H - 1;
        g_ui.rc_overlay = (rect_t){ (W - ow) / 2, (H - oh) / 2, ow, oh };
    } else {
        int ow = W - 8;
        if (ow > 94) ow = 94;
        if (ow < 20) ow = W;
        int oh = H - 6;
        if (oh > 30) oh = 30;
        if (oh < 8)  oh = H - 2;
        if (oh < 4)  oh = H;
        g_ui.rc_overlay = (rect_t){ (W - ow) / 2, (H - oh) / 2, ow, oh };
    }
}

static void fmt_opt_i(char *out, size_t n, const char *fmt, int v)
{
    if (v < 0)
        snprintf(out, n, "--");
    else
        snprintf(out, n, fmt, v);
}

static void draw_header(struct ncplane *n, unsigned dimy, unsigned dimx)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    const hw_state_t *hw = g_ui.hw;
    int W = (int)dimx;

    if (g_ui.rc_rail.w > 0) {
        /* wide Konsept C header: brand + model left, live summary right */
        char right[128], ct[16], gt[16], rc[16], bp[16], hz[16];
        fmt_opt_i(ct, sizeof(ct), "%d°", hw->cpu_temp);
        fmt_opt_i(gt, sizeof(gt), "%d°", hw->gpu_temp);
        fmt_opt_i(rc, sizeof(rc), "%d", hw->rpm_cpu);
        fmt_opt_i(bp, sizeof(bp), "%d%%", hw->bat_pct);
        if (hw->hz_cur > 0)
            snprintf(hz, sizeof(hz), "%d Hz", hw->hz_cur);
        else
            snprintf(hz, sizeof(hz), "-- Hz");
        snprintf(right, sizeof(right), "%s · %s · %s/%s · %s rpm · %s%s",
                 hw_profile_name(hw->profile), hz, ct, gt, rc, bp,
                 hw->ac_online ? " AC" : "");

        int rw = ut_cells(right);
        int left_max = W - rw - 4;
        if (left_max < 10)
            left_max = W - 2;

        ncplane_set_channels(n, ui_ch(pal->accent, false));
        ncplane_set_styles(n, NCSTYLE_BOLD);
        ncplane_putstr_yx(n, 0, 1, "ctron");
        ncplane_set_styles(n, NCSTYLE_NONE);

        if (left_max > 8) {
            char mdl[96];
            snprintf(mdl, sizeof(mdl), " · %s", hw->model);
            ui_putln(n, 6, 0, left_max - 6, mdl, pal->muted, false);
        }
        if (W - rw - 1 > 8)
            ui_putln(n, W - rw - 1, 0, rw + 1, right, pal->muted, false);
    } else {
        /* narrow fallback: inline clickable tabs across row 0 */
        static const char *const SHORT_N[VIEW_COUNT] = {
            "1 Power", "2 Fans", "3 System", "4 Snaps", "5 Telem"
        };
        int cx = 1;
        for (int i = 0; i < VIEW_COUNT; i++) {
            bool act = (int)g_ui.view == i;
            int len = ut_cells(SHORT_N[i]);
            if (cx + len >= W)
                break;
            ncplane_set_channels(n, ui_ch(act ? pal->accent : pal->muted, act));
            ncplane_set_styles(n, act ? NCSTYLE_BOLD : NCSTYLE_NONE);
            ncplane_putstr_yx(n, 0, cx, SHORT_N[i]);
            ncplane_set_styles(n, NCSTYLE_NONE);
            tgt_register(cx, 0, len, 1, TGT(TGT_PANEL_RAIL, i));
            cx += len + 2;
        }
    }

    if (dimy >= 6) {
        ui_hline(n, 0, 1, W, pal->border);
        if (g_ui.rc_rail.w > 0) {
            ncplane_set_channels(n, ui_ch(pal->border, false));
            ncplane_putstr_yx(n, 1, g_ui.rc_rail.w, "┬");
        }
    }
}

static int total_staged_edits(void)
{
    bool snap_dirty = g_ui.prof_in_edit &&
                      (g_ui.prof_in_fcurves ? g_ui.fc_edit_dirty
                                            : g_ui.prof_edit_dirty);
    return pw_staged_count() + sys_staged_count() +
           (g_ui.hw->fan_staged ? 1 : 0) +
           (snap_dirty ? 1 : 0);
}

static void rail_kv(struct ncplane *n, int x, int y, int w,
                    const char *k, const char *v, uint32_t vfg)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    ui_putln(n, x, y, 6, k, pal->muted, false);
    int vw = ut_cells(v);
    int vx = x + w - vw;
    if (vx < x + 6)
        vx = x + 6;
    ui_putln(n, vx, y, x + w - vx, v, vfg, false);
}

static void draw_rail(struct ncplane *n, const rect_t *r)
{
    if (r->w <= 0 || r->h < 6)
        return;
    const palette_t *pal = ui_palette(g_prefs.theme);
    const hw_state_t *hw = g_ui.hw;
    int x = r->x + 1, w = r->w - 2;

    ui_putln(n, x, r->y, w, "VIEWS", pal->muted, true);

    bool dirty_v[VIEW_COUNT] = {
        pw_staged_count() > 0,
        hw->fan_staged,
        sys_staged_count() > 0,
        g_ui.prof_in_edit && (g_ui.prof_in_fcurves ? g_ui.fc_edit_dirty
                                                   : g_ui.prof_edit_dirty),
        false,
    };

    for (int i = 0; i < VIEW_COUNT && r->y + 1 + i < r->y + r->h; i++) {
        int y = r->y + 1 + i;
        bool act = (int)g_ui.view == i;
        ncplane_set_channels(n, ui_ch(act ? pal->accent : pal->text, act));
        ncplane_set_styles(n, act ? NCSTYLE_BOLD : NCSTYLE_NONE);
        if (act) {
            ncplane_cursor_move_yx(n, y, x);
            for (int c = 0; c < w; c++)
                ncplane_putchar(n, ' ');
        }
        char lab[32];
        snprintf(lab, sizeof(lab), "%s%s", act ? "▸ " : "  ", VIEW_NAMES[i]);
        ui_trunc(lab, w - 3);
        ncplane_putstr_yx(n, y, x, lab);
        ncplane_set_styles(n, NCSTYLE_NONE);

        if (dirty_v[i]) {
            ncplane_set_channels(n, ui_ch(pal->warn, act));
            ncplane_putstr_yx(n, y, x + w - 3, "●");
        }
        char num[4];
        snprintf(num, sizeof(num), "%d", i + 1);
        ncplane_set_channels(n, ui_ch(pal->muted, act));
        ncplane_putstr_yx(n, y, x + w - 1, num);
        tgt_register(x, y, w, 1, TGT(TGT_PANEL_RAIL, i));
    }

    int sep_y = r->y + 1 + VIEW_COUNT + 1;
    if (sep_y + 2 < r->y + r->h) {
        ui_hline(n, x, sep_y, w, pal->border);
        int y = sep_y + 1;
        ui_putln(n, x, y++, w, "LIVE", pal->muted, true);

        char v[48], ct[16], gt[16];
        if (y < r->y + r->h) {
            fmt_opt_i(ct, sizeof(ct), "%d°C", hw->cpu_temp);
            if (hw->cpu_mhz_cur > 0)
                snprintf(v, sizeof(v), "%s · %.1fG", ct, hw->cpu_mhz_cur / 1000.0);
            else
                snprintf(v, sizeof(v), "%s", ct);
            rail_kv(n, x, y++, w, "CPU", v, pal->text);
        }
        if (y < r->y + r->h) {
            fmt_opt_i(gt, sizeof(gt), "%d°C", hw->gpu_temp);
            if (hw->gpu_mw > 0)
                snprintf(v, sizeof(v), "%s · %dW", gt, hw->gpu_mw / 1000);
            else
                snprintf(v, sizeof(v), "%s", gt);
            rail_kv(n, x, y++, w, "GPU", v, pal->text);
        }
        if (y < r->y + r->h) {
            char r1[16], r2[16];
            fmt_opt_i(r1, sizeof(r1), "%d", hw->rpm_cpu);
            fmt_opt_i(r2, sizeof(r2), "%d", hw->rpm_gpu);
            snprintf(v, sizeof(v), "%s/%s", r1, r2);
            rail_kv(n, x, y++, w, "Fan", v, pal->text);
        }
        if (y < r->y + r->h) {
            char bp[16], pwr[24];
            fmt_opt_i(bp, sizeof(bp), "%d%%", hw->bat_pct);
            if (hw->ac_online) {
                snprintf(v, sizeof(v), "%s AC", bp);
            } else if (hw->bat_mw_known) {
                hw_fmt_power(pwr, sizeof(pwr), true, hw->bat_mw);
                snprintf(v, sizeof(v), "%s %s", bp, pwr);
            } else {
                snprintf(v, sizeof(v), "%s", bp);
            }
            rail_kv(n, x, y++, w, "Bat", v, hw->ac_online ? pal->ok : pal->text);
        }
        if (y < r->y + r->h) {
            if (hw->hz_cur > 0)
                snprintf(v, sizeof(v), "%d Hz", hw->hz_cur);
            else
                snprintf(v, sizeof(v), "--");
            rail_kv(n, x, y++, w, "Hz", v, pal->text);
        }
        if (y < r->y + r->h)
            rail_kv(n, x, y++, w, "Prof", hw_profile_name(hw->profile), pal->text);
        if (y < r->y + r->h) {
            int st = total_staged_edits();
            if (st > 0)
                snprintf(v, sizeof(v), "%d pending", st);
            else
                snprintf(v, sizeof(v), "synced");
            rail_kv(n, x, y++, w, "Edit", v, st > 0 ? pal->warn : pal->muted);
        }
    }

    /* vertical hairline divider between rail and workspace */
    ui_vline(n, r->x + r->w, r->y, r->h, pal->border);
}

static void draw_footer(struct ncplane *n, unsigned dimy, unsigned dimx)
{
    int H = (int)dimy, W = (int)dimx;
    if (H < 6 || W < 12)
        return;
    const palette_t *pal = ui_palette(g_prefs.theme);

    ui_hline(n, 0, H - 3, W, pal->border);
    if (g_ui.rc_rail.w > 0) {
        ncplane_set_channels(n, ui_ch(pal->border, false));
        ncplane_putstr_yx(n, H - 3, g_ui.rc_rail.w, "┴");
    }

    /* row H-2: live apply progress / toast / staged summary / desc + last log */
    int sy = H - 2;
    if (atomic_load(&g_ui.apply_busy)) {
        static const char spin[] = "|/-\\";
        static int frame;
        char ptxt[96], line[160];
        int pct;
        ut_progress_get(ptxt, sizeof(ptxt), &pct);
        long el = (ut_mono_ms() - g_ui.apply_start_ms) / 100;
        if (pct >= 0)
            snprintf(line, sizeof(line),
                     "%c %.20s: %.60s · %d%% · %ld.%ld s · reads paused · q waits",
                     spin[frame++ % 4], g_ui.apply_what,
                     ptxt[0] ? ptxt : "working", pct, el / 10, el % 10);
        else
            snprintf(line, sizeof(line),
                     "%c %.20s: %.80s · %ld.%ld s · reads paused · q waits",
                     spin[frame++ % 4], g_ui.apply_what,
                     ptxt[0] ? ptxt : "working", el / 10, el % 10);
        ui_putln(n, 1, sy, W - 2, line, pal->accent, true);
    } else {
        bool toast = g_ui.pw_msg_ms > 0 && ut_mono_ms() - g_ui.pw_msg_ms < 5000;
        const char *lg = ut_log_get(0);
        if (toast) {
            ui_putln(n, 1, sy, W - 2, g_ui.pw_msg,
                     g_ui.pw_msg_fail ? pal->err : pal->ok, true);
        } else {
            const char *left = NULL;
            uint32_t lfg = pal->muted;
            char stbuf[96];
            int st = total_staged_edits();
            if (st > 0 && (g_ui.view == VIEW_POWER || g_ui.view == VIEW_SYSTEM ||
                           g_ui.view == VIEW_FANS)) {
                snprintf(stbuf, sizeof(stbuf),
                         "● %d staged — w apply · r revert", st);
                left = stbuf;
                lfg = pal->warn;
            } else if (g_ui.view == VIEW_POWER) {
                left = pw_selected_desc();
            } else if (g_ui.view == VIEW_SYSTEM) {
                left = sys_selected_desc();
            }
            if (left && left[0]) {
                int lw = W > 80 ? W / 2 : W - 2;
                ui_putln(n, 1, sy, lw, left, lfg, lfg == pal->warn);
                if (W > 80 && lg[0]) {
                    int rw = ut_cells(lg);
                    int maxr = W - lw - 4;
                    if (rw > maxr) rw = maxr;
                    if (rw > 8)
                        ui_putln(n, W - rw - 1, sy, rw, lg, pal->muted, false);
                }
            } else if (lg[0]) {
                ui_putln(n, 1, sy, W - 2, lg, pal->muted, false);
            }
        }
        tgt_register(0, sy, W, 1, TGT(TGT_PANEL_LOG, 1));
    }

    /* row H-1: contextual keybar */
    int ky = H - 1;
    bool dirty_here = (g_ui.view == VIEW_POWER && pw_staged_count() > 0) ||
                      (g_ui.view == VIEW_SYSTEM && sys_staged_count() > 0) ||
                      (g_ui.view == VIEW_FANS && g_ui.hw->fan_staged);
    if (g_ui.view == VIEW_POWER && g_ui.pw_in_cores) {
        const ui_hint_t h[] = {
            { "Esc/c", "power", false },  { "j/k", "core", false },
            { "h/l", "±100", false },     { "e", "exact", false },
            { "a/A", "CCD·all", false },  { "o", "max", false },
            { "w", "apply", dirty_here }, { "r", "revert", false },
            { "L", "log", false },        { "q", "quit", false },
        };
        ui_keybar(n, 1, ky, W - 2, h, (int)(sizeof(h) / sizeof(h[0])));
    } else if (g_ui.view == VIEW_POWER) {
        const ui_hint_t h[] = {
            { "1-5/Tab", "view", false }, { "j/k", "row", false },
            { "h/l", "step", false },     { "e", "edit", false },
            { "w", "apply", dirty_here }, { "r", "revert", false },
            { "c", "cores", false },      { "L", "log", false },
            { ",", "prefs", false },      { "?", "help", false },
            { "q", "quit", false },
        };
        ui_keybar(n, 1, ky, W - 2, h, (int)(sizeof(h) / sizeof(h[0])));
    } else if (g_ui.view == VIEW_FANS) {
        const ui_hint_t h[] = {
            { "1-5/Tab", "view", false }, { "c/g", "cpu·gpu", false },
            { "</>", "pt", false },       { "hjkl", "nudge", false },
            { "e/p", "T/P", false },      { "m", "presets", false },
            { "s", "save", false },       { "r", "revert", false },
            { "o", "on/off", false },     { "w", "write", dirty_here },
            { "L", "log", false },        { "q", "quit", false },
        };
        ui_keybar(n, 1, ky, W - 2, h, (int)(sizeof(h) / sizeof(h[0])));
    } else if (g_ui.view == VIEW_SYSTEM) {
        const ui_hint_t h[] = {
            { "1-5/Tab", "view", false }, { "j/k", "row", false },
            { "h/l", "step", false },     { "e", "edit", false },
            { "w/Enter", "apply", dirty_here }, { "r", "revert", false },
            { "b", "daeboard", false },   { "L", "log", false },
            { ",", "prefs", false },      { "q", "quit", false },
        };
        ui_keybar(n, 1, ky, W - 2, h, (int)(sizeof(h) / sizeof(h[0])));
    } else if (g_ui.view == VIEW_SNAPSHOTS && g_ui.prof_in_edit && g_ui.prof_in_cores) {
        const ui_hint_t h[] = {
            { "Esc/c", "fields", false }, { "j/k", "core", false },
            { "h/l", "±100", false },     { "e", "exact", false },
            { "x", "clear", false },      { "a/A", "CCD·all", false },
            { "r", "undo", false },       { "Enter", "keep", false },
            { "L", "log", false },        { "q", "quit", false },
        };
        ui_keybar(n, 1, ky, W - 2, h, (int)(sizeof(h) / sizeof(h[0])));
    } else if (g_ui.view == VIEW_SNAPSHOTS && g_ui.prof_in_edit && g_ui.prof_in_fcurves) {
        const ui_hint_t h[] = {
            { "Esc", "list", false },     { "j/k", "row", false },
            { "h/l", "PWM", false },      { "</>", "temp", false },
            { "e", "exact", false },      { "x", "skip", false },
            { "f/m", "live·ref", false }, { "o", "graph", false },
            { "w/Enter", "save", g_ui.fc_edit_dirty },
            { "q", "quit", false },
        };
        ui_keybar(n, 1, ky, W - 2, h, (int)(sizeof(h) / sizeof(h[0])));
    } else if (g_ui.view == VIEW_SNAPSHOTS && g_ui.prof_in_edit) {
        const ui_hint_t h[] = {
            { "Esc", "list", false },
            { "j/k", "field", false },
            { "h/l", "change", false },
            { "e", "exact", false },
            { "x", "skip", false },
            { "f", "live", false },
            { "c", "cores", false },
            { "w/Enter", "save", g_ui.prof_edit_dirty },
            { "L", "log", false },
            { "q", "quit", false },
        };
        ui_keybar(n, 1, ky, W - 2, h, (int)(sizeof(h) / sizeof(h[0])));
    } else if (g_ui.view == VIEW_SNAPSHOTS) {
        const ui_hint_t h[] = {
            { "1-5/Tab", "view", false },
            { "f", g_ui.prof_in_fcurves ? "snaps" : "fans", false },
            { "j/k", "row", false },      { "Enter", "apply", false },
            { "e", "edit", false },       { "s/n", "save", false },
            { "uu", "update", false },    { "r", "rename", false },
            { "c", "note", false },       { "dd", "del", false },
            { "q", "quit", false },
        };
        ui_keybar(n, 1, ky, W - 2, h, (int)(sizeof(h) / sizeof(h[0])));
    } else if (g_ui.telem_detail >= 0) {
        const ui_hint_t h[] = {
            { "Esc", "grid", false },     { "h/l", "section", false },
            { "1-8", "jump", false },     { "r", "reset", false },
            { "s", "win/since", false },  { "L", "log", false },
            { ",", "prefs", false },      { "q", "quit", false },
        };
        ui_keybar(n, 1, ky, W - 2, h, (int)(sizeof(h) / sizeof(h[0])));
    } else {
        const ui_hint_t h[] = {
            { "1-5/Tab", "view", false }, { "hjkl", "grid", false },
            { "Enter", "detail", false }, { "r", "reset", false },
            { "s", "win/since", false },  { "L", "log", false },
            { ",", "prefs", false },      { "q", "quit", false },
        };
        ui_keybar(n, 1, ky, W - 2, h, (int)(sizeof(h) / sizeof(h[0])));
    }
}

/* ---- mouse --------------------------------------------------------------- */

static void handle_mouse(struct ncplane *stdn, const struct ncinput *ni, uint32_t key)
{
    if (atomic_load(&g_ui.apply_busy))
        return;
    int mx = ni->x, my = ni->y;
    unsigned dimy = 0, dimx = 0, cdy = 0, cdx = 0;
    ncplane_dim_yx(stdn, &dimy, &dimx);
    ncplane_pixel_geom(stdn, NULL, NULL, &cdy, &cdx, NULL, NULL);
    if (cdx > 1 && mx >= (int)dimx * 2) mx /= (int)cdx;
    if (cdy > 1 && my >= (int)dimy * 2) my /= (int)cdy;
    if (mx < 0 || my < 0)
        return;
    if (dimx > 0 && mx >= (int)dimx) mx = (int)dimx - 1;
    if (dimy > 0 && my >= (int)dimy) my = (int)dimy - 1;

    if (key != NCKEY_BUTTON1)
        return;

    const rect_t *w = &g_ui.rc_overlay;
    bool outside = (mx < w->x || mx >= w->x + w->w || my < w->y || my >= w->y + w->h);

    if (g_ui.help_overlay) {
        if (outside) g_ui.help_overlay = false;
        return;
    }
    if (g_ui.log_overlay) {
        if (outside) g_ui.log_overlay = false;
        return;
    }
    if (g_ui.db_overlay) {
        if (outside) { editor_daeboard_close(); return; }
        int t = tgt_find(mx, my);
        if (((t >> 24) & 0x7f) == TGT_PANEL_DAEBOARD)
            editor_daeboard_act(t & TGT_ID_MASK);
        return;
    }
    if (g_ui.fe_presets_overlay) {
        if (outside) { g_ui.fe_presets_overlay = false; return; }
        int t = tgt_find(mx, my);
        if (((t >> 24) & 0x7f) == TGT_PANEL_FAN_PRESETS)
            editor_fan_presets_act(t & TGT_ID_MASK);
        return;
    }
    if (g_ui.settings_overlay) {
        if (outside) { g_ui.settings_overlay = false; g_ui.set_del_arm = -1; return; }
        int t = tgt_find(mx, my);
        if (((t >> 24) & 0x7f) == TGT_PANEL_SETTINGS)
            panel_settings_act(t & TGT_ID_MASK);
        return;
    }

    if (g_ui.view == VIEW_FANS) {
        const rect_t *g = editor_fan_graph();
        if (g->w > 0 && mx >= g->x && mx < g->x + g->w && my >= g->y && my < g->y + g->h) {
            editor_fan_act(0, mx - g->x, my - g->y);
            return;
        }
    }

    int t = tgt_find(mx, my);
    if (!t)
        return;
    int panel = (t >> 24) & 0x7f;
    int id = t & TGT_ID_MASK;
    if (panel != TGT_PANEL_PROFILES) {
        g_ui.prof_del_arm = -1;
        g_ui.prof_upd_arm = -1;
    }

    switch (panel) {
    case TGT_PANEL_RAIL:
        ui_set_view((ui_view_t)ut_clamp_i(id, 0, VIEW_COUNT - 1));
        break;
    case TGT_PANEL_PROFILES:
        panel_profiles_act(id);
        break;
    case TGT_PANEL_WORKSPACE:
        panel_workspace_act(id);
        break;
    case TGT_PANEL_COREFREQ:
        if (g_ui.view == VIEW_POWER) {
            g_ui.pw_in_cores = true;
            editor_corefreq_act(id);
        } else if (g_ui.view == VIEW_SNAPSHOTS && g_ui.prof_in_cores) {
            editor_corefreq_act(id);
        }
        break;
    case TGT_PANEL_TELEM:
        panel_telem_full_act(id);
        break;
    case TGT_PANEL_SETTINGS:
        panel_settings_act(id);
        break;
    case TGT_PANEL_LOG:
        g_ui.log_overlay = true;
        g_ui.log_scroll = 0;
        break;
    default:
        break;
    }
}

/* ---- background apply worker ------------------------------------------- */

#define KEY_REFRESH_MIN_MS 120
static long s_last_refresh_ms;

static void maybe_refresh(hw_state_t *hw, bool force)
{
    if (atomic_load(&g_ui.apply_busy))
        return;
    long now = ut_mono_ms();
    if (!force && s_last_refresh_ms > 0 &&
        now - s_last_refresh_ms < KEY_REFRESH_MIN_MS)
        return;
    hw_refresh_fast(hw);
    telem_poll(hw, now);
    s_last_refresh_ms = now;
}

static hw_state_t s_hw_frozen;

static void *apply_worker(void *arg)
{
    (void)arg;
    hw_state_t *hw = g_ui.apply_hw;

    switch (g_ui.apply_kind) {
    case APPLY_SNAPSHOT: {
        hw->fan_staged = false;
        char err[192] = "";
        g_ui.apply_ok = profile_import(g_ui.apply_arg, hw, err, sizeof(err)) == 0;
        snprintf(g_ui.apply_sum, sizeof(g_ui.apply_sum), "%s", err);
        if (hw->fan_staged) {
            ut_progress("snapshot: fan curves");
            if (ctrl_fan_write(hw) != 0) {
                g_ui.apply_ok = false;
                if (!g_ui.apply_sum[0])
                    snprintf(g_ui.apply_sum, sizeof(g_ui.apply_sum),
                             "fan curve write failed (see log)");
            }
        }
        hw_refresh_live(hw);
        break;
    }
    case APPLY_FAN_CURVE: {
        char err[192] = "";
        g_ui.apply_ok = fcurve_apply(g_ui.apply_arg, hw, err, sizeof(err)) == 0;
        snprintf(g_ui.apply_sum, sizeof(g_ui.apply_sum), "%s", err);
        hw_refresh_live(hw);
        break;
    }
    case APPLY_POWER: {
        int fails = 0;
        g_ui.apply_sum[0] = '\0';
        g_ui.apply_ok = true;
        pw_apply_writes(hw, &fails, g_ui.apply_sum, sizeof(g_ui.apply_sum));
        g_ui.apply_fails = fails;
        break;
    }
    case APPLY_SYSTEM: {
        int fails = 0;
        g_ui.apply_sum[0] = '\0';
        g_ui.apply_ok = true;
        sys_apply_writes(hw, &fails, g_ui.apply_sum, sizeof(g_ui.apply_sum));
        g_ui.apply_fails = fails;
        break;
    }
    case APPLY_MODE: {
        int idx = mode_find(g_ui.modes, g_ui.mode_n, g_ui.apply_arg);
        if (idx < 0) {
            g_ui.apply_ok = false;
            snprintf(g_ui.apply_sum, sizeof(g_ui.apply_sum), "no such mode");
            break;
        }
        char err[128] = "";
        g_ui.apply_ok = mode_apply(hw, g_ui.modes[idx].steps, err, sizeof(err)) == 0;
        snprintf(g_ui.apply_sum, sizeof(g_ui.apply_sum), "%s", err);
        hw_refresh_live(hw);
        break;
    }
    default:
        break;
    }

    atomic_store(&g_ui.apply_busy, false);
    return NULL;
}

void ui_spawn_apply(int kind, const char *what, const char *arg)
{
    if (atomic_load(&g_ui.apply_busy)) {
        if (!g_ui.apply_warned) {
            ut_log("apply already running — keys locked until it finishes");
            g_ui.apply_warned = true;
        }
        return;
    }
    g_ui.apply_kind = kind;
    g_ui.apply_fails = 0;
    g_ui.apply_warned = false;
    snprintf(g_ui.apply_what, sizeof(g_ui.apply_what), "%s", what ? what : "");
    snprintf(g_ui.apply_arg, sizeof(g_ui.apply_arg), "%s", arg ? arg : "");
    g_ui.apply_sum[0] = '\0';
    g_ui.apply_start_ms = ut_mono_ms();
    ut_progress_clear();
    ut_progress("%s: starting", g_ui.apply_what);

    g_ui.apply_hw = g_ui.hw;
    s_hw_frozen = *g_ui.hw;
    g_ui.hw = &s_hw_frozen;

    atomic_store(&g_ui.apply_busy, true);
    if (pthread_create(&g_ui.apply_tid, NULL, apply_worker, NULL) == 0) {
        g_ui.apply_spawned = true;
    } else {
        g_ui.hw = g_ui.apply_hw;
        atomic_store(&g_ui.apply_busy, false);
        ut_log("apply: could not start the worker thread");
    }
}

static void finalize_apply(void)
{
    g_ui.hw = g_ui.apply_hw;
    long took = (ut_mono_ms() - g_ui.apply_start_ms) / 100;
    switch (g_ui.apply_kind) {
    case APPLY_SNAPSHOT:
        if (g_ui.apply_ok)
            ut_log("applied snapshot '%s' · %ld.%ld s",
                   g_ui.apply_arg, took / 10, took % 10);
        else
            ut_log("apply failed: %.120s · %ld.%ld s",
                   g_ui.apply_sum[0] ? g_ui.apply_sum : "?",
                   took / 10, took % 10);
        pw_sync_from_hw();
        sys_sync_from_hw();
        cf_reset_staging();
        panel_profiles_reload();
        settings_save(g_ui.hw);
        break;
    case APPLY_FAN_CURVE:
        if (g_ui.apply_ok) {
            ut_log("applied fan curve '%s' · %ld.%ld s",
                   g_ui.apply_arg, took / 10, took % 10);
            snprintf(g_ui.pw_msg, sizeof(g_ui.pw_msg),
                     "✓ fan curve '%s' applied", g_ui.apply_arg);
            g_ui.pw_msg_fail = false;
        } else {
            ut_log("fan curve '%s': %.120s · %ld.%ld s",
                   g_ui.apply_arg,
                   g_ui.apply_sum[0] ? g_ui.apply_sum : "failed",
                   took / 10, took % 10);
            snprintf(g_ui.pw_msg, sizeof(g_ui.pw_msg),
                     "⚠ fan curve '%.36s': %.80s", g_ui.apply_arg,
                     g_ui.apply_sum[0] ? g_ui.apply_sum : "failed");
            g_ui.pw_msg_fail = true;
        }
        g_ui.pw_msg_ms = ut_mono_ms();
        panel_profiles_reload();
        editor_fan_sync_bufs();
        break;
    case APPLY_POWER:
    case APPLY_SYSTEM: {
        const char *tag = g_ui.apply_kind == APPLY_POWER ? "power" : "system";
        pw_sync_from_hw();
        sys_sync_from_hw();
        if (g_ui.apply_kind == APPLY_POWER) {
            cf_reset_staging();
            settings_save(g_ui.hw);
        }
        ut_log("%s apply: %s · %ld.%ld s", tag,
               g_ui.apply_fails ? "some fields FAILED (privilege?)" : "ok",
               took / 10, took % 10);
        if (g_ui.apply_sum[0]) {
            if (g_ui.apply_fails)
                snprintf(g_ui.pw_msg, sizeof(g_ui.pw_msg),
                         "⚠ %.140s · %d failed", g_ui.apply_sum,
                         g_ui.apply_fails);
            else
                snprintf(g_ui.pw_msg, sizeof(g_ui.pw_msg), "✓ %.150s",
                         g_ui.apply_sum);
            g_ui.pw_msg_fail = g_ui.apply_fails > 0;
            g_ui.pw_msg_ms = ut_mono_ms();
        }
        break;
    }
    case APPLY_MODE: {
        int idx = mode_find(g_ui.modes, g_ui.mode_n, g_ui.apply_arg);
        if (g_ui.apply_ok && idx >= 0) {
            ut_log("mode '%s' applied · %ld.%ld s",
                   g_ui.apply_arg, took / 10, took % 10);
            ctl_capture_mode(g_ui.apply_arg, g_ui.modes[idx].steps);
        } else if (!g_ui.apply_ok) {
            ut_log("mode '%s': %.100s · %ld.%ld s", g_ui.apply_arg,
                   g_ui.apply_sum[0] ? g_ui.apply_sum : "failed",
                   took / 10, took % 10);
        }
        pw_sync_from_hw();
        sys_sync_from_hw();
        cf_reset_staging();
        panel_profiles_reload();
        settings_save(g_ui.hw);
        break;
    }
    default:
        break;
    }
    ut_progress_clear();
    g_ui.apply_warned = false;
    s_last_refresh_ms = 0;
}

/* ---- key dispatch (unified conflict-free keymap) ------------------------ */

static void try_quit(void)
{
    if (atomic_load(&g_ui.apply_busy)) {
        if (!g_ui.quit_pending) {
            atomic_store(&g_ui.quit_pending, true);
            ut_log("apply running — quitting when it finishes");
            return;
        }
        ut_log("still applying — waiting for it to finish");
        return;
    }
    if ((g_ui.pw_dirty || g_ui.sys_dirty) && !g_ui.pw_quit_warned) {
        g_ui.pw_quit_warned = true;
        ut_log("staged edits pending — press q again to quit without applying");
        return;
    }
    g_ui.running = false;
}

static void dispatch_key(uint32_t key, const struct ncinput *ni)
{
    if (atomic_load(&g_ui.apply_busy) && key != 'q' && key != 'Q') {
        if (!g_ui.apply_warned) {
            ut_log("apply running — keys paused, q to queue quit");
            g_ui.apply_warned = true;
        }
        return;
    }

    /* normalize Ctrl+letter readline chords once for all tinput_t widgets */
    if (ncinput_ctrl_p(ni) && !ncinput_alt_p(ni)) {
        switch (key) {
        case 'a': case 'A': key = 0x01; break;
        case 'e': case 'E': key = 0x05; break;
        case 'k': case 'K': key = 0x0b; break;
        case 'u': case 'U': key = 0x15; break;
        case 'w': case 'W': key = 0x17; break;
        default: break;
        }
    }

    /* 1. Floating overlays swallow keys first; Esc always closes */
    if (g_ui.help_overlay) {
        if (key == NCKEY_ESC || key == '?' || key == 'h')
            g_ui.help_overlay = false;
        else if (key == 'q' || key == 'Q')
            try_quit();
        return;
    }
    if (g_ui.log_overlay) {
        int n = ut_log_count();
        switch (key) {
        case NCKEY_ESC:
        case 'l':
        case 'L':
            g_ui.log_overlay = false;
            break;
        case 'q':
        case 'Q':
            try_quit();
            break;
        case 'j':
        case NCKEY_DOWN:
            if (g_ui.log_scroll + 1 < n)
                g_ui.log_scroll++;
            break;
        case 'k':
        case NCKEY_UP:
            if (g_ui.log_scroll > 0)
                g_ui.log_scroll--;
            break;
        case 'g':
        case NCKEY_HOME:
            g_ui.log_scroll = 0;
            break;
        case 'G':
        case NCKEY_END:
            g_ui.log_scroll = n > 0 ? n - 1 : 0;
            break;
        default:
            break;
        }
        return;
    }
    if (g_ui.db_overlay) {
        editor_daeboard_key(key);
        return;
    }
    if (g_ui.fe_presets_overlay) {
        editor_fan_presets_key(key);
        return;
    }
    if (g_ui.settings_overlay) {
        if (g_ui.md_field >= 0) {
            panel_settings_key(key);
            return;
        }
        if (key == NCKEY_ESC || key == ',') {
            g_ui.settings_overlay = false;
            g_ui.set_del_arm = -1;
            return;
        }
        if (key == 'q' || key == 'Q') {
            try_quit();
            return;
        }
        panel_settings_key(key);
        return;
    }

    /* 2. Active inline text-input fields swallow printable keys */
    if (g_ui.view == VIEW_SNAPSHOTS &&
        (g_ui.prof_input != 0 || g_ui.prof_val_typing)) {
        panel_profiles_key(key);
        return;
    }
    if (g_ui.view == VIEW_FANS && g_ui.fe_input != 0) {
        editor_fan_key(key);
        return;
    }
    if ((g_ui.view == VIEW_POWER ||
         (g_ui.view == VIEW_SNAPSHOTS && g_ui.prof_in_cores)) &&
        g_ui.cf_input_active) {
        editor_corefreq_key(key);
        return;
    }
    if (g_ui.view == VIEW_POWER && g_ui.pw_typing) {
        panel_workspace_key(key);
        return;
    }
    if (g_ui.view == VIEW_SYSTEM && (g_ui.lt_hex_active || g_ui.sys_bat_typing)) {
        panel_workspace_key(key);
        return;
    }

    /* 3. Telemetry detail page owns 1..8 section jumps + Esc back to grid */
    if (g_ui.view == VIEW_TELEM && g_ui.telem_detail >= 0) {
        if (key == 'q' || key == 'Q') { try_quit(); return; }
        if (key == ',') { settings_open(); return; }
        if (key == 'L') { g_ui.log_overlay = true; g_ui.log_scroll = 0; return; }
        if (key == '?') { g_ui.help_overlay = true; return; }
        if (key == 't' || key == 'T') {
            g_ui.telem_detail = -1;
            ui_set_view(g_ui.prev_view != VIEW_TELEM ? g_ui.prev_view : VIEW_POWER);
            return;
        }
        if (key == NCKEY_TAB) {
            g_ui.telem_detail = -1;
            ui_set_view((ui_view_t)((g_ui.view + (ncinput_shift_p(ni) ? VIEW_COUNT - 1 : 1)) % VIEW_COUNT));
            return;
        }
        panel_telem_full_key(key);
        return;
    }

    /* 4. Global navigation & overlays (consistent across all 5 views) */
    switch (key) {
    case 'q':
    case 'Q':
        g_ui.prof_del_arm = -1;
        g_ui.prof_upd_arm = -1;
        try_quit();
        return;
    case ',':
        g_ui.prof_del_arm = -1;
        g_ui.prof_upd_arm = -1;
        settings_open();
        return;
    case 'L':
        g_ui.prof_del_arm = -1;
        g_ui.prof_upd_arm = -1;
        g_ui.log_overlay = true;
        g_ui.log_scroll = 0;
        return;
    case '?':
        g_ui.prof_del_arm = -1;
        g_ui.prof_upd_arm = -1;
        g_ui.help_overlay = true;
        return;
    case 't':
    case 'T':
        g_ui.prof_del_arm = -1;
        g_ui.prof_upd_arm = -1;
        if (g_ui.view == VIEW_TELEM)
            ui_set_view(g_ui.prev_view != VIEW_TELEM ? g_ui.prev_view : VIEW_POWER);
        else
            ui_set_view(VIEW_TELEM);
        return;
    case NCKEY_TAB:
        ui_set_view((ui_view_t)((g_ui.view + (ncinput_shift_p(ni) ? VIEW_COUNT - 1 : 1)) % VIEW_COUNT));
        return;
    case '1': ui_set_view(VIEW_POWER);     return;
    case '2': ui_set_view(VIEW_FANS);      return;
    case '3': ui_set_view(VIEW_SYSTEM);    return;
    case '4': ui_set_view(VIEW_SNAPSHOTS); return;
    case '5': ui_set_view(VIEW_TELEM);     return;
    case NCKEY_ESC:
        /* Esc NEVER opens a menu: exits inline cores/editor, cancels arms, or leaves telem */
        if (g_ui.view == VIEW_POWER && g_ui.pw_in_cores) {
            g_ui.pw_in_cores = false;
        } else if (g_ui.view == VIEW_SNAPSHOTS && g_ui.prof_in_edit) {
            panel_profiles_key(NCKEY_ESC);
        } else if (g_ui.prof_del_arm >= 0 || g_ui.prof_upd_arm >= 0) {
            g_ui.prof_del_arm = -1;
            g_ui.prof_upd_arm = -1;
        } else if (g_ui.view == VIEW_TELEM && g_ui.prev_view != VIEW_TELEM) {
            ui_set_view(g_ui.prev_view);
        }
        return;
    default:
        break;
    }

    /* 5. Delegate to the active view */
    switch (g_ui.view) {
    case VIEW_POWER:
    case VIEW_FANS:
    case VIEW_SYSTEM:
        panel_workspace_key(key);
        break;
    case VIEW_SNAPSHOTS:
        panel_profiles_key(key);
        break;
    case VIEW_TELEM:
        panel_telem_full_key(key);
        break;
    default:
        break;
    }
}

/* ---- main loop ------------------------------------------------------------ */

static volatile sig_atomic_t s_term_flag;

static void ui_on_term(int sig)
{
    (void)sig;
    s_term_flag = 1;
}

int ui_run(hw_state_t *hw)
{
    setlocale(LC_ALL, "");
    /* Rule 1: LC_NUMERIC stays "C" so strtod/printf never corrupt Hz lists */
    setlocale(LC_NUMERIC, "C");

    struct notcurses_options opts = {0};
    opts.flags = NCOPTION_SUPPRESS_BANNERS | NCOPTION_NO_CLEAR_BITMAPS |
                 NCOPTION_NO_WINCH_SIGHANDLER;
    opts.loglevel = NCLOGLEVEL_SILENT;

    struct notcurses *nc = notcurses_init(&opts, NULL);
    if (!nc) {
        fprintf(stderr, "ctron: failed to initialize notcurses\n");
        return -1;
    }
    notcurses_mice_enable(nc, NCMICE_BUTTON_EVENT);
    struct ncplane *stdn = notcurses_stdplane(nc);
    s_nc = nc;
    s_stdn = stdn;

    memset(&g_ui, 0, sizeof(g_ui));
    s_last_refresh_ms = 0;
    s_in_modal = false;
    atomic_init(&g_ui.apply_busy, false);
    atomic_init(&g_ui.quit_pending, false);
    g_ui.hw = hw;
    g_ui.running = true;
    g_ui.md_field = -1;
    g_ui.telem_detail = -1;
    g_ui.prof_del_arm = -1;
    g_ui.prof_upd_arm = -1;
    g_ui.set_del_arm = -1;
    g_ui.prof_meta_sel = -1;
    g_ui.mode_n = modes_load(g_ui.modes, MODES_MAX);
    tin_set(&g_ui.prof_name, "my-snapshot");
    tin_set(&g_ui.fe_save_name, "my-fan");

    hw_refresh_live(hw);
    panel_profiles_reload();
    editor_fan_sync_bufs();
    g_ui.view = (ui_view_t)ut_clamp_i(g_prefs.startup_view, 0, VIEW_COUNT - 1);
    g_ui.prev_view = VIEW_POWER;

    g_ui.lt_eff = 0;
    g_ui.lt_col = 0;
    tin_set(&g_ui.lt_hex, "00e5ff");
    g_ui.pwv_asusd_ac = -1;
    g_ui.pwv_asusd_bat = -1;
    pw_sync_from_hw();
    sys_sync_from_hw();

    ut_log("TUI ready (%d snapshots, %d modes)", g_ui.prof_n, g_ui.mode_n);

    bool apply_prev = false;
    s_term_flag = 0;
    signal(SIGTERM, ui_on_term);
    signal(SIGINT, ui_on_term);

    while (g_ui.running) {
        if (s_term_flag)
            g_ui.running = false;
        bool busy = atomic_load(&g_ui.apply_busy);
        if (apply_prev && !busy)
            finalize_apply();
        apply_prev = busy;
        if (atomic_load(&g_ui.quit_pending) && !busy)
            g_ui.running = false;

        tgt_clear();
        s_in_modal = false;

        /* clear canvas using active palette's native/solid base background */
        const palette_t *pal = ui_palette(g_prefs.theme);
        uint64_t base_ch = ui_ch(pal->text, false);
        ncplane_set_base(stdn, " ", 0, base_ch);
        ncplane_erase(stdn);

        unsigned dimy = 0, dimx = 0;
        ncplane_dim_yx(stdn, &dimy, &dimx);
        ui_layout(dimy, dimx);

        draw_header(stdn, dimy, dimx);
        draw_rail(stdn, &g_ui.rc_rail);

        switch (g_ui.view) {
        case VIEW_POWER:
        case VIEW_FANS:
        case VIEW_SYSTEM:
            panel_workspace_draw(stdn, &g_ui.rc_ws);
            break;
        case VIEW_SNAPSHOTS:
            panel_profiles_draw(stdn, &g_ui.rc_ws);
            break;
        case VIEW_TELEM:
            panel_telem_full_draw(stdn, &g_ui.rc_ws);
            break;
        default:
            break;
        }

        draw_footer(stdn, dimy, dimx);

        if (g_ui.help_overlay) {
            draw_help_overlay(stdn, &g_ui.rc_overlay);
        } else if (g_ui.log_overlay) {
            draw_log_overlay(stdn, &g_ui.rc_overlay);
        } else if (g_ui.db_overlay) {
            editor_daeboard_draw(stdn, &g_ui.rc_overlay);
            ui_modal_end();
        } else if (g_ui.fe_presets_overlay) {
            editor_fan_presets_draw(stdn, &g_ui.rc_overlay);
            ui_modal_end();
        } else if (g_ui.settings_overlay) {
            panel_settings_draw(stdn, &g_ui.rc_overlay);
            ui_modal_end();
        }

        notcurses_render(nc);

        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        long ms = g_prefs.poll_ms;
        ts.tv_nsec += ms * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000L;
        }

        struct ncinput ni = {0};
        uint32_t key = notcurses_get(nc, &ts, &ni);
        if (key == (uint32_t)-1)
            continue;
        if (key == 0 || key == NCKEY_RESIZE) {
            maybe_refresh(hw, true);
            continue;
        }

        bool force = false, any = false;
        while (key != (uint32_t)-1 && key != 0) {
            if (key == NCKEY_RESIZE) {
                force = true;
            } else if (key != '[' && key != ']') {
                any = true;
                if (nckey_mouse_p(key)) {
                    if (ni.evtype != NCTYPE_RELEASE)
                        handle_mouse(stdn, &ni, key);
                } else if (ni.evtype != NCTYPE_RELEASE) {
                    dispatch_key(key, &ni);
                }
            }
            key = notcurses_get_nblock(nc, &ni);
        }
        if (any || force)
            maybe_refresh(hw, force);
    }

    if (g_ui.apply_spawned) {
        pthread_join(g_ui.apply_tid, NULL);
        if (g_ui.hw == &s_hw_frozen)
            g_ui.hw = g_ui.apply_hw;
    }

    editor_daeboard_close();
    settings_save(hw);
    s_nc = NULL;
    s_stdn = NULL;
    notcurses_stop(nc);
    return 0;
}
