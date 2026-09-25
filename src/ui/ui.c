#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ui.h"
#include "ui_internal.h"
#include "../control.h"
#include "../util.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

ui_ctx_t g_ui;

/* ---- palette ----------------------------------------------------------- */

const char *const THEME_NAMES[] = { "Ice", "Amber", "Emerald", "Crimson", "Stealth" };

static const palette_t s_themes[THEME_COUNT] = {
    [0] = { 0x00E5FF, 0x00C5E6, 0x050A0E, 0x0D1924, 0x153040, 0xC5E6F2, 0x4D7A94 },
    [1] = { 0xFF8800, 0xFF5500, 0x0C0805, 0x1E140A, 0x442810, 0xF0E0D0, 0x907860 },
    [2] = { 0x00FF66, 0x00CC44, 0x040E06, 0x0C1E10, 0x164420, 0xD0F8D8, 0x60A070 },
    [3] = { 0xFF3366, 0xFF6688, 0x10050A, 0x220C16, 0x481628, 0xFFE0E8, 0xA06078 },
    [4] = { 0xD0D8E0, 0x8090A0, 0x080808, 0x14161A, 0x2C3038, 0xE0E4E8, 0x788088 },
};

const palette_t *ui_palette(int theme)
{
    return &s_themes[ut_clamp_i(theme, 0, THEME_COUNT - 1)];
}

/* ---- draw helpers ------------------------------------------------------- */

void ui_trunc(char *s, int maxlen_chars)
{
    if ((int)strlen(s) > maxlen_chars)
        s[maxlen_chars] = '\0';
}

void ui_putln(struct ncplane *n, int x, int y, int w,
              const char *s, uint32_t fg, bool bold)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", s ? s : "");
    ui_trunc(buf, w > 0 ? w - 1 : 0);
    uint64_t ch = 0;
    ncchannels_set_fg_rgb(&ch, fg);
    ncplane_set_channels(n, ch);
    ncplane_set_styles(n, bold ? NCSTYLE_BOLD : NCSTYLE_NONE);
    ncplane_putstr_yx(n, y, x, buf);
    ncplane_set_styles(n, NCSTYLE_NONE);
}

void ui_box(struct ncplane *n, const rect_t *r, const char *title, bool focused)
{
    if (r->w < 4 || r->h < 3)
        return;
    const palette_t *pal = ui_palette(g_prefs.theme);

    uint64_t chb = 0, cht = 0;
    ncchannels_set_fg_rgb(&chb, focused ? pal->accent : pal->border);
    ncchannels_set_bg_rgb(&chb, pal->bg);
    ncchannels_set_fg_rgb(&cht, focused ? pal->accent : pal->muted);
    ncchannels_set_bg_rgb(&cht, pal->bg);

    /* interior */
    ncplane_set_channels(n, chb);
    for (int y = 1; y < r->h - 1; y++) {
        ncplane_cursor_move_yx(n, r->y + y, r->x + 1);
        for (int x = 1; x < r->w - 1; x++)
            ncplane_putchar(n, ' ');
    }

    /* border */
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
        ncplane_set_styles(n, NCSTYLE_BOLD);
        char t[64];
        snprintf(t, sizeof(t), " %s ", title);
        ui_trunc(t, r->w - 6);
        ncplane_putstr_yx(n, r->y, r->x + 3, t);
        ncplane_set_styles(n, NCSTYLE_NONE);
    }
}

/* btop-style floating window: drop shadow one cell right/below, double
 * frame, filled interior (wipes ghost text every frame), centred title. */
