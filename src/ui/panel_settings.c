#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ui_internal.h"
#include "../util.h"

#include <stdio.h>
#include <string.h>

/* Fullscreen settings overlay (ESC / 's'). Covers every panel:
 *
 *   LAYOUT            swap left panels, telemetry position, column/split
 *                     ratios, telemetry height — applied live
 *   PREFERENCES       poll interval / write method / theme / gpu temp
 *   CLI SHORTCUTS     modes.ini list — Enter apply, n new, e edit, d del
 *   MODE EDITOR       name + steps fields (runs inside the overlay) */

enum {
    SET_SWAPLEFT = 0,
    SET_TELEMTOP,
    SET_LEFTPCT,
    SET_SPLITPCT,
    SET_TELEMH,
    SET_POLL,
    SET_WRITE,
    SET_THEME,
    SET_GPUTEMP,
    SET_ROW_COUNT      /* unified settings rows (LAYOUT + PREFERENCES) */
};
#define SET_PREF_FIRST SET_POLL

enum {
    ACT_SET_ROW_BASE = 1,   /* + row (0..SET_ROW_COUNT-1) */
    ACT_SET_MODE_BASE = 20, /* + index */
    ACT_SET_MODE_NEW = 90,
    ACT_SET_MODE_EDIT,
    ACT_SET_MODE_DEL,
    ACT_MD_SAVE = 95,
    ACT_MD_SAVE_APPLY,
};

static const int POLL_OPTS[] = { 100, 250, 500, 1000 };
#define POLL_OPTS_N 4
static const char *const WRITE_OPTS[] = { "asusctl first", "sysfs first", "no sudo" };

static const char *onoff(bool v) { return v ? "on" : "off"; }

void settings_open(void)
{
    g_ui.mode_n = modes_load(g_ui.modes, MODES_MAX);
    g_ui.settings_overlay = true;
}

void ws_start_mode_edit(const mode_def_t *m)
{
    if (m) {
        tin_set(&g_ui.md_name, m->name);
        tin_set(&g_ui.md_steps, m->steps);
    } else {
        tin_clear(&g_ui.md_name);
        tin_clear(&g_ui.md_steps);
    }
    g_ui.md_field = 0;
    g_ui.settings_overlay = true;
}

/* ---- settings rows ------------------------------------------------------ */

static void row_apply(int row, int dir)
{
    switch (row) {
    case SET_SWAPLEFT:
        g_prefs.swap_left = !g_prefs.swap_left;
        break;
    case SET_TELEMTOP:
        g_prefs.telem_top = !g_prefs.telem_top;
        break;
    case SET_LEFTPCT:
        g_prefs.left_pct = ut_clamp_i(g_prefs.left_pct + 2 * dir, 25, 50);
        break;
    case SET_SPLITPCT:
        g_prefs.split_pct = ut_clamp_i(g_prefs.split_pct + 5 * dir, 25, 75);
        break;
    case SET_TELEMH:
        g_prefs.telem_h = ut_clamp_i(g_prefs.telem_h + dir, 3, 10);
        break;
    case SET_POLL: {
        int idx = 0;
        for (int i = 0; i < POLL_OPTS_N; i++)
            if (g_prefs.poll_ms == POLL_OPTS[i])
                idx = i;
        idx = (idx + dir + POLL_OPTS_N) % POLL_OPTS_N;
        g_prefs.poll_ms = POLL_OPTS[idx];
        break;
    }
    case SET_WRITE:
        g_prefs.write_pref = (g_prefs.write_pref + dir + 3) % 3;
        ut_log("write method: %s", WRITE_OPTS[g_prefs.write_pref]);
        break;
    case SET_THEME:
        g_prefs.theme = (g_prefs.theme + dir + THEME_COUNT) % THEME_COUNT;
        break;
    case SET_GPUTEMP:
        g_prefs.gpu_temp = !g_prefs.gpu_temp;
        break;
    default:
        break;
    }
}

static void mode_apply_idx(int i)
{
    if (i < 0 || i >= g_ui.mode_n)
        return;
    char err[128];
    if (mode_apply(g_ui.hw, g_ui.modes[i].steps, err, sizeof(err)) == 0)
        ut_log("mode '%s' applied", g_ui.modes[i].name);
    else
        ut_log("mode '%s': %s", g_ui.modes[i].name, err[0] ? err : "failed");
}

/* ---- settings view ------------------------------------------------------ */

