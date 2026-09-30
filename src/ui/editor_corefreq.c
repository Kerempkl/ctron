#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ui_internal.h"
#include "../control.h"
#include "../util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Per-core frequency editor overlay (daeboard pattern): a grid of
 * cNN cells staged in memory; w writes only the changed cores through
 * ctrl_set_cpu_max_mhz_core (read-back verified there). Closing keeps
 * staged values in memory; POWER's all-cores apply overwrites them. */

enum {
    ACT_CF_CELL = 1,      /* + core index */
    ACT_CF_WRITE = 300,  /* cell ids reach 1+HW_CPU_MAX; stay clear */
    ACT_CF_REVERT,
};

static int cf_n(void)
{
    int n = g_ui.hw->cpu_n;
    return n > 0 && n <= HW_CPU_MAX ? n : 0;
}

static int cf_clamp(int v)
{
    hw_state_t *hw = g_ui.hw;
    if (hw->cpu_mhz_max > 0)
        v = ut_clamp_i(v, hw->cpu_mhz_min, hw->cpu_mhz_max);
    return v;
}

static void cf_revert(void)
{
    hw_state_t *hw = g_ui.hw;
    for (int i = 0; i < cf_n(); i++)
        g_ui.cf_staged[i] = hw->cpu_mhz_core[i];
}

static int cf_staged_count(void)
{
    hw_state_t *hw = g_ui.hw;
    int n = 0;
    for (int i = 0; i < cf_n(); i++)
        if (g_ui.cf_staged[i] != hw->cpu_mhz_core[i])
            n++;
    return n;
}

static int cf_capped_count(void)
{
    hw_state_t *hw = g_ui.hw;
    int n = 0;
    for (int i = 0; i < cf_n(); i++)
        if (hw->cpu_mhz_max > 0 && g_ui.cf_staged[i] < hw->cpu_mhz_max)
            n++;
    return n;
}

static void cf_write(void)
{
    hw_state_t *hw = g_ui.hw;
    int wrote = 0, fails = 0;
    ui_flash("applying core limits...");
    for (int i = 0; i < cf_n(); i++) {
        if (g_ui.cf_staged[i] != hw->cpu_mhz_core[i]) {
            if (ctrl_set_cpu_max_mhz_core(hw, i, g_ui.cf_staged[i]) == 0)
                wrote++;
            else
                fails++;
        }
    }
    if (wrote == 0 && fails == 0)
        ut_log("core limits: nothing staged");
    else if (fails)
        ut_log("core limits: %d written · %d FAILED", wrote, fails);
    else
        ut_log("core limits: %d written · verified", wrote);
}

void editor_corefreq_open(void)
{
    cf_revert();
    g_ui.cf_input_active = 0;
    if (g_ui.cf_sel >= cf_n())
        g_ui.cf_sel = 0;
    g_ui.cf_overlay = true;
}

void editor_corefreq_draw(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    hw_state_t *hw = g_ui.hw;
    int x = r->x + 2, w = r->w - 4;

    ui_window(n, r, "CORE LIMITS");

    if (g_ui.cf_input_active) {
        char line[144];
        snprintf(line, sizeof(line), "cpu %d MHz: %s_ · Enter stages · Esc cancels",
                 g_ui.cf_sel, g_ui.cf_input.buf);
        ui_putln(n, x, r->y + 1, w, line, pal->accent, true);
    } else {
        ui_putln(n, x, r->y + 1, w,
                 "h/l ±100 · j/k select · t value · a all · o max · w write · r revert · Esc close",
                 pal->muted, false);
    }

    int cellw = 11;
    int cols = w / cellw;
    if (cols < 1)
        cols = 1;
    int rows = r->h - 6;
    int y0 = r->y + 3;

    int nn = cf_n();
    for (int i = 0; i < nn && i / cols < rows; i++) {
        int cx = x + (i % cols) * cellw;
        int cy = y0 + i / cols;
        bool sel = (i == g_ui.cf_sel);
        bool capped = hw->cpu_mhz_max > 0 && g_ui.cf_staged[i] < hw->cpu_mhz_max;
        bool staged = g_ui.cf_staged[i] != hw->cpu_mhz_core[i];
        char cell[24];
        snprintf(cell, sizeof(cell), "%sc%02d %4d%s",
                 sel ? "▸" : " ", i, g_ui.cf_staged[i], staged ? "●" : " ");
        ui_putln(n, cx, cy, cellw + 1, cell,
                 capped ? pal->accent : (staged ? pal->accent2 : pal->text),
                 sel);
        tgt_register(cx, cy, cellw, 1,
                     TGT(TGT_PANEL_COREFREQ, ACT_CF_CELL + i));
    }

    int by = r->y + r->h - 2;
    char st[96];
    snprintf(st, sizeof(st), "%d/%d capped · %d staged — POWER all-apply overwrites per-core",
             cf_capped_count(), nn, cf_staged_count());
    ui_putln(n, x, by - 1, w, st, pal->muted, false);
    ui_btn(n, x, by, " Write ", cf_staged_count() > 0, false,
           TGT(TGT_PANEL_COREFREQ, ACT_CF_WRITE));
    ui_btn(n, x + 10, by, " Revert ", false, false,
           TGT(TGT_PANEL_COREFREQ, ACT_CF_REVERT));
}

