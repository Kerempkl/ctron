#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ui_internal.h"
#include "../util.h"

#include <stdio.h>
#include <string.h>

/* Preferences & CLI shortcuts modal (','). Esc or ',' closes.
 *
 *   PREFERENCES       startup view / boot snapshot / theme / poll / write / gpu temp
 *   CLI SHORTCUTS     modes.ini list — Enter apply, n new, e edit, dd del
 *   MODE EDITOR       name + steps fields (runs inside the modal) */

enum {
    SET_STARTUP = 0,
    SET_BOOT_SNAP,
    SET_THEME,
    SET_POLL,
    SET_WRITE,
    SET_GPUTEMP,
    SET_ROW_COUNT
};

enum {
    ACT_SET_ROW_BASE = 1,
    ACT_SET_MODE_BASE = 20,
    ACT_MD_SAVE = 95,
    ACT_MD_SAVE_APPLY,
};

static const int POLL_OPTS[] = { 100, 250, 500, 1000 };
#define POLL_OPTS_N 4
static const char *const WRITE_OPTS[] = { "asusctl first", "sysfs first", "no sudo" };

static const char *onoff(bool v) { return v ? "on" : "off"; }

void settings_open(void)
{
    panel_profiles_reload();
    g_ui.mode_n = modes_load(g_ui.modes, MODES_MAX);
    if (g_ui.mode_n <= 0) {
        g_ui.set_mode_sel = 0;
        g_ui.set_in_modes = false;
    } else if (g_ui.set_mode_sel >= g_ui.mode_n) {
        g_ui.set_mode_sel = g_ui.mode_n - 1;
    }
    g_ui.set_del_arm = -1;
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
    g_ui.set_del_arm = -1;
    g_ui.md_field = 0;
    g_ui.settings_overlay = true;
}