static void draw_settings(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int x = r->x + 2, w = r->w - 4;

    /* floating window: shadow + double frame + interior fill wipes the
     * panels underneath every frame (no ghost text) */
    ui_window(n, r, "SETTINGS");

    char pollv[16], leftv[16], splitv[16], telemhv[16];
    snprintf(pollv,   sizeof(pollv),   "%d ms", g_prefs.poll_ms);
    snprintf(leftv,   sizeof(leftv),   "%d %%", g_prefs.left_pct);
    snprintf(splitv,  sizeof(splitv),  "%d %%", g_prefs.split_pct);
    snprintf(telemhv, sizeof(telemhv), "%d rows", g_prefs.telem_h);

    const char *vals[SET_ROW_COUNT] = {
        onoff(g_prefs.swap_left),
        onoff(g_prefs.telem_top),
        leftv,
        splitv,
        telemhv,
        pollv,
        WRITE_OPTS[ut_clamp_i(g_prefs.write_pref, 0, 2)],
        THEME_NAMES[ut_clamp_i(g_prefs.theme, 0, THEME_COUNT - 1)],
        onoff(g_prefs.gpu_temp),
    };
    static const char *const labels[SET_ROW_COUNT] = {
        "Swap left panels", "Telemetry at top", "Left column",
        "Left split (upper panel)", "Telemetry height",
        "Poll interval", "Write method", "Theme", "GPU temp (nvidia-smi)",
    };

    int y = r->y + 1;
    ui_putln(n, x, y, w, "Esc/s closes · click outside closes · changes persist on quit",
             pal->muted, false);
    y += 2;

    ui_putln(n, x, y, w, "LAYOUT (applied instantly)", pal->accent, true);
    y += 1;
    for (int i = 0; i < SET_PREF_FIRST; i++, y++) {
        ui_row(n, x, y, w, labels[i], vals[i], g_ui.set_sel == i);
        tgt_register(x, y, w, 1, TGT(TGT_PANEL_SETTINGS, ACT_SET_ROW_BASE + i));
    }
    y += 1;

    ui_putln(n, x, y, w, "PREFERENCES", pal->accent, true);
    y += 1;
    for (int i = SET_PREF_FIRST; i < SET_ROW_COUNT; i++, y++) {
        ui_row(n, x, y, w, labels[i], vals[i], g_ui.set_sel == i);
        tgt_register(x, y, w, 1, TGT(TGT_PANEL_SETTINGS, ACT_SET_ROW_BASE + i));
    }
    y += 2;

    ui_putln(n, x, y, w, "CLI SHORTCUTS (modes.ini) — Enter apply · n new · e edit · d del",
             pal->accent, true);
    y += 1;
    int last = r->y + r->h - 1;
    for (int i = 0; i < g_ui.mode_n && y < last; i++, y++) {
        char label[96];
        snprintf(label, sizeof(label), "%s%s", i == g_ui.set_mode_sel ? "▸ " : "  ",
                 g_ui.modes[i].name);
        char val[128];
        snprintf(val, sizeof(val), "%s", g_ui.modes[i].steps);
        ui_row(n, x, y, w, label, val, i == g_ui.set_mode_sel);
        tgt_register(x, y, w, 1, TGT(TGT_PANEL_SETTINGS, ACT_SET_MODE_BASE + i));
    }
    if (g_ui.mode_n == 0 && y < last)
        ui_putln(n, x, y, w, "(no modes)", pal->muted, false);
}

/* ---- mode editor view --------------------------------------------------- */

static void save_mode(bool also_apply)
{
    char name[MODE_NAME_MAX], steps[MODE_STEPS_MAX];
    snprintf(name, sizeof(name), "%.23s", g_ui.md_name.buf);
    snprintf(steps, sizeof(steps), "%.219s", g_ui.md_steps.buf);

    int idx = mode_find(g_ui.modes, g_ui.mode_n, name);
    if (idx < 0 && g_ui.mode_n < MODES_MAX)
        idx = g_ui.mode_n++;
    if (idx < 0)
        return;
    snprintf(g_ui.modes[idx].name, MODE_NAME_MAX, "%s", name);
    snprintf(g_ui.modes[idx].steps, MODE_STEPS_MAX, "%s", steps);
    g_ui.set_mode_sel = idx;

    if (modes_save(g_ui.modes, g_ui.mode_n) == 0)
        ut_log("mode saved: %s", name);
    else
        ut_log("mode save FAILED");

    if (also_apply)
        mode_apply_idx(idx);

    g_ui.md_field = -1;
}

