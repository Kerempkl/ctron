#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ui_internal.h"
#include "../control.h"
#include "../util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Per-core frequency editor overlay (daeboard pattern). Limits stage
 * per thread id in cf_staged[]; 'w' writes only the changed ones via
 * ctrl_set_cpu_max_mhz_core (read-back verified there). Closing keeps
 * staged values in memory; POWER's all-cores apply overwrites them.
 *
 * Two presentations:
 *  - CORE mode (topology known): physical cores as c0·16 cells under
 *    CCD headers (L3 groups). Editing a cell stages BOTH threads of
 *    the core — the kernel treats them as one physical core anyway.
 *    With a header selected, h/l/t/o/a operate on the whole CCD.
 *    'a' = apply the selected value to its CCD, 'A' = to every core.
 *  - CPU mode (topo_odd fallback): the per-thread grid over the real
 *    present ids, exactly as before the topology work. */

enum {
    ACT_CF_CELL = 1,      /* + entry (grid position) */
    ACT_CF_WRITE = 300,  /* cell ids reach 1+HW_CPU_MAX; stay clear */
    ACT_CF_REVERT,
};

/* ---- shared ------------------------------------------------------------ */

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
    for (int c = 0; c < hw->cpu_n && c < HW_CPU_MAX; c++) {
        int id = hw->cpu_ids[c];
        g_ui.cf_staged[id] = hw->cpu_mhz_core[id];
    }
}

/* ---- entry walk (navigation + mouse share one numbering) ---------------
 * CORE mode: per ccd group g — an optional header row (only when there
 * is more than one group), then that group's cores in core order.
 * CPU mode: entries are the cpu grid positions 0..cpu_n-1. */

static bool cf_core_mode(void)
{
    hw_state_t *hw = g_ui.hw;
    return !hw->topo_odd && hw->core_n > 0;
}

static int cf_entries(void)
{
    hw_state_t *hw = g_ui.hw;
    if (!cf_core_mode()) {
        int n = hw->cpu_n;
        return n > 0 && n <= HW_CPU_MAX ? n : 0;
    }
    int hdrs = hw->ccd_n > 1 ? hw->ccd_n : 0;
    return hdrs + hw->core_n;
}

/* entry -> core index; -1 = header row (*g_out = its group), -2 = bad */
static int cf_entry_at(int e, int *g_out)
{
    hw_state_t *hw = g_ui.hw;
    if (!cf_core_mode())
        return (e >= 0 && e < hw->cpu_n) ? e : -2;
    bool hdrs = hw->ccd_n > 1;
    int seen = 0;
    for (int g = 0; g < hw->ccd_n; g++) {
        if (hdrs) {
            if (e == seen) {
                if (g_out)
                    *g_out = g;
                return -1;
            }
            seen++;
        }
        for (int k = 0; k < hw->core_n; k++) {
            if (hw->core_ccd[k] != g)
                continue;
            if (e == seen) {
                if (g_out)
                    *g_out = g;
                return k;
            }
            seen++;
        }
    }
    return -2;
}

/* ---- CORE mode helpers -------------------------------------------------- */

static int cf_core_a(int k)
{
    return g_ui.hw->core_cpu[k];
}

static int cf_core_b(int k)
{
    return g_ui.hw->core_sib[k]; /* -1 = no second thread */
}

/* stage v to both threads of core k */
static void cf_scope_stage(int k, int v)
{
    g_ui.cf_staged[cf_core_a(k)] = v;
    int b = cf_core_b(k);
    if (b >= 0)
        g_ui.cf_staged[b] = v;
}

static bool cf_scope_diff(int k)
{
    hw_state_t *hw = g_ui.hw;
    int a = cf_core_a(k), b = cf_core_b(k);
    if (g_ui.cf_staged[a] != hw->cpu_mhz_core[a])
        return true;
    return b >= 0 && g_ui.cf_staged[b] != hw->cpu_mhz_core[b];
}

/* value of the current selection; a header borrows its first core */
static int cf_sel_base(void)
{
    hw_state_t *hw = g_ui.hw;
    int g = 0;
    int k = cf_entry_at(g_ui.cf_sel, &g);
    if (k == -1) {
        for (int i = 0; i < hw->core_n; i++)
            if (hw->core_ccd[i] == g)
                return g_ui.cf_staged[cf_core_a(i)];
        return hw->cpu_mhz_max;
    }
    return g_ui.cf_staged[cf_core_a(k)];
}

/* stage v to the selection's scope (one core, or a whole CCD header) */
static void cf_sel_apply(int v)
{
    hw_state_t *hw = g_ui.hw;
    int g = 0;
    int k = cf_entry_at(g_ui.cf_sel, &g);
    if (k == -1) {
        for (int i = 0; i < hw->core_n; i++)
            if (hw->core_ccd[i] == g)
                cf_scope_stage(i, v);
    } else {
        cf_scope_stage(k, v);
    }
}