void editor_corefreq_key(uint32_t key)
{
    hw_state_t *hw = g_ui.hw;
    int n = cf_n();

    if (g_ui.cf_input_active) {
        if (key == NCKEY_ESC) {
            g_ui.cf_input_active = 0;
            return;
        }
        if (key == NCKEY_ENTER || key == '\r' || key == '\n') {
            if (g_ui.cf_input.buf[0])
                g_ui.cf_staged[g_ui.cf_sel] =
                    cf_clamp(atoi(g_ui.cf_input.buf));
            g_ui.cf_input_active = 0;
            return;
        }
        if ((key >= '0' && key <= '9') || key == NCKEY_BACKSPACE ||
            key == 127 || key == 8)
            tin_key(&g_ui.cf_input, key);
        return;
    }

    if (n == 0) {                    /* no cpufreq policies: just close */
        if (key == NCKEY_ESC || key == 'q' || key == 'Q')
            g_ui.cf_overlay = false;
        return;
    }
    if (g_ui.cf_sel >= n)
        g_ui.cf_sel = 0;

    switch (key) {
    case NCKEY_ESC:
    case 'q':
    case 'Q':
        g_ui.cf_overlay = false;
        return;
    case 'j': case NCKEY_DOWN:
        g_ui.cf_sel = (g_ui.cf_sel + 1) % n;
        return;
    case 'k': case NCKEY_UP:
        g_ui.cf_sel = (g_ui.cf_sel + n - 1) % n;
        return;
    case 'h': case NCKEY_LEFT:
        g_ui.cf_staged[g_ui.cf_sel] = cf_clamp(g_ui.cf_staged[g_ui.cf_sel] - 100);
        return;
    case 'l': case NCKEY_RIGHT:
        g_ui.cf_staged[g_ui.cf_sel] = cf_clamp(g_ui.cf_staged[g_ui.cf_sel] + 100);
        return;
    case 't': case 'T':
        g_ui.cf_input_active = 1;
        tin_clear(&g_ui.cf_input);
        return;
    case 'a': case 'A': {           /* selected value to every core */
        int v = g_ui.cf_staged[g_ui.cf_sel];
        for (int i = 0; i < n; i++)
            g_ui.cf_staged[i] = v;
        return;
    }
    case 'o': case 'O':             /* selected back to the cpuinfo max */
        g_ui.cf_staged[g_ui.cf_sel] = hw->cpu_mhz_max;
        return;
    case 'r': case 'R':
        cf_revert();
        return;
    case 'w': case 'W':
    case NCKEY_ENTER: case '\r': case '\n': case ' ':
        cf_write();
        return;
    default:
        return;
    }
}

void editor_corefreq_act(int id)
{
    int n = cf_n();
    if (id >= ACT_CF_CELL && id < ACT_CF_CELL + HW_CPU_MAX) {
        int i = id - ACT_CF_CELL;
        if (i < n) {
            if (i == g_ui.cf_sel)   /* second click: step the value up */
                g_ui.cf_staged[i] = cf_clamp(g_ui.cf_staged[i] + 100);
            else
                g_ui.cf_sel = i;
        }
        return;
    }
    switch (id) {
    case ACT_CF_WRITE: cf_write(); break;
    case ACT_CF_REVERT: cf_revert(); break;
    default:
        break;
    }
}