static void draw_modeedit(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int x = r->x + 2, w = r->w - 4;

    ui_window(n, r, "MODE EDITOR");

    ui_putln(n, x, r->y + 1, w, "Tab switches field · Esc cancels",
             pal->muted, false);

    char name_l[96], steps_l[256];
    snprintf(name_l, sizeof(name_l), "Name : %s%s",
             g_ui.md_name.buf, g_ui.md_field == 0 ? "_" : "");
    snprintf(steps_l, sizeof(steps_l), "Steps: %s%s",
             g_ui.md_steps.buf, g_ui.md_field == 1 ? "_" : "");
    ui_putln(n, x, r->y + 3, w, name_l, g_ui.md_field == 0 ? pal->accent : pal->text, true);
    ui_putln(n, x, r->y + 5, w, steps_l, g_ui.md_field == 1 ? pal->accent : pal->text, true);

    ui_putln(n, x, r->y + 7, w,
             "steps: comma list — profile performance, ppt P80, fan cool, hz max,",
             pal->muted, false);
    ui_putln(n, x, r->y + 8, w,
             "        epp power, battery 80, kbd high, aura static red, freq 4000",
             pal->muted, false);

    int by = r->y + 10;
    const ui_btndef_t row[] = {
        { " Save ",         false, TGT(TGT_PANEL_SETTINGS, ACT_MD_SAVE) },
        { " Save & apply ", false, TGT(TGT_PANEL_SETTINGS, ACT_MD_SAVE_APPLY) },
    };
    ui_btn_row(n, by, x, w, row, 2);
}

/* ---- dispatcher ---------------------------------------------------------- */

void panel_settings_draw(struct ncplane *n, const rect_t *r)
{
    if (g_ui.md_field >= 0)
        draw_modeedit(n, r);
    else
        draw_settings(n, r);
}

void panel_settings_key(uint32_t key)
{
    if (g_ui.md_field >= 0) {
        if (key == NCKEY_ESC) {
            g_ui.md_field = -1;
            return;
        }
        if (g_ui.md_field == 2 || key == NCKEY_ENTER || key == '\r' || key == '\n') {
            save_mode(false);
            return;
        }
        if (key == NCKEY_TAB) {
            g_ui.md_field = (g_ui.md_field + 1) % 2;
            return;
        }
        tinput_t *t = g_ui.md_field == 0 ? &g_ui.md_name : &g_ui.md_steps;
        if (!tin_key(t, key))
            g_ui.md_field = -1; /* navigation keys leave the editor */
        return;
    }

    switch (key) {
    case 'j':
    case NCKEY_DOWN:
        g_ui.set_sel = (g_ui.set_sel + 1) % SET_ROW_COUNT;
        break;
    case 'k':
    case NCKEY_UP:
        g_ui.set_sel = (g_ui.set_sel + SET_ROW_COUNT - 1) % SET_ROW_COUNT;
        break;
    case 'h':
    case NCKEY_LEFT:
        row_apply(g_ui.set_sel, -1);
        break;
    case 'l':
    case NCKEY_RIGHT:
    case NCKEY_ENTER:
    case '\r':
    case '\n':
    case ' ':
        row_apply(g_ui.set_sel, 1);
        break;
    case 'n':
    case 'N':
        ws_start_mode_edit(NULL);
        break;
    case 'e':
    case 'E':
        if (g_ui.set_mode_sel < g_ui.mode_n)
            ws_start_mode_edit(&g_ui.modes[g_ui.set_mode_sel]);
        break;
    case 'd':
    case 'D':
        if (g_ui.set_mode_sel < g_ui.mode_n) {
            for (int i = g_ui.set_mode_sel; i + 1 < g_ui.mode_n; i++)
                g_ui.modes[i] = g_ui.modes[i + 1];
            g_ui.mode_n--;
            if (g_ui.set_mode_sel >= g_ui.mode_n && g_ui.set_mode_sel > 0)
                g_ui.set_mode_sel--;
            modes_save(g_ui.modes, g_ui.mode_n);
        }
        break;
    default:
        break;
    }
}

void panel_settings_act(int id)
{
    if (id >= ACT_SET_ROW_BASE && id < ACT_SET_ROW_BASE + SET_ROW_COUNT) {
        int row = id - ACT_SET_ROW_BASE;
        g_ui.set_sel = row;
        row_apply(row, 1);
        return;
    }
    if (id >= ACT_SET_MODE_BASE && id < ACT_SET_MODE_BASE + MODES_MAX) {
        int i = id - ACT_SET_MODE_BASE;
        if (i < g_ui.mode_n) {
            if (i == g_ui.set_mode_sel)
                mode_apply_idx(i);
            else
                g_ui.set_mode_sel = i;
        }
        return;
    }
    switch (id) {
    case ACT_MD_SAVE:
    case ACT_MD_SAVE_APPLY:
        save_mode(id == ACT_MD_SAVE_APPLY);
        break;
    default:
        break;
    }
}