static void row_apply(int row, int dir)
{
    switch (row) {
    case SET_STARTUP:
        g_prefs.startup_view = (g_prefs.startup_view + dir + VIEW_COUNT) % VIEW_COUNT;
        ut_log("startup view: %s", VIEW_NAMES[g_prefs.startup_view]);
        break;
    case SET_BOOT_SNAP: {
        int total = 1 + g_ui.prof_n;
        int cur = 0;
        bool found = !g_prefs.startup_snapshot[0];
        if (!found) {
            for (int i = 0; i < g_ui.prof_n; i++) {
                if (!strcmp(g_ui.profs[i], g_prefs.startup_snapshot)) {
                    cur = i + 1;
                    found = true;
                    break;
                }
            }
        }
        if (total == 1 && found) {
            ut_log("boot snapshot: off (save a snapshot in 4 Snapshots first)");
            break;
        }
        int next = !found ? 0 : (cur + dir + total) % total;
        settings_set_startup_snapshot(next == 0 ? "" : g_ui.profs[next - 1]);
        settings_save(g_ui.hw);
        ut_log("boot snapshot: %s",
               g_prefs.startup_snapshot[0] ? g_prefs.startup_snapshot : "off");
        break;
    }
    case SET_THEME:
        g_prefs.theme = (g_prefs.theme + dir + THEME_COUNT) % THEME_COUNT;
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
    ui_spawn_apply(APPLY_MODE, "mode", g_ui.modes[i].name);
}

static void draw_settings(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int x = r->x + 2, w = r->w - 4;
    if (w < 14 || r->h < 8)
        return;

    ui_window(n, r, "PREFERENCES");

    char pollv[16], bootv[80];
    snprintf(pollv, sizeof(pollv), "%d ms", g_prefs.poll_ms);
    if (!g_prefs.startup_snapshot[0]) {
        snprintf(bootv, sizeof(bootv), "%s",
                 g_ui.prof_n > 0 ? "off" : "off (no snapshots yet)");
    } else {
        bool exists = false;
        for (int i = 0; i < g_ui.prof_n; i++) {
            if (!strcmp(g_ui.profs[i], g_prefs.startup_snapshot)) {
                exists = true;
                break;
            }
        }
        if (exists)
            snprintf(bootv, sizeof(bootv), "%s (--apply-startup)",
                     g_prefs.startup_snapshot);
        else
            snprintf(bootv, sizeof(bootv), "%s (missing!)",
                     g_prefs.startup_snapshot);
    }

    const char *vals[SET_ROW_COUNT] = {
        VIEW_NAMES[ut_clamp_i(g_prefs.startup_view, 0, VIEW_COUNT - 1)],
        bootv,
        THEME_NAMES[ut_clamp_i(g_prefs.theme, 0, THEME_COUNT - 1)],
        pollv,
        WRITE_OPTS[ut_clamp_i(g_prefs.write_pref, 0, 2)],
        onoff(g_prefs.gpu_temp),
    };
    static const char *const labels[SET_ROW_COUNT] = {
        "Startup view", "Boot snapshot", "Theme", "Poll interval",
        "Write method", "GPU temp (nvidia-smi)",
    };

    int y = r->y + 1;
    int last = r->y + r->h - 1;
    ui_putln(n, x, y, w,
             "j/k/Tab move · h/l change · Enter apply · Esc/, close · saves on quit",
             pal->muted, false);
    y += 2;

    if (y < last) {
        ui_putln(n, x, y, w, "PREFERENCES", pal->accent, true);
        y += 1;
    }
    for (int i = 0; i < SET_ROW_COUNT && y < last; i++, y++) {
        ui_row(n, x, y, w, labels[i], vals[i],
               !g_ui.set_in_modes && g_ui.set_sel == i);
        tgt_register(x, y, w, 1, TGT(TGT_PANEL_SETTINGS, ACT_SET_ROW_BASE + i));
    }
    y += 1;
    if (y >= last)
        return;

    if (g_ui.set_del_arm >= g_ui.mode_n || g_ui.set_del_arm != g_ui.set_mode_sel)
        g_ui.set_del_arm = -1;
    if (g_ui.set_del_arm >= 0 && g_ui.set_mode_sel < g_ui.mode_n) {
        char warn[128];
        snprintf(warn, sizeof(warn),
                 "CLI SHORTCUTS — press d again to delete '%.24s' · other key cancels",
                 g_ui.modes[g_ui.set_mode_sel].name);
        ui_putln(n, x, y, w, warn, pal->err, true);
    } else {
        ui_putln(n, x, y, w,
                 "CLI SHORTCUTS (modes.ini) — Enter apply · n new · e edit · dd del",
                 pal->accent, true);
    }
    y += 1;
    int vis = last - y;
    if (vis < 1)
        vis = 1;
    if (g_ui.mode_n <= vis) {
        g_ui.set_mode_top = 0;
    } else {
        if (g_ui.set_mode_top > g_ui.set_mode_sel)
            g_ui.set_mode_top = g_ui.set_mode_sel;
        if (g_ui.set_mode_sel >= g_ui.set_mode_top + vis)
            g_ui.set_mode_top = g_ui.set_mode_sel - vis + 1;
        if (g_ui.set_mode_top + vis > g_ui.mode_n)
            g_ui.set_mode_top = g_ui.mode_n - vis;
        if (g_ui.set_mode_top < 0)
            g_ui.set_mode_top = 0;
    }
    for (int i = g_ui.set_mode_top; i < g_ui.mode_n && y < last; i++, y++) {
        char label[96];
        snprintf(label, sizeof(label), "%s", g_ui.modes[i].name);
        char val[128];
        snprintf(val, sizeof(val), "%s", g_ui.modes[i].steps);
        ui_row(n, x, y, w, label, val,
               g_ui.set_in_modes && i == g_ui.set_mode_sel);
        tgt_register(x, y, w, 1, TGT(TGT_PANEL_SETTINGS, ACT_SET_MODE_BASE + i));
    }
    if (g_ui.mode_n == 0 && y < last)
        ui_putln(n, x, y, w, "(no modes — press n to add)", pal->muted, false);
}

static void save_mode(bool also_apply)
{
    char name[MODE_NAME_MAX], steps[MODE_STEPS_MAX];
    snprintf(name, sizeof(name), "%.23s", g_ui.md_name.buf);
    snprintf(steps, sizeof(steps), "%.219s", g_ui.md_steps.buf);

    if (!mode_name_ok(name)) {
        ut_log("mode save FAILED: bad name (empty, [ ] or newline)");
        g_ui.md_field = -1;
        return;
    }

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
    ui_putln(n, x, r->y + 1, w, "Tab switches field · Enter saves · Esc cancels",
             pal->muted, false);

    char nm[88], st[232], name_l[96], steps_l[256];
    tin_format(&g_ui.md_name, nm, sizeof(nm), g_ui.md_field == 0);
    tin_format(&g_ui.md_steps, st, sizeof(st), g_ui.md_field == 1);
    snprintf(name_l, sizeof(name_l), "Name : %s", nm);
    snprintf(steps_l, sizeof(steps_l), "Steps: %s", st);
    ui_putln(n, x, r->y + 3, w, name_l, g_ui.md_field == 0 ? pal->accent : pal->text, true);
    ui_putln(n, x, r->y + 5, w, steps_l, g_ui.md_field == 1 ? pal->accent : pal->text, true);

    ui_putln(n, x, r->y + 7, w,
             "steps: comma list — profile performance, ppt P80, fan cool, hz max,",
             pal->muted, false);
    ui_putln(n, x, r->y + 8, w,
             "       epp power, battery 80, kbd high, aura static red, freq 4000",
             pal->muted, false);

    int by = r->y + 10;
    const ui_btndef_t row[] = {
        { " Save ",         false, TGT(TGT_PANEL_SETTINGS, ACT_MD_SAVE) },
        { " Save & apply ", false, TGT(TGT_PANEL_SETTINGS, ACT_MD_SAVE_APPLY) },
    };
    ui_btn_row(n, by, x, w, row, 2);
}

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
        if (key == NCKEY_ENTER || key == '\r' || key == '\n') {
            save_mode(false);
            return;
        }
        if (key == NCKEY_TAB || key == NCKEY_UP || key == NCKEY_DOWN) {
            g_ui.md_field = (g_ui.md_field + 1) % 2;
            return;
        }
        tinput_t *t = g_ui.md_field == 0 ? &g_ui.md_name : &g_ui.md_steps;
        tin_key(t, key);
        return;
    }

    if (key != 'd' && key != 'D')
        g_ui.set_del_arm = -1;

    switch (key) {
    case NCKEY_TAB:
        if (g_ui.mode_n > 0)
            g_ui.set_in_modes = !g_ui.set_in_modes;
        break;
    case 'j':
    case NCKEY_DOWN:
        if (g_ui.set_in_modes) {
            if (g_ui.set_mode_sel + 1 < g_ui.mode_n) {
                g_ui.set_mode_sel++;
            } else {
                g_ui.set_in_modes = false;
                g_ui.set_sel = 0;
            }
        } else if (g_ui.set_sel + 1 < SET_ROW_COUNT) {
            g_ui.set_sel++;
        } else if (g_ui.mode_n > 0) {
            g_ui.set_in_modes = true;
            g_ui.set_mode_sel = 0;
        } else {
            g_ui.set_sel = 0;
        }
        break;
    case 'k':
    case NCKEY_UP:
        if (g_ui.set_in_modes) {
            if (g_ui.set_mode_sel > 0) {
                g_ui.set_mode_sel--;
            } else {
                g_ui.set_in_modes = false;
                g_ui.set_sel = SET_ROW_COUNT - 1;
            }
        } else if (g_ui.set_sel > 0) {
            g_ui.set_sel--;
        } else if (g_ui.mode_n > 0) {
            g_ui.set_in_modes = true;
            g_ui.set_mode_sel = g_ui.mode_n - 1;
        } else {
            g_ui.set_sel = SET_ROW_COUNT - 1;
        }
        break;
    case 'h':
    case NCKEY_LEFT:
        if (!g_ui.set_in_modes)
            row_apply(g_ui.set_sel, -1);
        break;
    case 'l':
    case NCKEY_RIGHT:
    case ' ':
        if (!g_ui.set_in_modes)
            row_apply(g_ui.set_sel, 1);
        break;
    case NCKEY_ENTER:
    case '\r':
    case '\n':
        if (g_ui.set_in_modes)
            mode_apply_idx(g_ui.set_mode_sel);
        else
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
        if (g_ui.mode_n > 0 && g_ui.set_mode_sel < g_ui.mode_n) {
            if (g_ui.set_del_arm == g_ui.set_mode_sel) {
                char gone[MODE_NAME_MAX];
                snprintf(gone, sizeof(gone), "%s",
                         g_ui.modes[g_ui.set_mode_sel].name);
                g_ui.set_del_arm = -1;
                for (int i = g_ui.set_mode_sel; i + 1 < g_ui.mode_n; i++)
                    g_ui.modes[i] = g_ui.modes[i + 1];
                g_ui.mode_n--;
                if (g_ui.set_mode_sel >= g_ui.mode_n && g_ui.set_mode_sel > 0)
                    g_ui.set_mode_sel--;
                if (g_ui.mode_n == 0)
                    g_ui.set_in_modes = false;
                modes_save(g_ui.modes, g_ui.mode_n);
                ut_log("mode deleted: %s", gone);
            } else {
                g_ui.set_del_arm = g_ui.set_mode_sel;
                ut_log("press d again to delete mode '%s' (any other key cancels)",
                       g_ui.modes[g_ui.set_mode_sel].name);
            }
        }
        break;
    default:
        break;
    }
}

void panel_settings_act(int id)
{
    g_ui.set_del_arm = -1;
    if (id >= ACT_SET_ROW_BASE && id < ACT_SET_ROW_BASE + SET_ROW_COUNT) {
        int row = id - ACT_SET_ROW_BASE;
        g_ui.set_in_modes = false;
        g_ui.set_sel = row;
        row_apply(row, 1);
        return;
    }
    if (id >= ACT_SET_MODE_BASE && id < ACT_SET_MODE_BASE + MODES_MAX) {
        int i = id - ACT_SET_MODE_BASE;
        if (i < g_ui.mode_n) {
            if (g_ui.set_in_modes && i == g_ui.set_mode_sel)
                mode_apply_idx(i);
            else {
                g_ui.set_in_modes = true;
                g_ui.set_mode_sel = i;
            }
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
