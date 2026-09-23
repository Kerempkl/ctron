#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ui_internal.h"
#include "../control.h"
#include "../util.h"

#include <stdio.h>
#include <string.h>

/* Left-top panel: saved .ctr profiles — list, apply, save, delete, rename.
 *
 * Keys (panel focused): j/k select · Enter apply · s save current ·
 * d delete · n new name (then Enter saves, Esc cancels) */

enum {
    ACT_PL_ITEM = 1,   /* + index */
    ACT_PL_APPLY = 900,
    ACT_PL_SAVE,
    ACT_PL_DEL,
    ACT_PL_NEW,
};

static int list_rows(void)
{
    return g_ui.rc_prof.h - 6;
}

void panel_profiles_draw(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    hw_state_t *hw = g_ui.hw;

    ui_box(n, r, "PROFILES", g_ui.focus == FOC_PROFILES);
    int x = r->x + 2, w = r->w - 4;
    if (w < 10)
        return;

    int rows = list_rows();
    if (g_ui.prof_typing) {
        char line[128];
        snprintf(line, sizeof(line), "name: %s_", g_ui.prof_name.buf);
        ui_putln(n, x, r->y + 1, w, line, pal->accent, true);
        ui_putln(n, x, r->y + 2, w, "Enter: save · Esc: cancel", pal->muted, false);
    } else {
        ui_putln(n, x, r->y + 1, w, "j/k select · Enter apply · s save", pal->muted, false);
    }

    int shown = 0;
    for (int i = 0; i < g_ui.prof_n && shown < rows; i++) {
        int y = r->y + 3 + shown;
        char label[64];
        snprintf(label, sizeof(label), "%s%s", i == g_ui.prof_sel ? "▸ " : "  ",
                 g_ui.profs[i]);
        bool sel = (i == g_ui.prof_sel && g_ui.focus == FOC_PROFILES);
        ui_row(n, x, y, w, label, "", false);
        (void)sel;
        tgt_register(x, y, w, 1, TGT(TGT_PANEL_PROFILES, ACT_PL_ITEM + i));
        if (i == g_ui.prof_sel) {
            char dot[8] = "●";
            (void)dot;
        }
        shown++;
    }
    if (g_ui.prof_n == 0)
        ui_putln(n, x, r->y + 3, w, "(none — press s to save current)", pal->muted, false);

    /* summary of selection */
    if (g_ui.prof_n > 0 && g_ui.prof_sel < g_ui.prof_n) {
        char sum[128];
        if (profile_summary(g_ui.profs[g_ui.prof_sel], sum, sizeof(sum)) == 0)
            ui_putln(n, x, r->y + r->h - 3, w, sum, pal->text, false);
    }

    int by = r->y + r->h - 2;
    const ui_btndef_t row[] = {
        { " Apply ", false, TGT(TGT_PANEL_PROFILES, ACT_PL_APPLY) },
        { " Save ",  false, TGT(TGT_PANEL_PROFILES, ACT_PL_SAVE) },
        { " Del ",   false, TGT(TGT_PANEL_PROFILES, ACT_PL_DEL) },
        { " New ",   g_ui.prof_typing, TGT(TGT_PANEL_PROFILES, ACT_PL_NEW) },
    };
    ui_btn_row(n, by, x, w, row, 4);
    (void)hw;
}

static void apply_selected(void)
{
    if (g_ui.prof_n == 0 || g_ui.prof_sel >= g_ui.prof_n)
        return;
    char err[128];
    if (profile_import(g_ui.profs[g_ui.prof_sel], g_ui.hw, err, sizeof(err)) == 0)
        ut_log("applied profile '%s'", g_ui.profs[g_ui.prof_sel]);
    else
        ut_log("apply failed: %s", err[0] ? err : "?");
    ctrl_fan_write(g_ui.hw); /* curves ride along in the profile */
    pw_sync_from_hw();       /* the profile may have changed power fields */
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
    }
}

void panel_profiles_key(uint32_t key)
{
    if (g_ui.prof_typing) {
        if (key == NCKEY_ESC) {
            g_ui.prof_typing = false;
            return;
        }
        if (key == NCKEY_ENTER || key == '\r' || key == '\n') {
            g_ui.prof_typing = false;
            save_current();
            return;
        }
        tin_key(&g_ui.prof_name, key);
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
        g_ui.prof_typing = true;
        break;
    case 'd':
    case 'D':
        if (g_ui.prof_n > 0 && g_ui.prof_sel < g_ui.prof_n) {
            profile_delete(g_ui.profs[g_ui.prof_sel]);
            g_ui.prof_n = profile_list(g_ui.profs, MAX_PROFILES);
            if (g_ui.prof_sel >= g_ui.prof_n && g_ui.prof_sel > 0)
                g_ui.prof_sel--;
        }
        break;
    case 'n':
    case 'N': {
        char name[32];
        profile_gen_name(name, sizeof(name));
        tin_set(&g_ui.prof_name, name);
        g_ui.prof_typing = true;
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
        g_ui.prof_typing = true;
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
