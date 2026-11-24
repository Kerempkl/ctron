#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ui_internal.h"
#include "../control.h"
#include "../util.h"

#include <stdio.h>
#include <string.h>

/* View 4: SNAPSHOTS — full-height workspace table of saved .ctr states
 * ("group/name" entries group into subdirectories). Enter applies,
 * s saves current (with live capture preview), n generates a name,
 * r renames, c edits note, dd deletes with confirmation. */

enum {
    ACT_PL_ITEM = 1, /* + index */
};

enum { PL_INPUT_NONE = 0, PL_INPUT_SAVE, PL_INPUT_RENAME, PL_INPUT_NOTE };

static void ensure_meta(void)
{
    if (g_ui.prof_meta_sel == g_ui.prof_sel && g_ui.prof_meta_ok)
        return;
    g_ui.prof_meta_sel = g_ui.prof_sel;
    g_ui.prof_meta_ok = false;
    if (g_ui.prof_n > 0 && g_ui.prof_sel < g_ui.prof_n)
        g_ui.prof_meta_ok =
            profile_meta_parse(g_ui.profs[g_ui.prof_sel], &g_ui.prof_meta) == 0;
}

void panel_profiles_draw(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int x = r->x, w = r->w;
    if (w < 12 || r->h < 5)
        return;

    if (g_ui.prof_del_arm >= g_ui.prof_n || g_ui.prof_del_arm != g_ui.prof_sel)
        g_ui.prof_del_arm = -1;

    /* top section header / inline input / delete confirmation */
    int y = r->y;
    if (g_ui.prof_input != PL_INPUT_NONE) {
        const char *act = g_ui.prof_input == PL_INPUT_SAVE ? "save"
                        : g_ui.prof_input == PL_INPUT_RENAME ? "rename"
                                                             : "note";
        const tinput_t *fld = g_ui.prof_input == PL_INPUT_NOTE
                                  ? &g_ui.prof_note : &g_ui.prof_name;
        char disp[96], line[160];
        tin_format(fld, disp, sizeof(disp), true);
        snprintf(line, sizeof(line), "%s: %s   (Enter ok · Esc cancel)", act, disp);
        ui_putln(n, x, y++, w, line, pal->accent, true);
        if (g_ui.prof_input == PL_INPUT_SAVE) {
            char prev[160], line2[192];
            profile_capture_line(g_ui.hw, prev, sizeof(prev));
            snprintf(line2, sizeof(line2), "will save: %.140s", prev);
            ui_putln(n, x, y++, w, line2, pal->muted, false);
        } else {
            y++;
        }
    } else if (g_ui.prof_del_arm >= 0 && g_ui.prof_del_arm == g_ui.prof_sel) {
        char warn[128];
        snprintf(warn, sizeof(warn),
                 "press d again to delete '%.48s' · any other key cancels",
                 g_ui.profs[g_ui.prof_sel]);
        ui_putln(n, x, y++, w, warn, pal->err, true);
        y++;
    } else {
        char hdr[64];
        snprintf(hdr, sizeof(hdr), "SNAPSHOTS (%d)", g_ui.prof_n);
        ui_putln(n, x, y++, w, hdr, pal->muted, true);
        ui_hline(n, x, y++, w, pal->border);
    }

    int foot_h = 4;
    int rows = (r->y + r->h) - y - foot_h;
    if (rows < 1)
        rows = 1;

    if (g_ui.prof_n <= rows) {
        g_ui.prof_top = 0;
    } else {
        if (g_ui.prof_top > g_ui.prof_sel)
            g_ui.prof_top = g_ui.prof_sel;
        if (g_ui.prof_sel >= g_ui.prof_top + rows)
            g_ui.prof_top = g_ui.prof_sel - rows + 1;
        if (g_ui.prof_top + rows > g_ui.prof_n)
            g_ui.prof_top = g_ui.prof_n - rows;
        if (g_ui.prof_top < 0)
            g_ui.prof_top = 0;
    }

    if (g_ui.prof_n == 0) {
        ui_putln(n, x, y, w, "(none — press s to save current hardware state)",
                 pal->muted, false);
    } else {
        for (int i = g_ui.prof_top, shown = 0;
             i < g_ui.prof_n && shown < rows; i++, shown++) {
            int ry = y + shown;
            bool sel = (i == g_ui.prof_sel);
            const char *tag = (sel && g_ui.prof_del_arm == i) ? "DELETE?" : "";
            ui_row(n, x, ry, w, g_ui.profs[i], tag, sel);
            tgt_register(x, ry, w, 1, TGT(TGT_PANEL_PROFILES, ACT_PL_ITEM + i));
        }
    }

    /* selected snapshot inspector footer (cached per selection) */
    ensure_meta();
    int fy = r->y + r->h - foot_h;
    if (fy > y && g_ui.prof_n > 0 && g_ui.prof_sel < g_ui.prof_n && g_ui.prof_meta_ok) {
        ui_hline(n, x, fy, w, pal->border);
        char sum[160], diff[128];
        profile_meta_summary(&g_ui.prof_meta, sum, sizeof(sum));
        profile_meta_diff(g_ui.hw, &g_ui.prof_meta, diff, sizeof(diff));
        bool synced = strncmp(diff, "≡", 3) == 0;
        ui_putln(n, x, fy + 1, w, sum, pal->text, false);
        ui_putln(n, x, fy + 2, w, diff, synced ? pal->ok : pal->warn, false);
    }
}