void ui_window(struct ncplane *n, const rect_t *r, const char *title)
{
    if (r->w < 8 || r->h < 4)
        return;

    const palette_t *pal = ui_palette(g_prefs.theme);

    /* shadow first (under the frame's right/bottom edge) */
    uint64_t sh = 0;
    ncchannels_set_bg_rgb(&sh, 0x05070a);
    ncplane_set_channels(n, sh);
    ncplane_cursor_move_yx(n, r->y + r->h, r->x + 2);
    for (int x = 2; x < r->w + 1; x++)
        ncplane_putchar(n, ' ');
    for (int y = 1; y < r->h + 1; y++)
        ncplane_putstr_yx(n, r->y + y, r->x + r->w, " ");

    /* double frame */
    uint64_t chb = 0, cht = 0;
    ncchannels_set_fg_rgb(&chb, pal->accent);
    ncchannels_set_bg_rgb(&chb, pal->bg);
    ncchannels_set_fg_rgb(&cht, pal->accent);
    ncchannels_set_bg_rgb(&cht, pal->bg);
    ncplane_set_channels(n, chb);

    for (int y = 1; y < r->h - 1; y++) {
        ncplane_cursor_move_yx(n, r->y + y, r->x + 1);
        for (int x = 1; x < r->w - 1; x++)
            ncplane_putchar(n, ' ');
    }
    ncplane_putstr_yx(n, r->y, r->x, "╔");
    for (int x = 1; x < r->w - 1; x++)
        ncplane_putstr(n, "═");
    ncplane_putstr(n, "╗");
    for (int y = 1; y < r->h - 1; y++) {
        ncplane_putstr_yx(n, r->y + y, r->x, "║");
        ncplane_putstr_yx(n, r->y + y, r->x + r->w - 1, "║");
    }
    ncplane_putstr_yx(n, r->y + r->h - 1, r->x, "╚");
    for (int x = 1; x < r->w - 1; x++)
        ncplane_putstr(n, "═");
    ncplane_putstr(n, "╝");

    if (title) {
        char t[64];
        snprintf(t, sizeof(t), " %s ", title);
        ui_trunc(t, r->w - 4);
        ncplane_set_channels(n, cht);
        ncplane_set_styles(n, NCSTYLE_BOLD);
        ncplane_putstr_yx(n, r->y, r->x + (r->w - (int)strlen(t)) / 2, t);
        ncplane_set_styles(n, NCSTYLE_NONE);
    }
}

void ui_btn(struct ncplane *n, int x, int y, const char *label,
            bool active, bool focused, int id)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    uint64_t ch = 0;
    if (focused && active) {
        ncchannels_set_fg_rgb(&ch, 0x000000);
        ncchannels_set_bg_rgb(&ch, pal->accent);
        ncplane_set_styles(n, NCSTYLE_BOLD | NCSTYLE_UNDERLINE);
    } else if (active) {
        ncchannels_set_fg_rgb(&ch, 0x000000);
        ncchannels_set_bg_rgb(&ch, pal->accent);
        ncplane_set_styles(n, NCSTYLE_BOLD);
    } else if (focused) {
        ncchannels_set_fg_rgb(&ch, pal->accent);
        ncchannels_set_bg_rgb(&ch, pal->card);
        ncplane_set_styles(n, NCSTYLE_BOLD | NCSTYLE_UNDERLINE);
    } else {
        ncchannels_set_fg_rgb(&ch, pal->text);
        ncchannels_set_bg_rgb(&ch, pal->card);
        ncplane_set_styles(n, NCSTYLE_NONE);
    }
    ncplane_set_channels(n, ch);
    ncplane_putstr_yx(n, y, x, label);
    ncplane_set_styles(n, NCSTYLE_NONE);
    tgt_register(x, y, (int)strlen(label), 1, id);
}

int ui_btn_row(struct ncplane *n, int y, int x, int w,
               const ui_btndef_t *btns, int count)
{
    int cx = x;
    for (int i = 0; i < count; i++) {
        int len = (int)strlen(btns[i].label);
        if (cx + len > x + w)
            break; /* does not fit: skip the rest */
        ui_btn(n, cx, y, btns[i].label, btns[i].active, false, btns[i].id);
        cx += len + 1;
    }
    return cx;
}

