#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ui_internal.h"
#include "../control.h"
#include "../util.h"

#include <stdio.h>
#include <string.h>

/* Left-top panel: SNAPSHOTS — saved .ctr states ("group/name" entries
 * group into subdirectories). Enter applies, s saves the current
 * state (with a live preview of what will be captured), r renames,
 * c edits the one-line note, d deletes. While a snapshot is selected
 * the footer shows its summary plus which tracked fields differ from
 * the live state (Δ … / ≡ live). */

enum {
    ACT_PL_ITEM = 1,   /* + index */
    ACT_PL_APPLY = 900,
    ACT_PL_SAVE,
    ACT_PL_DEL,
    ACT_PL_NEW,
};

enum { PL_INPUT_NONE = 0, PL_INPUT_SAVE, PL_INPUT_RENAME, PL_INPUT_NOTE };

static int list_rows(void)
{
    return g_ui.rc_prof.h - 7; /* list + summary + diff + button rows */
}

/* parse the selected snapshot's metadata only when the selection
 * changed — the draw-time diff is then just integer compares */
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

    ui_box(n, r, "SNAPSHOTS", g_ui.focus == FOC_PROFILES);
    int x = r->x + 2, w = r->w - 4;
    if (w < 10)
        return;

    int rows = list_rows();
    /* keep the selection inside the visible window (scroll) */
    if (g_ui.prof_n <= rows)
        g_ui.prof_top = 0;
    else {
        if (g_ui.prof_top > g_ui.prof_sel)
            g_ui.prof_top = g_ui.prof_sel;
        if (g_ui.prof_sel >= g_ui.prof_top + rows)
            g_ui.prof_top = g_ui.prof_sel - rows + 1;
        if (g_ui.prof_top + rows > g_ui.prof_n)
            g_ui.prof_top = g_ui.prof_n - rows;
        if (g_ui.prof_top < 0)
            g_ui.prof_top = 0;
    }

    if (g_ui.prof_input != PL_INPUT_NONE) {
        const char *act = g_ui.prof_input == PL_INPUT_SAVE ? "save"
                        : g_ui.prof_input == PL_INPUT_RENAME ? "rename"
                                                             : "note";
        const tinput_t *fld = g_ui.prof_input == PL_INPUT_NOTE
                                  ? &g_ui.prof_note : &g_ui.prof_name;
        char line[160];
        snprintf(line, sizeof(line), "%s: %.90s_ · Enter ok · Esc cancel",
                 act, fld->buf);
        ui_putln(n, x, r->y + 1, w, line, pal->accent, true);
        if (g_ui.prof_input == PL_INPUT_SAVE) {
            char prev[160];
            profile_capture_line(g_ui.hw, prev, sizeof(prev));
            char line2[192];
            snprintf(line2, sizeof(line2), "will save: %.140s", prev);
            ui_putln(n, x, r->y + 2, w, line2, pal->muted, false);
        }
    } else {
        char hint[80] = "j/k · Enter apply · s save · r ren · c note · d del";
        if (g_ui.prof_n > rows) {
            if (g_ui.prof_top > 0)
                strncat(hint, " ▲", sizeof(hint) - strlen(hint) - 1);
            if (g_ui.prof_top + rows < g_ui.prof_n)
                strncat(hint, " ▼", sizeof(hint) - strlen(hint) - 1);
        }
        ui_putln(n, x, r->y + 1, w, hint, pal->muted, false);
    }

    int shown = 0;
    for (int i = g_ui.prof_top; i < g_ui.prof_n && shown < rows; i++) {
        int y = r->y + 3 + shown;
        char label[64];
        snprintf(label, sizeof(label), "%s%s", i == g_ui.prof_sel ? "▸ " : "  ",
                 g_ui.profs[i]);
        ui_row(n, x, y, w, label, "", false);
        tgt_register(x, y, w, 1, TGT(TGT_PANEL_PROFILES, ACT_PL_ITEM + i));
        shown++;
    }
    if (g_ui.prof_n == 0)
        ui_putln(n, x, r->y + 3, w, "(none — press s to save current)",
                 pal->muted, false);

    /* selection footer: summary + live diff (meta cached per selection) */
    ensure_meta();
    if (g_ui.prof_n > 0 && g_ui.prof_sel < g_ui.prof_n) {
        char sum[128];
        if (profile_summary(g_ui.profs[g_ui.prof_sel], sum, sizeof(sum)) == 0)
            ui_putln(n, x, r->y + r->h - 4, w, sum, pal->text, false);
        if (g_ui.prof_meta_ok) {
            char diff[96];
            profile_meta_diff(g_ui.hw, &g_ui.prof_meta, diff, sizeof(diff));
            ui_putln(n, x, r->y + r->h - 3, w, diff, pal->muted, false);
        }
    }

    int by = r->y + r->h - 2;
    const ui_btndef_t row[] = {
        { " Apply ", false, TGT(TGT_PANEL_PROFILES, ACT_PL_APPLY) },
        { " Save ",  false, TGT(TGT_PANEL_PROFILES, ACT_PL_SAVE) },
        { " Del ",   false, TGT(TGT_PANEL_PROFILES, ACT_PL_DEL) },
        { " New ",   g_ui.prof_input == PL_INPUT_SAVE,
          TGT(TGT_PANEL_PROFILES, ACT_PL_NEW) },
    };
    ui_btn_row(n, by, x, w, row, 4);
}

static void refresh_list(void)
{
    g_ui.prof_n = profile_list(g_ui.profs, MAX_PROFILES);
    if (g_ui.prof_sel >= g_ui.prof_n && g_ui.prof_sel > 0)
        g_ui.prof_sel--;
    g_ui.prof_meta_sel = -1; /* force a meta re-parse */
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
        g_ui.prof_n = profile_list(g_ui.profs, MAX_PROFILES);
        for (int i = 0; i < g_ui.prof_n; i++)
            if (!strcmp(g_ui.profs[i], g_ui.prof_name.buf))
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
    refresh_list();
    for (int i = 0; i < g_ui.prof_n; i++)
        if (!strcmp(g_ui.profs[i], g_ui.prof_name.buf))
            g_ui.prof_sel = i;
    g_ui.prof_meta_sel = -1;
}

static void note_selected(void)
{
    if (g_ui.prof_n == 0 || g_ui.prof_sel >= g_ui.prof_n)
        return;
    if (profile_set_note(g_ui.profs[g_ui.prof_sel], g_ui.prof_note.buf) == 0)
        g_ui.prof_meta_sel = -1; /* refresh the cached summary */
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
            profile_delete(g_ui.profs[g_ui.prof_sel]);
            refresh_list();
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
    if (id >= ACT_PL_ITEM && id < ACT_PL_ITEM + MAX_PROFILES) {
        int i = id - ACT_PL_ITEM;
        if (i < g_ui.prof_n) {
            if (i == g_ui.prof_sel)
                apply_selected(); /* second click applies */
            else
                g_ui.prof_sel = i;
        }
        return;
    }
    switch (id) {
    case ACT_PL_APPLY: apply_selected(); break;
    case ACT_PL_SAVE:
        if (!g_ui.prof_name.buf[0])
            tin_set(&g_ui.prof_name, "my-snapshot");
        g_ui.prof_input = PL_INPUT_SAVE;
        break;
    case ACT_PL_DEL:
        panel_profiles_key('d');
        break;
    case ACT_PL_NEW:
        panel_profiles_key('n');
        break;
    default:
        break;
    }
}