/* stage v to every core of ccd group g */
static void cf_group_stage(int g, int v)
{
    hw_state_t *hw = g_ui.hw;
    for (int i = 0; i < hw->core_n; i++)
        if (hw->core_ccd[i] == g)
            cf_scope_stage(i, v);
}

/* ---- counts / write ------------------------------------------------------ */

static int cf_staged_count(void)
{
    hw_state_t *hw = g_ui.hw;
    int n = 0;
    if (cf_core_mode()) {
        for (int k = 0; k < hw->core_n; k++)
            if (cf_scope_diff(k))
                n++;
    } else {
        for (int c = 0; c < hw->cpu_n && c < HW_CPU_MAX; c++) {
            int id = hw->cpu_ids[c];
            if (g_ui.cf_staged[id] != hw->cpu_mhz_core[id])
                n++;
        }
    }
    return n;
}

static int cf_capped_count(void)
{
    hw_state_t *hw = g_ui.hw;
    int n = 0;
    if (cf_core_mode()) {
        for (int k = 0; k < hw->core_n; k++)
            if (hw->cpu_mhz_max > 0 &&
                g_ui.cf_staged[cf_core_a(k)] < hw->cpu_mhz_max)
                n++;
    } else {
        for (int c = 0; c < hw->cpu_n && c < HW_CPU_MAX; c++) {
            int id = hw->cpu_ids[c];
            if (hw->cpu_mhz_max > 0 && g_ui.cf_staged[id] < hw->cpu_mhz_max)
                n++;
        }
    }
    return n;
}

static void cf_write(void)
{
    hw_state_t *hw = g_ui.hw;
    int wrote = 0, fails = 0;
    ui_flash("applying core limits...");
    if (cf_core_mode()) {
        for (int k = 0; k < hw->core_n; k++) {
            if (!cf_scope_diff(k))
                continue;
            /* the cpufreq policy is per thread — write both siblings;
             * when the kernel propagates anyway the second write is an
             * idempotent same-value write */
            if (ctrl_set_cpu_max_mhz_core(hw, cf_core_a(k),
                                          g_ui.cf_staged[cf_core_a(k)]) == 0)
                wrote++;
            else
                fails++;
            int b = cf_core_b(k);
            if (b >= 0) {
                if (ctrl_set_cpu_max_mhz_core(hw, b, g_ui.cf_staged[b]) == 0)
                    wrote++;
                else
                    fails++;
            }
        }
    } else {
        for (int c = 0; c < hw->cpu_n && c < HW_CPU_MAX; c++) {
            int id = hw->cpu_ids[c];
            if (g_ui.cf_staged[id] != hw->cpu_mhz_core[id]) {
                if (ctrl_set_cpu_max_mhz_core(hw, id, g_ui.cf_staged[id]) == 0)
                    wrote++;
                else
                    fails++;
            }
        }
    }
    if (wrote == 0 && fails == 0)
        ut_log("core limits: nothing staged");
    else if (fails)
        ut_log("core limits: %d written · %d FAILED", wrote, fails);
    else
        ut_log("core limits: %d written · verified", wrote);
}

/* ---- open / draw --------------------------------------------------------- */

void editor_corefreq_open(void)
{
    cf_revert();
    g_ui.cf_input_active = 0;
    if (g_ui.cf_sel >= cf_entries())
        g_ui.cf_sel = 0;
    g_ui.cf_overlay = true;
}

static const char *cf_group_label(int g)
{
    static char lab[16];
    const char *cpu = g_ui.hw->cpu;
    bool amd = strstr(cpu, "Ryzen") || strstr(cpu, "AMD");
    snprintf(lab, sizeof lab, amd ? "CCD%d" : "L3-%d", g + 1);
    return lab;
}

/* "c0·16" / "c8" — label of core k */
static void cf_core_label(int k, char *out, size_t n)
{
    int b = cf_core_b(k);
    if (b >= 0)
        snprintf(out, n, "c%d·%d", cf_core_a(k), b);
    else
        snprintf(out, n, "c%d", cf_core_a(k));
}

/* cores of ccd group g below the cpuinfo max (for header labels) */
static int cf_group_capped(int g)
{
    hw_state_t *hw = g_ui.hw;
    int n = 0;
    for (int i = 0; i < hw->core_n; i++)
        if (hw->core_ccd[i] == g && hw->cpu_mhz_max > 0 &&
            g_ui.cf_staged[cf_core_a(i)] < hw->cpu_mhz_max)
            n++;
    return n;
}

static int cf_group_cores(int g)
{
    hw_state_t *hw = g_ui.hw;
    int n = 0;
    for (int i = 0; i < hw->core_n; i++)
        if (hw->core_ccd[i] == g)
            n++;
    return n;
}