void ui_row(struct ncplane *n, int x, int y, int w,
            const char *label, const char *value, bool selected)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    uint64_t ch = 0;
    if (selected) {
        ncchannels_set_fg_rgb(&ch, pal->accent);
        ncchannels_set_bg_rgb(&ch, pal->card);
    } else {
        ncchannels_set_fg_rgb(&ch, pal->text);
        ncchannels_set_bg_rgb(&ch, pal->bg);
    }
    ncplane_set_channels(n, ch);

    char lb[128], vb[128];
    snprintf(lb, sizeof(lb), "%s%s ", selected ? "▸ " : "  ", label);
    snprintf(vb, sizeof(vb), "%s", value ? value : "");
    int lw = w / 2 - 2;
    if (lw > 28)
        lw = 28;
    ui_trunc(lb, lw);
    char line[256];
    snprintf(line, sizeof(line), "%-*s%s", lw, lb, vb);
    ui_trunc(line, w - 2);
    ncplane_putstr_yx(n, y, x, line);
}

/* ---- mouse targets ------------------------------------------------------ */

#define MAX_TARGETS 256
static struct { int x1, y1, x2, y2, id; } s_tg[MAX_TARGETS];
static int s_tg_n;

void tgt_clear(void) { s_tg_n = 0; }

void tgt_register(int x, int y, int w, int h, int encoded)
{
    if (s_tg_n >= MAX_TARGETS)
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
    /* search newest-first: the window drawn last (e.g. the settings
     * overlay) wins over the panels underneath it */
    for (int i = s_tg_n - 1; i >= 0; i--)
        if (x >= s_tg[i].x1 && x <= s_tg[i].x2 && y >= s_tg[i].y1 && y <= s_tg[i].y2)
            return s_tg[i].id;
    return 0;
}

/* ---- text input --------------------------------------------------------- */

void tin_set(tinput_t *t, const char *s)
{
    snprintf(t->buf, sizeof(t->buf), "%s", s ? s : "");
    t->len = strlen(t->buf);
}

void tin_clear(tinput_t *t)
{
    t->buf[0] = '\0';
    t->len = 0;
}

bool tin_key(tinput_t *t, uint32_t key)
{
    if (key >= 32 && key < 127) {
        if (t->len + 1 < sizeof(t->buf)) {
            t->buf[t->len++] = (char)key;
            t->buf[t->len] = '\0';
        }
        return true;
    }
    if (key == NCKEY_BACKSPACE || key == 127 || key == 8) {
        if (t->len > 0)
            t->buf[--t->len] = '\0';
        return true;
    }
    return false; /* navigation keys fall through */
}

/* ---- layout -------------------------------------------------------------- */

/* Placement is data-driven from g_prefs (Settings overlay → LAYOUT);
 * re-evaluated every frame, so changes apply instantly. */
static void ui_layout(unsigned dimy, unsigned dimx)
{
    int W = (int)dimx, H = (int)dimy;

    int top = 1; /* top bar */
    int telem_h = ut_clamp_i(g_prefs.telem_h, 3, 10);
    int body_h = H - top - telem_h;
    if (body_h < 10) {
        body_h = H - top; /* tiny terminal: telemetry collapses away */
        telem_h = 0;
    }

    int lw = W * ut_clamp_i(g_prefs.left_pct, 25, 50) / 100;
    if (lw < 30)
        lw = 30;
    if (lw > W - 20)
        lw = W / 2;

    int split = ut_clamp_i(g_prefs.split_pct, 25, 75);
    int up_h = body_h * split / 100;
    int body_y = g_prefs.telem_top ? top + telem_h : top;
    int telem_y = g_prefs.telem_top ? top : top + body_h;

    rect_t left_top = { 0, body_y, lw, up_h };
    rect_t left_bot = { 0, body_y + up_h, lw, body_h - up_h };
    if (g_prefs.swap_left) {
        g_ui.rc_ctl  = left_top;
        g_ui.rc_prof = left_bot;
    } else {
        g_ui.rc_prof = left_top;
        g_ui.rc_ctl  = left_bot;
    }

    g_ui.rc_ws     = (rect_t){ lw, body_y, W - lw, body_h };
    g_ui.rc_telem  = (rect_t){ 0, telem_y, W, telem_h };
    if (!g_prefs.telem_top && telem_y + telem_h < H)
        g_ui.rc_telem.h = H - telem_y; /* absorb rounding at the bottom */

    /* settings overlay: centred floating window, not the whole screen.
     * the daeboard editor uses the same slot but nearly the full frame. */
    if (g_ui.db_overlay) {
        int ow = W - 4;
        int oh = H - top - 2;
        if (ow < 20)
            ow = W;
        if (oh < 8)
            oh = H - top;
        g_ui.rc_overlay = (rect_t){ (W - ow) / 2, top, ow, oh };
    } else {
        int ow = W - 6;
        if (ow > 96)
            ow = 96;
        if (ow < 20)
            ow = W;
        int oh = H - 4;
        if (oh > 30)
            oh = 30;
        if (oh < 8)
            oh = H;
        g_ui.rc_overlay = (rect_t){ (W - ow) / 2, top + (H - top - oh) / 2, ow, oh };
    }
}