static void refresh_list(void)
{
    g_ui.prof_n = profile_list(g_ui.profs, MAX_PROFILES);
    if (g_ui.prof_sel >= g_ui.prof_n && g_ui.prof_sel > 0)
        g_ui.prof_sel--;
    g_ui.prof_meta_sel = -1;
}

static void apply_selected(void)
{
    if (g_ui.prof_n == 0 || g_ui.prof_sel >= g_ui.prof_n)
        return;
    ui_spawn_apply(APPLY_SNAPSHOT, "snapshot", g_ui.profs[g_ui.prof_sel]);
}

static void save_current(void)
{
    if (!g_ui.prof_name.buf[0])
        return;
    if (profile_export(g_ui.prof_name.buf, g_ui.hw) == 0) {
        char safe[PROFILE_NAME_MAX];
        profile_sanitize_name(g_ui.prof_name.buf, safe, sizeof(safe));
        g_ui.prof_n = profile_list(g_ui.profs, MAX_PROFILES);
        for (int i = 0; i < g_ui.prof_n; i++)
            if (!strcmp(g_ui.profs[i], safe))
                g_ui.prof_sel = i;
        g_ui.prof_meta_sel = -1;
    }
}

static void rename_selected(void)
{
    if (g_ui.prof_n == 0 || g_ui.prof_sel >= g_ui.prof_n)
        return;
    const char *old = g_ui.profs[g_ui.prof_sel];
    if (profile_rename(old, g_ui.prof_name.buf) != 0)
        return;
    char safe[PROFILE_NAME_MAX];
    profile_sanitize_name(g_ui.prof_name.buf, safe, sizeof(safe));
    refresh_list();
    for (int i = 0; i < g_ui.prof_n; i++)
        if (!strcmp(g_ui.profs[i], safe))
            g_ui.prof_sel = i;
    g_ui.prof_meta_sel = -1;
}

static void note_selected(void)
{
    if (g_ui.prof_n == 0 || g_ui.prof_sel >= g_ui.prof_n)
        return;
    if (profile_set_note(g_ui.profs[g_ui.prof_sel], g_ui.prof_note.buf) == 0)
        g_ui.prof_meta_sel = -1;
}

void panel_profiles_key(uint32_t key)
{
    if (g_ui.prof_input != PL_INPUT_NONE) {
        tinput_t *fld = g_ui.prof_input == PL_INPUT_NOTE
                            ? &g_ui.prof_note : &g_ui.prof_name;
        if (key == NCKEY_ESC) {
            g_ui.prof_input = PL_INPUT_NONE;
            return;
        }
        if (key == NCKEY_ENTER || key == '\r' || key == '\n') {
            if (g_ui.prof_input == PL_INPUT_SAVE)
                save_current();
            else if (g_ui.prof_input == PL_INPUT_RENAME)
                rename_selected();
            else
                note_selected();
            g_ui.prof_input = PL_INPUT_NONE;
            return;
        }
        tin_key(fld, key);
        return;
    }

    if (key != 'd' && key != 'D')
        g_ui.prof_del_arm = -1;

    switch (key) {
    case 'j':
    case NCKEY_DOWN:
        if (g_ui.prof_sel + 1 < g_ui.prof_n)
            g_ui.prof_sel++;
        break;
    case 'k':
    case NCKEY_UP:
        if (g_ui.prof_sel > 0)
            g_ui.prof_sel--;
        break;
    case NCKEY_ENTER:
    case '\r':
    case '\n':
    case ' ':
        apply_selected();
        break;
    case 's':
    case 'S':
        if (!g_ui.prof_name.buf[0])
            tin_set(&g_ui.prof_name, "my-snapshot");
        g_ui.prof_input = PL_INPUT_SAVE;
        break;
    case 'r':
    case 'R':
        if (g_ui.prof_n > 0 && g_ui.prof_sel < g_ui.prof_n) {
            tin_set(&g_ui.prof_name, g_ui.profs[g_ui.prof_sel]);
            g_ui.prof_input = PL_INPUT_RENAME;
        }
        break;
    case 'c':
    case 'C':
        if (g_ui.prof_n > 0 && g_ui.prof_sel < g_ui.prof_n) {
            ensure_meta();
            tin_set(&g_ui.prof_note, g_ui.prof_meta_ok ? g_ui.prof_meta.note : "");
            g_ui.prof_input = PL_INPUT_NOTE;
        }
        break;
    case 'd':
    case 'D':
        if (g_ui.prof_n > 0 && g_ui.prof_sel < g_ui.prof_n) {
            if (g_ui.prof_del_arm == g_ui.prof_sel) {
                g_ui.prof_del_arm = -1;
                profile_delete(g_ui.profs[g_ui.prof_sel]);
                refresh_list();
            } else {
                g_ui.prof_del_arm = g_ui.prof_sel;
                ut_log("press d again to delete '%s' (any other key cancels)",
                       g_ui.profs[g_ui.prof_sel]);
            }
        }
        break;
    case 'n':
    case 'N': {
        char name[32];
        profile_gen_name(name, sizeof(name));
        tin_set(&g_ui.prof_name, name);
        g_ui.prof_input = PL_INPUT_SAVE;
        break;
    }
    default:
        break;
    }
}

void panel_profiles_act(int id)
{
    g_ui.prof_del_arm = -1;
    if (id >= ACT_PL_ITEM && id < ACT_PL_ITEM + MAX_PROFILES) {
        int i = id - ACT_PL_ITEM;
        if (i < g_ui.prof_n) {
            if (i == g_ui.prof_sel)
                apply_selected();
            else
                g_ui.prof_sel = i;
        }
    }
}
