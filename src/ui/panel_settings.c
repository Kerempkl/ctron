#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ui_internal.h"
#include "../util.h"

#include <stdio.h>
#include <string.h>

/* Settings view (inside the workspace) and the mode ("CLI shortcut")
 * editor. Layout:
 *
 *   PREFERENCES
 *     poll interval / write method / theme / gpu temp
 *   CLI SHORTCUTS
 *     list of modes -> Enter apply, n new, e edit, d delete
 *   MODE EDITOR (WSV_MODEEDIT)
 *     name field + steps field + save/apply */

enum {
    SET_POLL = 0,
    SET_WRITE,
    SET_THEME,
    SET_GPUTEMP,
    SET_PREF_ROWS
};

enum {
    ACT_SET_PREF_BASE = 1,  /* + row */
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
static const char *const WRITE_HINT[] = {
    "asusctl when available, then direct sysfs, then sudo -n",
    "direct sysfs first, then asusctl, then sudo -n",
    "never escalate; only unprivileged sysfs writes",
};

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
    g_ui.ws_view = WSV_MODEEDIT;
}

/* ---- settings view ----------------------------------------------------- */

static void pref_apply(int row, int dir)
{
    switch (row) {
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

static void draw_settings(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int x = r->x + 2, w = r->w - 4;

    char pollv[16];
    snprintf(pollv, sizeof(pollv), "%d ms", g_prefs.poll_ms);
    char gpuv[8] = "on";
    if (!g_prefs.gpu_temp)
        snprintf(gpuv, sizeof(gpuv), "off");

    const char *vals[SET_PREF_ROWS] = {
        pollv,
        WRITE_OPTS[ut_clamp_i(g_prefs.write_pref, 0, 2)],
        THEME_NAMES[ut_clamp_i(g_prefs.theme, 0, THEME_COUNT - 1)],
        gpuv,
    };
    static const char *const labels[SET_PREF_ROWS] = {
        "Poll interval", "Write method", "Theme", "GPU temp (nvidia-smi)",
    };

    int y = r->y + 1;
    ui_putln(n, x, y, w, "PREFERENCES", pal->accent, true);
    y += 1;
    for (int i = 0; i < SET_PREF_ROWS; i++, y++) {
        bool sel = (g_ui.set_sel == i);
        ui_row(n, x, y, w, labels[i], vals[i], sel);
        tgt_register(x, y, w, 1, TGT(TGT_PANEL_WORKSPACE, ACT_SET_PREF_BASE + i));
    }
    ui_putln(n, x, y, w, WRITE_HINT[ut_clamp_i(g_prefs.write_pref, 0, 2)], pal->muted, false);
    y += 2;

    ui_putln(n, x, y, w, "CLI SHORTCUTS (modes.ini) — Enter apply · n new · e edit · d del",
             pal->accent, true);
    y += 1;
    int rows = r->y + r->h - 1;
    for (int i = 0; i < g_ui.mode_n && y < rows; i++, y++) {
        char label[96];
        snprintf(label, sizeof(label), "%s%s", i == g_ui.set_mode_sel ? "▸ " : "  ",
                 g_ui.modes[i].name);
        char val[128];
        snprintf(val, sizeof(val), "%s", g_ui.modes[i].steps);
        ui_row(n, x, y, w, label, val, i == g_ui.set_mode_sel);
        tgt_register(x, y, w, 1, TGT(TGT_PANEL_WORKSPACE, ACT_SET_MODE_BASE + i));
    }
    if (g_ui.mode_n == 0 && y < rows)
        ui_putln(n, x, y, w, "(no modes)", pal->muted, false);
}

/* ---- mode editor view --------------------------------------------------- */

static void save_mode(bool also_apply)
{
    char name[MODE_NAME_MAX], steps[MODE_STEPS_MAX];
    snprintf(name, sizeof(name), "%.23s", g_ui.md_name.buf);
    snprintf(steps, sizeof(steps), "%.219s", g_ui.md_steps.buf);

    /* replace or append */
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
    g_ui.ws_view = WSV_SETTINGS;
}

static void draw_modeedit(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int x = r->x + 2, w = r->w - 4;

    ui_putln(n, x, r->y + 1, w, "MODE EDITOR — Tab switches field, Esc cancels",
             pal->accent, true);

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
    ui_btn(n, x, by, " Save (Esc) ", false, false, TGT(TGT_PANEL_WORKSPACE, ACT_MD_SAVE));
    ui_btn(n, x + 14, by, " Save & apply ", false, false,
           TGT(TGT_PANEL_WORKSPACE, ACT_MD_SAVE_APPLY));
}

/* ---- dispatcher ---------------------------------------------------------- */

void panel_settings_draw(struct ncplane *n, const rect_t *r)
{
    if (g_ui.ws_view == WSV_MODEEDIT)
        draw_modeedit(n, r);
    else
        draw_settings(n, r);
}

void panel_settings_key(uint32_t key)
{
    if (g_ui.ws_view == WSV_MODEEDIT) {
        if (key == NCKEY_ESC) {
            g_ui.md_field = -1;
            g_ui.ws_view = WSV_SETTINGS;
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
        if (!tin_key(t, key)) {
            /* navigation keys leave the editor */
            g_ui.md_field = -1;
            g_ui.ws_view = WSV_SETTINGS;
        }
        return;
    }

    switch (key) {
    case 'j':
    case NCKEY_DOWN:
        g_ui.set_mode_sel = (g_ui.set_mode_sel + 1) % (g_ui.mode_n > 0 ? g_ui.mode_n : 1);
        break;
    case 'k':
    case NCKEY_UP:
        g_ui.set_mode_sel = (g_ui.set_mode_sel + (g_ui.mode_n > 0 ? g_ui.mode_n : 1) - 1) %
                            (g_ui.mode_n > 0 ? g_ui.mode_n : 1);
        break;
    case 'h':
    case NCKEY_LEFT:
        pref_apply(g_ui.set_sel, -1);
        break;
    case 'l':
    case NCKEY_RIGHT:
        pref_apply(g_ui.set_sel, 1);
        break;
    case NCKEY_ENTER:
    case '\r':
    case '\n':
    case ' ':
        if (g_ui.set_sel < SET_PREF_ROWS)
            pref_apply(g_ui.set_sel, 1);
        else
            mode_apply_idx(g_ui.set_mode_sel);
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
    if (id >= ACT_SET_PREF_BASE && id < ACT_SET_PREF_BASE + SET_PREF_ROWS) {
        int row = id - ACT_SET_PREF_BASE;
        g_ui.set_sel = row;
        pref_apply(row, 1);
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