static void draw_topbar(struct ncplane *n, unsigned dimy, unsigned dimx)
{
    (void)dimy;
    const palette_t *pal = ui_palette(g_prefs.theme);

    uint64_t ch = 0;
    ncchannels_set_fg_rgb(&ch, pal->accent);
    ncchannels_set_bg_rgb(&ch, pal->bg);
    ncplane_set_channels(n, ch);

    char bar[256];
    snprintf(bar, sizeof(bar), " ctron — %s ", g_ui.hw->model);
    ui_trunc(bar, (int)dimx - 46);
    ncplane_set_styles(n, NCSTYLE_BOLD);
    ncplane_putstr_yx(n, 0, 0, bar);
    ncplane_set_styles(n, NCSTYLE_NONE);

    static const char *names[FOC_COUNT] = {
        "1:profiles", "2:controls", "3:workspace", "4:telemetry"
    };
    int x = (int)dimx - 44;
    for (int i = 0; i < FOC_COUNT; i++) {
        uint64_t chf = 0;
        if (i == (int)g_ui.focus) {
            ncchannels_set_fg_rgb(&chf, 0x000000);
            ncchannels_set_bg_rgb(&chf, pal->accent);
            ncplane_set_styles(n, NCSTYLE_BOLD);
        } else {
            ncchannels_set_fg_rgb(&chf, pal->muted);
            ncchannels_set_bg_rgb(&chf, pal->bg);
        }
        ncplane_set_channels(n, chf);
        ncplane_putstr_yx(n, 0, x, names[i]);
        x += 12;
    }
    ncplane_set_styles(n, NCSTYLE_NONE);
}

/* ---- mouse --------------------------------------------------------------- */

/* v1 lesson: pixel-capable terminals report pixel coords; map back to cells
 * and clamp. Only BUTTON1 press acts; drags/releases/edges are ignored. */
