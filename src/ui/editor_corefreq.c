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
 * per thread id in cf_staged[] and SURVIVE closing — they ride the
 * POWER Apply (single write door): the worker's write half is
 * cf_apply_staged (read-back verified in ctrl_set_cpu_max_mhz_core);
 * 'w'/Apply here just closes the overlay and spawns that apply.
 * POWER's global CPU-limit row writes first, then these — the specific
 * intent wins, nothing is overwritten.
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

static int cf_eff(int id)
{
    if (id < 0 || id >= HW_CPU_MAX)
        return 0;
    return g_ui.cf_staged[id] > 0 ? g_ui.cf_staged[id]
                                  : g_ui.hw->cpu_mhz_core[id];
}

static void cf_revert(void)
{
    memset(g_ui.cf_staged, 0, sizeof(g_ui.cf_staged));
}

/* POWER's revert drops core staging too — one pool, one revert */
void cf_reset_staging(void)
{
    cf_revert();
    pw_recompute_dirty();
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
    /* 0 = never staged/seeded (app start): NOT a pending write —
     * cf_staged is zero-initialised and the editor seeds on open */
    if (g_ui.cf_staged[a] != 0 && g_ui.cf_staged[a] != hw->cpu_mhz_core[a])
        return true;
    return b >= 0 && g_ui.cf_staged[b] != 0 &&
           g_ui.cf_staged[b] != hw->cpu_mhz_core[b];
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
                return cf_eff(cf_core_a(i));
        return hw->cpu_mhz_max;
    }
    return cf_eff(cf_core_a(k));
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

int cf_staged_n(void)
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
            if (g_ui.cf_staged[id] != 0 &&
                g_ui.cf_staged[id] != hw->cpu_mhz_core[id])
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
                cf_eff(cf_core_a(k)) < hw->cpu_mhz_max)
                n++;
    } else {
        for (int c = 0; c < hw->cpu_n && c < HW_CPU_MAX; c++) {
            int id = hw->cpu_ids[c];
            if (hw->cpu_mhz_max > 0 && cf_eff(id) < hw->cpu_mhz_max)
                n++;
        }
    }
    return n;
}

/* Capture the per-core write set BEFORE any other POWER write runs:
 * "staged != live" is the user's explicit intent only against the
 * baseline at apply START — the global CPU-limit write legitimately
 * moves hw->cpu_mhz_core underneath, and a core whose staging merely
 * echoes the old baseline must not be "restored" over the new value
 * (live-caught: global 2200 + CCD1 2000 ended with CCD2 back at the
 * stale 2401). */
void cf_capture_staged(hw_state_t *hw, int ids[HW_CPU_MAX],
                       int vals[HW_CPU_MAX], int *n_out)
{
    int n = 0;
    if (cf_core_mode()) {
        for (int k = 0; k < hw->core_n && n + 1 < HW_CPU_MAX; k++) {
            if (!cf_scope_diff(k))
                continue;
            /* the cpufreq policy is per thread — write both siblings;
             * when the kernel propagates anyway the second write is an
             * idempotent same-value write */
            ids[n] = cf_core_a(k);
            vals[n] = g_ui.cf_staged[cf_core_a(k)];
            n++;
            int b = cf_core_b(k);
            if (b >= 0) {
                ids[n] = b;
                vals[n] = g_ui.cf_staged[b];
                n++;
            }
        }
    } else {
        for (int c = 0; c < hw->cpu_n && c < HW_CPU_MAX; c++) {
            int id = hw->cpu_ids[c];
            if (g_ui.cf_staged[id] != 0 &&
                g_ui.cf_staged[id] != hw->cpu_mhz_core[id]) {
                ids[n] = id;
                vals[n] = g_ui.cf_staged[id];
                n++;
            }
        }
    }
    *n_out = n;
}

/* the WRITE half, called from the POWER apply worker over the captured
 * set; read-back verified inside ctrl_set_cpu_max_mhz_core. Touches hw
 * + the write layer only (reads of g_ui are gated-while-busy safe) */