void editor_corefreq_draw(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    hw_state_t *hw = g_ui.hw;
    int x = r->x + 2, w = r->w - 4;

    ui_window(n, r, "CORE LIMITS");

    if (g_ui.cf_input_active) {
        char line[144];
        if (cf_core_mode()) {
            int g = 0;
            int k = cf_entry_at(g_ui.cf_sel, &g);
            char lab[16];
            if (k == -1)
                snprintf(lab, sizeof lab, "%s", cf_group_label(g));
            else
                cf_core_label(k, lab, sizeof lab);
            snprintf(line, sizeof(line), "%s MHz: %s_ · Enter stages · Esc cancels",
                     lab, g_ui.cf_input.buf);
        } else {
            int id = (g_ui.cf_sel >= 0 && g_ui.cf_sel < hw->cpu_n)
                         ? hw->cpu_ids[g_ui.cf_sel] : 0;
            snprintf(line, sizeof(line), "cpu %d MHz: %s_ · Enter stages · Esc cancels",
                     id, g_ui.cf_input.buf);
        }
        ui_putln(n, x, r->y + 1, w, line, pal->accent, true);
    } else if (cf_core_mode()) {
        ui_putln(n, x, r->y + 1, w,
                 "h/l ±100 · j/k select · t value · a CCD · A all · o max · w write · r revert · Esc close",
                 pal->muted, false);
    } else {
        ui_putln(n, x, r->y + 1, w,
                 "h/l ±100 · j/k select · t value · a all · o max · w write · r revert · Esc close",
                 pal->muted, false);
    }

    int rows = r->h - 6;
    int y0 = r->y + 3;
    int ymax = y0 + (rows > 0 ? rows : 0);

    if (!cf_core_mode()) {
        /* CPU fallback: the historical per-thread grid */
        int cellw = 11;
        int cols = w / cellw;
        if (cols < 1)
            cols = 1;
        for (int i = 0; i < hw->cpu_n && i / cols < rows; i++) {
            int id = hw->cpu_ids[i];
            int cx = x + (i % cols) * cellw;
            int cy = y0 + i / cols;
            bool sel = (i == g_ui.cf_sel);
            bool capped = hw->cpu_mhz_max > 0 &&
                          g_ui.cf_staged[id] < hw->cpu_mhz_max;
            bool staged = g_ui.cf_staged[id] != hw->cpu_mhz_core[id];
            char cell[24];
            snprintf(cell, sizeof(cell), "%sc%02d %4d%s",
                     sel ? "▸" : " ", id, g_ui.cf_staged[id],
                     staged ? "●" : " ");
            ui_putln(n, cx, cy, cellw + 1, cell,
                     capped ? pal->accent : (staged ? pal->accent2 : pal->text),
                     sel);
            tgt_register(cx, cy, cellw, 1,
                         TGT(TGT_PANEL_COREFREQ, ACT_CF_CELL + i));
        }
    } else {
        /* CORE mode: CCD blocks with paired-thread cells */
        int cellw = 12;
        int cols = w / cellw;
        if (cols < 1)
            cols = 1;
        int col = 0, y = y0;
        int e = 0;
        for (int g = 0; g < hw->ccd_n; g++) {
            if (hw->ccd_n > 1) {
                if (y < ymax) {
                    char lab[40];
                    snprintf(lab, sizeof(lab), "%s%s %d/%d capped",
                             g_ui.cf_sel == e ? "▸" : " ", cf_group_label(g),
                             cf_group_capped(g), cf_group_cores(g));
                    bool hsel = (g_ui.cf_sel == e);
                    ui_putln(n, x, y, w, lab,
                             hsel ? pal->accent : pal->muted, hsel);
                    tgt_register(x, y, w, 1,
                                 TGT(TGT_PANEL_COREFREQ, ACT_CF_CELL + e));
                }
                e++;
                y++;
                col = 0;
            }
            for (int k = 0; k < hw->core_n; k++) {
                if (hw->core_ccd[k] != g)
                    continue;
                if (y < ymax) {
                    int a = cf_core_a(k);
                    int cx = x + col * cellw;
                    bool sel = (g_ui.cf_sel == e);
                    bool capped = hw->cpu_mhz_max > 0 &&
                                  g_ui.cf_staged[a] < hw->cpu_mhz_max;
                    bool staged = cf_scope_diff(k);
                    char cell[40], lab[16];
                    cf_core_label(k, lab, sizeof lab);
                    snprintf(cell, sizeof(cell), "%s%-7s %4d%s",
                             sel ? "▸" : " ", lab, g_ui.cf_staged[a],
                             staged ? "●" : " ");
                    ui_putln(n, cx, y, cellw + 1, cell,
                             capped ? pal->accent
                                    : (staged ? pal->accent2 : pal->text),
                             sel);
                    tgt_register(cx, y, cellw, 1,
                                 TGT(TGT_PANEL_COREFREQ, ACT_CF_CELL + e));
                }
                e++;
                if (++col >= cols) {
                    col = 0;
                    y++;
                }
            }
            if (col != 0) { /* finish the group's row */
                col = 0;
                y++;
            }
        }
    }

    int by = r->y + r->h - 2;
    char st[96];
    snprintf(st, sizeof(st), "%d/%d capped · %d staged — POWER all-apply overwrites per-core",
             cf_capped_count(),
             cf_core_mode() ? hw->core_n : hw->cpu_n,
             cf_staged_count());
    ui_putln(n, x, by - 1, w, st, pal->muted, false);
    ui_btn(n, x, by, " Write ", cf_staged_count() > 0, false,
           TGT(TGT_PANEL_COREFREQ, ACT_CF_WRITE));
    ui_btn(n, x + 10, by, " Revert ", false, false,
           TGT(TGT_PANEL_COREFREQ, ACT_CF_REVERT));
}