static void handle_mouse(struct ncplane *stdn, const struct ncinput *ni, uint32_t key)
{
    int mx = ni->x, my = ni->y;
    unsigned dimy = 0, dimx = 0, cdy = 0, cdx = 0;
    ncplane_dim_yx(stdn, &dimy, &dimx);
    ncplane_pixel_geom(stdn, NULL, NULL, &cdy, &cdx, NULL, NULL);
    if (cdx > 1 && mx >= (int)dimx * 2)
        mx = mx / (int)cdx;
    if (cdy > 1 && my >= (int)dimy * 2)
        my = my / (int)cdy;
    if (mx < 0 || my < 0)
        return;
    if (dimx > 0 && mx >= (int)dimx)
        mx = (int)dimx - 1;
    if (dimy > 0 && my >= (int)dimy)
        my = (int)dimy - 1;

    if (key != NCKEY_BUTTON1)
        return;

    /* while the settings window is open, it is the only clickable layer:
     * a click outside closes it (btop habit), inside only its rows act */
    if (g_ui.db_overlay) {
        const rect_t *w = &g_ui.rc_overlay;
        int t;
        if (mx < w->x || mx >= w->x + w->w || my < w->y || my >= w->y + w->h) {
            g_ui.db_overlay = false;
            return;
        }
        t = tgt_find(mx, my);
        if (((t >> 24) & 0x7f) == TGT_PANEL_DAEBOARD)
            editor_daeboard_act(t & TGT_ID_MASK);
        return;
    }
    if (g_ui.settings_overlay) {
        const rect_t *w = &g_ui.rc_overlay;
        if (mx < w->x || mx >= w->x + w->w || my < w->y || my >= w->y + w->h) {
            g_ui.settings_overlay = false;
            return;
        }
        int t = tgt_find(mx, my);
        if (((t >> 24) & 0x7f) == TGT_PANEL_SETTINGS)
            panel_settings_act(t & TGT_ID_MASK);
        return;
    }

    /* fan graph hit test first (registered rect from last draw);
     * meaningless while the settings overlay covers everything */
    if (g_ui.ws_view == WSV_FAN && g_ui.focus == FOC_WORKSPACE) {
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

    switch (panel) {
    case TGT_PANEL_TOPBAR:
        g_ui.focus = (focus_t)ut_clamp_i(id, 0, FOC_COUNT - 1);
        break;
    case TGT_PANEL_PROFILES:
        g_ui.focus = FOC_PROFILES;
        panel_profiles_act(id);
        break;
    case TGT_PANEL_CONTROLS:
        g_ui.focus = FOC_CONTROLS;
        panel_controls_act(id);
        break;
    case TGT_PANEL_WORKSPACE:
        g_ui.focus = FOC_WORKSPACE;
        panel_workspace_act(id);
        break;
    case TGT_PANEL_SETTINGS:
        panel_settings_act(id);
        break;
    default:
        break;
    }
}

/* ---- main loop ------------------------------------------------------------ */

/* Quit with a guard: staged POWER edits are dropped silently otherwise,
 * so the first q just warns and the second one quits. */
static void try_quit(void)
{
    if (g_ui.pw_dirty && !g_ui.pw_quit_warned) {
        g_ui.pw_quit_warned = true;
        ut_log("POWER: staged edits pending — q again to quit");
        return;
    }
    g_ui.running = false;
}

static void dispatch_key(uint32_t key, const struct ncinput *ni)
{
    /* typing modes swallow printable keys first */
    if (g_ui.prof_typing && g_ui.focus == FOC_PROFILES) {
        panel_profiles_key(key);
        return;
    }
    if (g_ui.fe_input && g_ui.focus == FOC_WORKSPACE && g_ui.ws_view == WSV_FAN) {
        editor_fan_key(key);
        return;
    }
    if (g_ui.lt_hex_active && g_ui.focus == FOC_WORKSPACE && g_ui.ws_view == WSV_LIGHT) {
        panel_workspace_key(key);
        return;
    }
    if (g_ui.md_field >= 0 && g_ui.settings_overlay) {
        panel_settings_key(key);
        return;
    }

    if (g_ui.db_overlay) {
        editor_daeboard_key(key);
        return;
    }
    if (g_ui.settings_overlay) {
        if (key == NCKEY_ESC || key == 's' || key == 'S') {
            g_ui.settings_overlay = false;
            return;
        }
        if (key == 'q' || key == 'Q') {
            try_quit();
            return;
        }
        panel_settings_key(key);
        return;
    }

    switch (key) {
    case 'q':
    case 'Q':
        try_quit();
        return;
    case NCKEY_ESC:
    case 's':
    case 'S':
        settings_open();
        return;
    case NCKEY_TAB:
        g_ui.focus = (focus_t)((g_ui.focus + (ni->shift ? FOC_COUNT - 1 : 1)) % FOC_COUNT);
        return;
    case '1': g_ui.focus = FOC_PROFILES; return;
    case '2': g_ui.focus = FOC_CONTROLS; return;
    case '3': g_ui.focus = FOC_WORKSPACE; return;
    case '4': g_ui.focus = FOC_TELEM; return;
    case '?':
        g_ui.focus = FOC_WORKSPACE;
        ws_set_view(WSV_HELP);
        return;
    default:
        break;
    }

    switch (g_ui.focus) {
    case FOC_PROFILES:  panel_profiles_key(key); break;
    case FOC_CONTROLS:  panel_controls_key(key); break;
    case FOC_WORKSPACE: panel_workspace_key(key); break;
    default: break; /* telemetry: nothing to type */
    }
}

int ui_run(hw_state_t *hw)
{
    setlocale(LC_ALL, "");
    /* Notcurses wants LC_CTYPE for unicode, but LC_NUMERIC must stay "C":
     * strtod/printf otherwise parse "59.87" as 59 under tr_TR (decimal
     * comma) and the refresh-rate lists corrupt (59/164 ghosts). */
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

    memset(&g_ui, 0, sizeof(g_ui));
    g_ui.hw = hw;
    g_ui.running = true;
    g_ui.focus = FOC_CONTROLS;
    g_ui.ws_view = WSV_FAN;
    g_ui.md_field = -1;
    g_ui.prof_n = profile_list(g_ui.profs, MAX_PROFILES);
    g_ui.mode_n = modes_load(g_ui.modes, MODES_MAX);
    tin_set(&g_ui.prof_name, "my-profile");

    hw_refresh_live(hw);
    {
        char dbg[160]; int off = 0;
        for (int i = 0; i < hw->hz_count; i++)
            off += snprintf(dbg + off, sizeof(dbg) - off, "%s%d", i ? "," : "", hw->hz_modes[i]);
        fprintf(stderr, "DBGHZ count=%d list=[%s]\n", hw->hz_count, dbg);
    }
    hw_refresh_live(hw);
    settings_load(hw);
    g_ui.ctl_prof_idx = (int)hw->profile;
    g_ui.ctl_kbd_idx = (int)hw->kbd;
    g_ui.ctl_bat = hw->bat_limit > 0 ? hw->bat_limit : 80;
    g_ui.ctl_fan_idx = 0;
    /* start the Refresh row on the mode closest to the live rate */
    g_ui.ctl_hz_idx = 0;
    for (int i = 1; i < hw->hz_count; i++) {
        if (hw->hz_cur > 0 &&
            abs(hw->hz_modes[i] - hw->hz_cur) <
                abs(hw->hz_modes[g_ui.ctl_hz_idx] - hw->hz_cur))
            g_ui.ctl_hz_idx = i;
    }
    g_ui.lt_eff = 0;
    g_ui.lt_col = 0;
    tin_set(&g_ui.lt_hex, "00e5ff");
    pw_sync_from_hw();

    ut_log("TUI ready (%d profiles, %d modes)", g_ui.prof_n, g_ui.mode_n);

    while (g_ui.running) {
        tgt_clear();

        unsigned dimy = 0, dimx = 0;
        ncplane_dim_yx(stdn, &dimy, &dimx);
        ui_layout(dimy, dimx);
        draw_topbar(stdn, dimy, dimx);

        /* background panels always render (each fills its own box, so the
         * whole screen is wiped); the settings window floats on top */
        panel_profiles_draw(stdn, &g_ui.rc_prof);
        panel_controls_draw(stdn, &g_ui.rc_ctl);
        panel_workspace_draw(stdn, &g_ui.rc_ws);
        panel_telemetry_draw(stdn, &g_ui.rc_telem);
        if (g_ui.db_overlay)
            editor_daeboard_draw(stdn, &g_ui.rc_overlay);
        else if (g_ui.settings_overlay)
            panel_settings_draw(stdn, &g_ui.rc_overlay);

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
            hw_refresh_fast(hw);
            continue;
        }
        /* lone '[' / ']' are CSI leftovers from focus reporting; a bare
         * 0x1b here IS NCKEY_ESC (same value) and must reach the overlay
         * dispatch, so it is deliberately not filtered. */
        if (key == '[' || key == ']')
            continue;

        if (nckey_mouse_p(key)) {
            if (ni.evtype != NCTYPE_RELEASE)
                handle_mouse(stdn, &ni, key);
        } else if (ni.evtype != NCTYPE_RELEASE) {
            dispatch_key(key, &ni);
        }

        hw_refresh_fast(hw);
    }

    settings_save(hw);
    notcurses_stop(nc);
    return 0;
}