int cf_apply_list(hw_state_t *hw, const int ids[HW_CPU_MAX],
                  const int vals[HW_CPU_MAX], int n, int *fails_out)
{
    int wrote = 0, fails = 0;
    for (int i = 0; i < n; i++) {
        if (ctrl_set_cpu_max_mhz_core(hw, ids[i], vals[i]) == 0)
            wrote++;
        else
            fails++;
    }
    if (wrote || fails)
        ut_log("core limits: %d written · %s", wrote,
               fails ? "FAILED entries above" : "verified");
    *fails_out = fails;
    return wrote;
}

/* ---- open / draw --------------------------------------------------------- */

void editor_corefreq_open(void)
{
    g_ui.cf_input_active = 0;
    if (g_ui.cf_sel >= cf_entries())
        g_ui.cf_sel = 0;
    g_ui.pw_in_cores = true;
    pw_recompute_dirty();
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
            cf_eff(cf_core_a(i)) < hw->cpu_mhz_max)
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
    int x = r->x, w = r->w;
    if (w < 16 || r->h < 4)
        return;

    bool foc = g_ui.pw_in_cores;
    char hdr[80];
    int ntot = cf_core_mode() ? hw->core_n : hw->cpu_n;
    int ncap = cf_capped_count();
    int nstg = cf_staged_n();
    if (nstg > 0)
        snprintf(hdr, sizeof(hdr), "CORE LIMITS · %d/%d capped · %d staged",
                 ncap, ntot, nstg);
    else if (ncap > 0)
        snprintf(hdr, sizeof(hdr), "CORE LIMITS · %d/%d capped", ncap, ntot);
    else
        snprintf(hdr, sizeof(hdr), "CORE LIMITS (c)");
    ui_putln(n, x, r->y, w, hdr, foc ? pal->accent : pal->muted, true);
    {
        int hw_cells = ut_cells(hdr) + 1;
        if (w - hw_cells > 2)
            ui_hline(n, x + hw_cells, r->y, w - hw_cells,
                     foc ? pal->accent : pal->border);
    }

    int y0 = r->y + 1;
    if (g_ui.cf_input_active) {
        char disp[68], line[144];
        tin_format(&g_ui.cf_input, disp, sizeof(disp), true);
        if (cf_core_mode()) {
            int g = 0;
            int k = cf_entry_at(g_ui.cf_sel, &g);
            char lab[16];
            if (k == -1)
                snprintf(lab, sizeof(lab), "%s", cf_group_label(g));
            else
                cf_core_label(k, lab, sizeof(lab));
            snprintf(line, sizeof(line), "edit %s MHz: %s  (Enter ok · Esc cancel)",
                     lab, disp);
        } else {
            int id = (g_ui.cf_sel >= 0 && g_ui.cf_sel < hw->cpu_n)
                         ? hw->cpu_ids[g_ui.cf_sel] : 0;
            snprintf(line, sizeof(line), "edit cpu %d MHz: %s  (Enter ok · Esc cancel)",
                     id, disp);
        }
        ui_putln(n, x, y0, w, line, pal->accent, true);
        y0++;
    }

    int ymax = r->y + r->h - 1;
    int rows = ymax - y0;
    if (rows < 1)
        rows = 1;

    if (!cf_core_mode()) {
        int cellw = 12;
        int cols = w / cellw;
        if (cols < 1)
            cols = 1;
        for (int i = 0; i < hw->cpu_n && i / cols < rows; i++) {
            int id = hw->cpu_ids[i];
            int cx = x + (i % cols) * cellw;
            int cy = y0 + i / cols;
            bool sel = foc && (i == g_ui.cf_sel);
            int val = cf_eff(id);
            bool capped = hw->cpu_mhz_max > 0 && val < hw->cpu_mhz_max;
            bool staged = g_ui.cf_staged[id] != 0 &&
                          g_ui.cf_staged[id] != hw->cpu_mhz_core[id];
            char cell[24];
            snprintf(cell, sizeof(cell), "%sc%02d %4d%s",
                     sel ? "▸" : " ", id, val,
                     staged ? "●" : " ");
            uint32_t fg = sel ? pal->accent
                        : staged ? pal->warn
                        : capped ? pal->accent : pal->text;
            ncplane_set_channels(n, ui_ch(fg, sel));
            ncplane_set_styles(n, sel ? NCSTYLE_BOLD : NCSTYLE_NONE);
            ncplane_putstr_yx(n, cy, cx, cell);
            ncplane_set_styles(n, NCSTYLE_NONE);
            tgt_register(cx, cy, cellw, 1,
                         TGT(TGT_PANEL_COREFREQ, ACT_CF_CELL + i));
        }
    } else {
        int cellw = 16;
        int colw = w / (hw->ccd_n > 0 ? hw->ccd_n : 1);
        if (colw < cellw)
            colw = cellw;
        int e = 0;
        for (int g = 0; g < hw->ccd_n; g++) {
            int cx = x + g * colw;
            int y = y0;
            if (hw->ccd_n > 1) {
                if (y < ymax && cx < x + w) {
                    char lab[40];
                    int lw = colw - 1;
                    if (lw > 22) lw = 22;
                    if (cx + lw > x + w) lw = x + w - cx;
                    bool hsel = foc && (g_ui.cf_sel == e);
                    snprintf(lab, sizeof(lab), "%s%s %d/%d capped",
                             hsel ? "▸" : " ", cf_group_label(g),
                             cf_group_capped(g), cf_group_cores(g));
                    ui_trunc(lab, lw);
                    ncplane_set_channels(n, ui_ch(hsel ? pal->accent : pal->muted, hsel));
                    ncplane_set_styles(n, (hsel || true) ? NCSTYLE_BOLD : NCSTYLE_NONE);
                    ncplane_putstr_yx(n, y, cx, lab);
                    ncplane_set_styles(n, NCSTYLE_NONE);
                    tgt_register(cx, y, lw, 1,
                                 TGT(TGT_PANEL_COREFREQ, ACT_CF_CELL + e));
                }
                e++;
                y++;
            }
            for (int k = 0; k < hw->core_n; k++) {
                if (hw->core_ccd[k] != g)
                    continue;
                if (y < ymax && cx < x + w) {
                    int a = cf_core_a(k);
                    int val = cf_eff(a);
                    bool sel = foc && (g_ui.cf_sel == e);
                    bool capped = hw->cpu_mhz_max > 0 && val < hw->cpu_mhz_max;
                    bool staged = cf_scope_diff(k);
                    char cell[40], lab[16];
                    cf_core_label(k, lab, sizeof(lab));
                    snprintf(cell, sizeof(cell), "%s%-7s %4d%s",
                             sel ? "▸" : " ", lab, val,
                             staged ? "●" : " ");
                    int cw = cellw;
                    if (cx + cw > x + w) cw = x + w - cx;
                    ui_trunc(cell, cw);
                    uint32_t fg = sel ? pal->accent
                                : staged ? pal->warn
                                : capped ? pal->accent : pal->text;
                    ncplane_set_channels(n, ui_ch(fg, sel));
                    ncplane_set_styles(n, sel ? NCSTYLE_BOLD : NCSTYLE_NONE);
                    ncplane_putstr_yx(n, y, cx, cell);
                    ncplane_set_styles(n, NCSTYLE_NONE);
                    tgt_register(cx, y, cw, 1,
                                 TGT(TGT_PANEL_COREFREQ, ACT_CF_CELL + e));
                }
                e++;
                y++;
            }
        }
    }

    if (r->h >= 5) {
        const char *foot = foc
            ? "h/l ±100 · e exact · a CCD · A all · o max · Esc power"
            : "press c or click a core to edit per-core ceilings";
        ui_putln(n, x, r->y + r->h - 1, w, foot, pal->muted, false);
    }
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
        if ((key >= '0' && key <= '9') || tin_is_edit_key(key))
            tin_key(&g_ui.cf_input, key);
        return;
    }

    if (n == 0) {
        if (key == NCKEY_ESC || key == 'c' || key == 'C')
            g_ui.pw_in_cores = false;
        return;
    }
    if (g_ui.cf_sel >= n)
        g_ui.cf_sel = 0;

    if (cf_core_mode()) {
        int g = 0;
        int k = cf_entry_at(g_ui.cf_sel, &g);
        switch (key) {
        case NCKEY_ESC:
        case 'c':
        case 'C':
            g_ui.pw_in_cores = false;
            return;
        case 'j': case NCKEY_DOWN:
            g_ui.cf_sel = (g_ui.cf_sel + 1) % n;
            return;
        case 'k': case NCKEY_UP:
            g_ui.cf_sel = (g_ui.cf_sel + n - 1) % n;
            return;
        case 'h': case NCKEY_LEFT:
            cf_sel_apply(cf_clamp(cf_sel_base() - 100));
            pw_recompute_dirty();
            return;
        case 'l': case NCKEY_RIGHT:
            cf_sel_apply(cf_clamp(cf_sel_base() + 100));
            pw_recompute_dirty();
            return;
        case 'e': case 'E':
        case 't': case 'T':
            g_ui.cf_input_active = 1;
            tin_clear(&g_ui.cf_input);
            return;
        case 'a': case 'A': {
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
        case 'o': case 'O':
            cf_sel_apply(hw->cpu_mhz_max);
            pw_recompute_dirty();
            return;
        case 'r': case 'R':
            cf_revert();
            pw_recompute_dirty();
            return;
        case 'w': case 'W':
        case NCKEY_ENTER: case '\r': case '\n': case ' ':
            g_ui.pw_in_cores = false;
            ui_spawn_apply(APPLY_POWER, "power settings", "");
            return;
        default:
            return;
        }
    }

    int id = hw->cpu_ids[g_ui.cf_sel];
    switch (key) {
    case NCKEY_ESC:
    case 'c':
    case 'C':
        g_ui.pw_in_cores = false;
        return;
    case 'j': case NCKEY_DOWN:
        g_ui.cf_sel = (g_ui.cf_sel + 1) % n;
        return;
    case 'k': case NCKEY_UP:
        g_ui.cf_sel = (g_ui.cf_sel + n - 1) % n;
        return;
    case 'h': case NCKEY_LEFT:
        g_ui.cf_staged[id] = cf_clamp(cf_eff(id) - 100);
        pw_recompute_dirty();
        return;
    case 'l': case NCKEY_RIGHT:
        g_ui.cf_staged[id] = cf_clamp(cf_eff(id) + 100);
        pw_recompute_dirty();
        return;
    case 'e': case 'E':
    case 't': case 'T':
        g_ui.cf_input_active = 1;
        tin_clear(&g_ui.cf_input);
        return;
    case 'a': case 'A': {
        int v = cf_eff(id);
        for (int c = 0; c < hw->cpu_n && c < HW_CPU_MAX; c++)
            g_ui.cf_staged[hw->cpu_ids[c]] = v;
        pw_recompute_dirty();
        return;
    }
    case 'o': case 'O':
        g_ui.cf_staged[id] = hw->cpu_mhz_max;
        pw_recompute_dirty();
        return;
    case 'r': case 'R':
        cf_revert();
        pw_recompute_dirty();
        return;
    case 'w': case 'W':
    case NCKEY_ENTER: case '\r': case '\n': case ' ':
        g_ui.pw_in_cores = false;
        ui_spawn_apply(APPLY_POWER, "power settings", "");
        return;
    default:
        return;
    }
}

void editor_corefreq_act(int id_)
{
    int n = cf_entries();
    if (id_ >= ACT_CF_CELL && id_ < ACT_CF_CELL + HW_CPU_MAX) {
        int e = id_ - ACT_CF_CELL;
        if (e >= n)
            return;
        if (e == g_ui.cf_sel) {
            if (cf_core_mode()) {
                cf_sel_apply(cf_clamp(cf_sel_base() + 100));
            } else {
                int cid = g_ui.hw->cpu_ids[e];
                g_ui.cf_staged[cid] = cf_clamp(cf_eff(cid) + 100);
            }
            pw_recompute_dirty();
        } else {
            g_ui.cf_sel = e;
        }
        return;
    }
    switch (id_) {
    case ACT_CF_WRITE:
        g_ui.pw_in_cores = false;
        ui_spawn_apply(APPLY_POWER, "power settings", "");
        break;
    case ACT_CF_REVERT: cf_revert(); break;
    default:
        break;
    }
}