/* ---- keys ---------------------------------------------------------------- */

void editor_corefreq_key(uint32_t key)
{
    hw_state_t *hw = g_ui.hw;
    int n = cf_entries();

    if (g_ui.cf_input_active) {
        if (key == NCKEY_ESC) {
            g_ui.cf_input_active = 0;
            return;
        }
        if (key == NCKEY_ENTER || key == '\r' || key == '\n') {
            if (g_ui.cf_input.buf[0]) {
                if (cf_core_mode())
                    cf_sel_apply(cf_clamp(atoi(g_ui.cf_input.buf)));
                else if (g_ui.cf_sel >= 0 && g_ui.cf_sel < hw->cpu_n)
                    g_ui.cf_staged[hw->cpu_ids[g_ui.cf_sel]] =
                        cf_clamp(atoi(g_ui.cf_input.buf));
            }
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

    if (cf_core_mode()) {
        int g = 0;
        int k = cf_entry_at(g_ui.cf_sel, &g);
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
            cf_sel_apply(cf_clamp(cf_sel_base() - 100));
            return;
        case 'l': case NCKEY_RIGHT:
            cf_sel_apply(cf_clamp(cf_sel_base() + 100));
            return;
        case 't': case 'T':
            g_ui.cf_input_active = 1;
            tin_clear(&g_ui.cf_input);
            return;
        case 'a': case 'A': {
            /* 'a' = the selected value across its CCD, 'A' = everywhere */
            int v = cf_clamp(cf_sel_base());
            if (key == 'a') {
                cf_group_stage(g, v);
            } else {
                for (int i = 0; i < hw->core_n; i++)
                    cf_scope_stage(i, v);
            }
            (void)k;
            return;
        }
        case 'o': case 'O':           /* selection back to the cpuinfo max */
            cf_sel_apply(hw->cpu_mhz_max);
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

    /* CPU fallback: historical key map over real ids */
    int id = hw->cpu_ids[g_ui.cf_sel];
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
        g_ui.cf_staged[id] = cf_clamp(g_ui.cf_staged[id] - 100);
        return;
    case 'l': case NCKEY_RIGHT:
        g_ui.cf_staged[id] = cf_clamp(g_ui.cf_staged[id] + 100);
        return;
    case 't': case 'T':
        g_ui.cf_input_active = 1;
        tin_clear(&g_ui.cf_input);
        return;
    case 'a': case 'A': {           /* selected value to every core */
        int v = g_ui.cf_staged[id];
        for (int c = 0; c < hw->cpu_n && c < HW_CPU_MAX; c++)
            g_ui.cf_staged[hw->cpu_ids[c]] = v;
        return;
    }
    case 'o': case 'O':
        g_ui.cf_staged[id] = hw->cpu_mhz_max;
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

/* ---- mouse --------------------------------------------------------------- */

void editor_corefreq_act(int id_)
{
    int n = cf_entries();
    if (id_ >= ACT_CF_CELL && id_ < ACT_CF_CELL + HW_CPU_MAX) {
        int e = id_ - ACT_CF_CELL;   /* entry index */
        if (e >= n)
            return;
        if (e == g_ui.cf_sel) {      /* second click: step the value up */
            if (cf_core_mode()) {
                cf_sel_apply(cf_clamp(cf_sel_base() + 100));
            } else {
                int cid = g_ui.hw->cpu_ids[e];
                g_ui.cf_staged[cid] = cf_clamp(g_ui.cf_staged[cid] + 100);
            }
        } else {
            g_ui.cf_sel = e;
        }
        return;
    }
    switch (id_) {
    case ACT_CF_WRITE: cf_write(); break;
    case ACT_CF_REVERT: cf_revert(); break;
    default:
        break;
    }
}
