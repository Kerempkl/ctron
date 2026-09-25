#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ui_internal.h"
#include "daeboard.h"
#include "../control.h"
#include "../util.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* Right-top workspace. The left column decides what is shown here via
 * ws_set_view(); workspace-local keys F/P/L/S/E switch views directly. */

enum {
    ACT_WS_TAB_FAN = 1,
    ACT_WS_TAB_POWER,
    ACT_WS_TAB_LIGHT,
    ACT_WS_TAB_SETTINGS,
    ACT_WS_TAB_HELP,
    /* power view buttons */
    ACT_WS_PW_APPLY = 15,
    ACT_WS_PW_REVERT,
    /* power view rows */
    ACT_WS_PW_BASE = 20,   /* + row index */
    /* light view rows */
    ACT_WS_LT_BASE = 40,   /* + row index */
};

static const char *const WS_NAMES[WSV_COUNT] = {
    "FAN CURVE", "POWER", "LIGHT", "HELP"
};

void ws_set_view(ws_view_t v)
{
    if (g_ui.ws_view == v)
        return;
    g_ui.ws_view = v;
    /* entering POWER: pick up external changes (CLI, asusctl) unless
     * the user has staged edits pending */
    if (v == WSV_POWER && !g_ui.pw_dirty) {
        hw_refresh_live(g_ui.hw);
        pw_sync_from_hw();
    }
}

/* Lowercase f/p/l/e yield to keys the active view binds (fan editor
 * 'p'/'l', POWER/LIGHT 'l'); uppercase always switches views. */
static bool view_binds_key(ws_view_t v, uint32_t key)
{
    switch (v) {
    case WSV_FAN:   return key == 'p' || key == 'l';
    case WSV_POWER: return key == 'l';
    case WSV_LIGHT: return key == 'l';
    default:        return false; /* help view binds none */
    }
}

/* ---- POWER view -------------------------------------------------------- */

/* Staged edits: h/l only mutate the pwv_* staging fields; nothing
 * reaches the hardware until Apply (Enter / w / Apply button). Revert
 * drops them. */

/* semantic colors for the apply toast (theme-independent) */
#define PW_OK_RGB  0x33FF66
#define PW_ERR_RGB 0xFF4D5E
#define PW_TOAST_MS 5000

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

enum {
    PW_PROFILE = 0,
    PW_EPP,
    PW_SPL,
    PW_SPPT,
    PW_FPPT,
    PW_PRESET_Q,
    PW_PRESET_B,
    PW_PRESET_P,
    PW_PPT_LIMITS,
    PW_NVBOOST,
    PW_NVTEMP,
    PW_PANEL_OD,
    PW_CPUBOOST,
    PW_CPUFREQ,
    PW_ROWS
};

static void pw_recompute_dirty(void)
{
    const hw_state_t *hw = g_ui.hw;
    g_ui.pw_dirty =
        g_ui.pwv_profile != (int)hw->profile ||
        g_ui.pwv_epp != (int)hw->epp ||
        (hw->ppt_spl > 0 && g_ui.pwv_spl != hw->ppt_spl) ||
        (hw->ppt_sppt > 0 && g_ui.pwv_sppt != hw->ppt_sppt) ||
        (hw->ppt_fppt > 0 && g_ui.pwv_fppt != hw->ppt_fppt) ||
        g_ui.pwv_ppt_off != hw->ppt_off ||
        (hw->nv_boost > 0 && g_ui.pwv_nvboost != hw->nv_boost) ||
        (hw->nv_temp > 0 && g_ui.pwv_nvtemp != hw->nv_temp) ||
        g_ui.pwv_panel_od != hw->panel_od ||
        g_ui.pwv_cpuboost != hw->cpu_boost ||
        (hw->cpu_mhz_limit > 0 && g_ui.pwv_mhz != hw->cpu_mhz_limit);
}

void pw_sync_from_hw(void)
{
    const hw_state_t *hw = g_ui.hw;
    /* unknown/stale reads (0) keep the current staged value: the defaults
     * on the very first sync, the written values right after an apply
     * whose read-back is still kernel-cache stale */
    g_ui.pwv_profile = (int)hw->profile;
    g_ui.pwv_epp = (int)hw->epp;
    g_ui.pwv_spl  = hw->ppt_spl  > 0 ? hw->ppt_spl
                  : (g_ui.pwv_spl  > 0 ? g_ui.pwv_spl  : 45);
    g_ui.pwv_sppt = hw->ppt_sppt > 0 ? hw->ppt_sppt
                  : (g_ui.pwv_sppt > 0 ? g_ui.pwv_sppt : 55);
    g_ui.pwv_fppt = hw->ppt_fppt > 0 ? hw->ppt_fppt
                  : (g_ui.pwv_fppt > 0 ? g_ui.pwv_fppt : 55);
    g_ui.pwv_nvboost = hw->nv_boost > 0 ? hw->nv_boost
                     : (g_ui.pwv_nvboost > 0 ? g_ui.pwv_nvboost : 5);
    g_ui.pwv_nvtemp = hw->nv_temp > 0 ? hw->nv_temp
                    : (g_ui.pwv_nvtemp > 0 ? g_ui.pwv_nvtemp : 75);
    g_ui.pwv_mhz = hw->cpu_mhz_limit > 0 ? hw->cpu_mhz_limit
                 : (g_ui.pwv_mhz > 0 ? g_ui.pwv_mhz : hw->cpu_mhz_max);
    g_ui.pwv_panel_od = hw->panel_od;
    g_ui.pwv_cpuboost = hw->cpu_boost;
    g_ui.pwv_ppt_off = hw->ppt_off;
    g_ui.pw_quit_warned = false;
    g_ui.pw_touched = 0;
    pw_recompute_dirty();
}

static void pw_stage_preset(int spl, int sppt, int fppt)
{
    int smin, smax, pmin, pmax, fmin, fmax;
    ctrl_ppt_limits(g_ui.hw, &smin, &smax, &pmin, &pmax, &fmin, &fmax);
    ctrl_ppt_order(&spl, &sppt, &fppt, smin, smax, pmin, pmax, fmin, fmax);
    g_ui.pwv_spl = spl;
    g_ui.pwv_sppt = sppt;
    g_ui.pwv_fppt = fppt;
    g_ui.pwv_ppt_off = false; /* staging watts implies limits back on */
    g_ui.pw_touched |= PW_T_PPT;
}

static void pw_nudge_row(int row, int dir)
{
    hw_state_t *hw = g_ui.hw;
    g_ui.pw_msg_ms = 0; /* new staging dismisses the apply toast */
    switch (row) {
    case PW_PROFILE:
        g_ui.pwv_profile = (g_ui.pwv_profile +
                            (dir > 0 ? 1 : HW_PROF_COUNT - 1)) % HW_PROF_COUNT;
        g_ui.pw_touched |= PW_T_PROFILE;
        break;
    case PW_EPP:
        g_ui.pwv_epp = (g_ui.pwv_epp +
                        (dir > 0 ? 1 : HW_EPP_COUNT - 1)) % HW_EPP_COUNT;
        g_ui.pw_touched |= PW_T_EPP;
        break;
    case PW_SPL: case PW_SPPT: case PW_FPPT: {
        int smin, smax, pmin, pmax, fmin, fmax;
        ctrl_ppt_limits(hw, &smin, &smax, &pmin, &pmax, &fmin, &fmax);
        if (row == PW_SPL)
            g_ui.pwv_spl += 5 * dir;
        else if (row == PW_SPPT)
            g_ui.pwv_sppt += 5 * dir;
        else
            g_ui.pwv_fppt += 5 * dir;
        g_ui.pwv_ppt_off = false;
        /* clamp + order exactly as the write path will, so the staged
         * triple is what Apply writes */
        ctrl_ppt_order(&g_ui.pwv_spl, &g_ui.pwv_sppt, &g_ui.pwv_fppt,
                       smin, smax, pmin, pmax, fmin, fmax);
        g_ui.pw_touched |= PW_T_PPT;
        break;
    }
    case PW_PRESET_Q: pw_stage_preset(45, 55, 55); break;
    case PW_PRESET_B: pw_stage_preset(60, 75, 75); break;
    case PW_PRESET_P: pw_stage_preset(80, 80, 80); break;
    case PW_PPT_LIMITS:
        if (!g_ui.pwv_ppt_off) {
            /* staging "removed" mirrors ctrl_ppt_off: it writes the
             * platform maxima, so stage exactly those */
            int smin, smax, pmin, pmax, fmin, fmax;
            ctrl_ppt_limits(hw, &smin, &smax, &pmin, &pmax, &fmin, &fmax);
            g_ui.pwv_spl = smax;
            g_ui.pwv_sppt = pmax;
            g_ui.pwv_fppt = fmax;
            g_ui.pwv_ppt_off = true;
        } else {
            g_ui.pwv_ppt_off = false;
        }
        g_ui.pw_touched |= PW_T_PPT_OFF;
        break;
    case PW_NVBOOST:
        g_ui.pwv_nvboost = ut_clamp_i(g_ui.pwv_nvboost + 5 * dir, 5, 25);
        g_ui.pw_touched |= PW_T_NVBOOST;
        break;
    case PW_NVTEMP:
        g_ui.pwv_nvtemp = ut_clamp_i(g_ui.pwv_nvtemp + dir, 75, 87);
        g_ui.pw_touched |= PW_T_NVTEMP;
        break;
    case PW_PANEL_OD: g_ui.pwv_panel_od = !g_ui.pwv_panel_od; break;
    case PW_CPUBOOST: g_ui.pwv_cpuboost = !g_ui.pwv_cpuboost; break;
    case PW_CPUFREQ: {
        int v = g_ui.pwv_mhz + 100 * dir;
        if (hw->cpu_mhz_max > 0)
            v = ut_clamp_i(v, hw->cpu_mhz_min, hw->cpu_mhz_max);
        g_ui.pwv_mhz = v;
        g_ui.pw_touched |= PW_T_CPUFREQ;
        break;
    }
    default:
        break;
    }
    pw_recompute_dirty();
}

/* append one summary entry, " · " separated; entries that do not fit
 * are skipped (caller adds an ellipsis) */
static void pw_app(char *out, size_t n, int *off, const char *entry)
{
    const char *sep = *off > 0 ? " · " : "";
    if (*off + (int)strlen(sep) + (int)strlen(entry) >= (int)n - 2)
        return;
    *off += snprintf(out + *off, n - (size_t)*off, "%s%s", sep, entry);
}

/* "what Apply will change", captured before the writes collapse the
 * staging back onto the live values */
static void pw_diff_summary(char *out, size_t n)
{
    const hw_state_t *hw = g_ui.hw;
    char e[48];
    int off = 0;
    out[0] = '\0';

    if (g_ui.pwv_profile != (int)hw->profile) {
        snprintf(e, sizeof(e), "profile %s",
                 hw_profile_name((hw_profile_t)g_ui.pwv_profile));
        pw_app(out, n, &off, e);
    }
    if (g_ui.pwv_epp != (int)hw->epp) {
        snprintf(e, sizeof(e), "EPP %s", hw_epp_name((hw_epp_t)g_ui.pwv_epp));
        pw_app(out, n, &off, e);
    }
    if (g_ui.pwv_ppt_off != hw->ppt_off) {
        pw_app(out, n, &off, g_ui.pwv_ppt_off ? "PPT limits off" : "PPT limits on");
    } else if (!g_ui.pwv_ppt_off && (g_ui.pw_touched & PW_T_PPT)) {
        if (hw->ppt_spl > 0 && g_ui.pwv_spl != hw->ppt_spl) {
            snprintf(e, sizeof(e), "SPL %d→%d W", hw->ppt_spl, g_ui.pwv_spl);
            pw_app(out, n, &off, e);
        } else if (hw->ppt_spl <= 0 && g_ui.pwv_spl > 0) {
            snprintf(e, sizeof(e), "SPL →%d W", g_ui.pwv_spl);
            pw_app(out, n, &off, e);
        }
        if (hw->ppt_sppt > 0 && g_ui.pwv_sppt != hw->ppt_sppt) {
            snprintf(e, sizeof(e), "SPPT %d→%d W", hw->ppt_sppt, g_ui.pwv_sppt);
            pw_app(out, n, &off, e);
        }
        if (hw->ppt_fppt > 0 && g_ui.pwv_fppt != hw->ppt_fppt) {
            snprintf(e, sizeof(e), "FPPT %d→%d W", hw->ppt_fppt, g_ui.pwv_fppt);
            pw_app(out, n, &off, e);
        }
    }
    if ((g_ui.pw_touched & PW_T_NVBOOST) || hw->nv_boost > 0) {
        if (g_ui.pwv_nvboost != hw->nv_boost) {
            if (hw->nv_boost > 0)
                snprintf(e, sizeof(e), "NV boost %d→%d W", hw->nv_boost, g_ui.pwv_nvboost);
            else
                snprintf(e, sizeof(e), "NV boost →%d W", g_ui.pwv_nvboost);
            pw_app(out, n, &off, e);
        }
    }
    if ((g_ui.pw_touched & PW_T_NVTEMP) || hw->nv_temp > 0) {
        if (g_ui.pwv_nvtemp != hw->nv_temp) {
            if (hw->nv_temp > 0)
                snprintf(e, sizeof(e), "NV temp %d→%d °C", hw->nv_temp, g_ui.pwv_nvtemp);
            else
                snprintf(e, sizeof(e), "NV temp →%d °C", g_ui.pwv_nvtemp);
            pw_app(out, n, &off, e);
        }
    }
    if (g_ui.pwv_panel_od != hw->panel_od)
        pw_app(out, n, &off, g_ui.pwv_panel_od ? "panel OD on" : "panel OD off");
    if (g_ui.pwv_cpuboost != hw->cpu_boost)
        pw_app(out, n, &off, g_ui.pwv_cpuboost ? "CPU boost on" : "CPU boost off");
    if (hw->cpu_mhz_limit > 0 && g_ui.pwv_mhz != hw->cpu_mhz_limit) {
        snprintf(e, sizeof(e), "CPU %d→%d MHz", hw->cpu_mhz_limit, g_ui.pwv_mhz);
        pw_app(out, n, &off, e);
    }
}

static void pw_apply(void)
{
    hw_state_t *hw = g_ui.hw;
    int fails = 0;
    char sum[160];
    pw_diff_summary(sum, sizeof(sum));

    /* profile first: it can move the EPP too, and an explicitly staged
     * EPP must win over the profile-implied one */
    if (g_ui.pwv_profile != (int)hw->profile) {
        if (ctrl_set_profile(hw, (hw_profile_t)g_ui.pwv_profile) != 0)
            fails++;
    }
    if (g_ui.pwv_epp != (int)hw->epp) {
        if (ctrl_set_epp(hw, (hw_epp_t)g_ui.pwv_epp) != 0)
            fails++;
    }
    if (g_ui.pwv_ppt_off != hw->ppt_off) {
        if (g_ui.pwv_ppt_off) {
            if (ctrl_ppt_off(hw) != 0)
                fails++;
        } else {
            if (ctrl_ppt_restore(hw) != 0)
                fails++;
        }
    }
    /* the watt triple only when touched or known-different — a stale
     * nb-wmi read (0 W) must not turn defaults into writes */
    if (!g_ui.pwv_ppt_off &&
        ((g_ui.pw_touched & PW_T_PPT) ||
         (hw->ppt_spl > 0 && g_ui.pwv_spl != hw->ppt_spl) ||
         (hw->ppt_sppt > 0 && g_ui.pwv_sppt != hw->ppt_sppt) ||
         (hw->ppt_fppt > 0 && g_ui.pwv_fppt != hw->ppt_fppt))) {
        if (ctrl_set_ppt(hw, g_ui.pwv_spl, g_ui.pwv_sppt, g_ui.pwv_fppt) != 0)
            fails++;
    }
    if (((g_ui.pw_touched & PW_T_NVBOOST) || hw->nv_boost > 0) &&
        g_ui.pwv_nvboost != hw->nv_boost) {
        if (ctrl_set_nv_boost(hw, g_ui.pwv_nvboost) != 0)
            fails++;
    }
    if (((g_ui.pw_touched & PW_T_NVTEMP) || hw->nv_temp > 0) &&
        g_ui.pwv_nvtemp != hw->nv_temp) {
        if (ctrl_set_nv_temp(hw, g_ui.pwv_nvtemp) != 0)
            fails++;
    }
    if (g_ui.pwv_panel_od != hw->panel_od) {
        if (ctrl_set_panel_od(hw, g_ui.pwv_panel_od) != 0)
            fails++;
    }
    if (g_ui.pwv_cpuboost != hw->cpu_boost) {
        if (ctrl_set_cpu_boost(hw, g_ui.pwv_cpuboost) != 0)
            fails++;
    }
    /* skip the privileged no-op when no limit is set and the staging
     * is just the cpuinfo maximum */
    if (g_ui.pwv_mhz > 0 && g_ui.pwv_mhz != hw->cpu_mhz_limit &&
        !(hw->cpu_mhz_limit <= 0 && g_ui.pwv_mhz >= hw->cpu_mhz_max)) {
        if (ctrl_set_cpu_max_mhz(hw, g_ui.pwv_mhz) != 0)
            fails++;
    }

    ut_log("power apply: %s", fails ? "some fields FAILED (privilege?)" : "ok");

    hw_refresh_live(hw); /* verify the writes by reading back */
    pw_sync_from_hw();

    /* toast: what just got applied (green) or what failed (red) */
    if (sum[0]) {
        if (fails)
            snprintf(g_ui.pw_msg, sizeof(g_ui.pw_msg), "⚠ %s · %d failed",
                     sum, fails);
        else
            snprintf(g_ui.pw_msg, sizeof(g_ui.pw_msg), "✓ %s", sum);
        g_ui.pw_msg_fail = fails > 0;
        g_ui.pw_msg_ms = now_ms();
    }
}

/* "live" / "--", with "live → staged ●" while an edit is pending and
 * "staged (?)" when the live read is unknown/stale but a staged or just
 * written value exists */
static void pw_val(char *out, size_t n, int live, int staged, const char *unit)
{
    char lb[14], sb[14];
    if (live > 0)   snprintf(lb, sizeof(lb), "%d%s", live, unit);
    else            snprintf(lb, sizeof(lb), "--");
    if (staged > 0) snprintf(sb, sizeof(sb), "%d%s", staged, unit);
    else            snprintf(sb, sizeof(sb), "--");
    if (live > 0 && staged != live)
        snprintf(out, n, "%s → %s ●", lb, sb);
    else if (live <= 0 && staged > 0)
        snprintf(out, n, "%s (?)", sb);
    else
        snprintf(out, n, "%s", lb);
}

static void pw_val_b(char *out, size_t n, bool live, bool staged)
{
    if (staged != live)
        snprintf(out, n, "%s → %s ●", live ? "on" : "off",
                 staged ? "on" : "off");
    else
        snprintf(out, n, "%s", live ? "on" : "off");
}

/* enum-valued rows (profile / EPP): live, with "live → staged ●" */
static void pw_val_e(char *out, size_t n,
                     const char *live, const char *staged)
{
    if (strcmp(live, staged) != 0)
        snprintf(out, n, "%s → %s ●", live, staged);
    else
        snprintf(out, n, "%s", live);
}

static void draw_power(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    const hw_state_t *hw = g_ui.hw;
    int x = r->x + 2, w = r->w - 4;

    char spl[48], sppt[48], fppt[48], nvb[48], nvt[48], cfq[48];
    char pod[32], cb[32], lim[48];
    pw_val(spl,  sizeof(spl),  hw->ppt_spl,  g_ui.pwv_spl,  " W");
    pw_val(sppt, sizeof(sppt), hw->ppt_sppt, g_ui.pwv_sppt, " W");
    pw_val(fppt, sizeof(fppt), hw->ppt_fppt, g_ui.pwv_fppt, " W");
    pw_val(nvb,  sizeof(nvb),  hw->nv_boost, g_ui.pwv_nvboost, " W");
    pw_val(nvt,  sizeof(nvt),  hw->nv_temp,  g_ui.pwv_nvtemp, " °C");
    pw_val(cfq,  sizeof(cfq),  hw->cpu_mhz_limit, g_ui.pwv_mhz, " MHz");
    pw_val_b(pod, sizeof(pod), hw->panel_od, g_ui.pwv_panel_od);
    pw_val_b(cb,  sizeof(cb),  hw->cpu_boost, g_ui.pwv_cpuboost);
    if (g_ui.pwv_ppt_off != hw->ppt_off)
        snprintf(lim, sizeof(lim), "%s → %s ●",
                 hw->ppt_off ? "removed (max)" : "on",
                 g_ui.pwv_ppt_off ? "removed (max)" : "on");
    else
        snprintf(lim, sizeof(lim), "%s", hw->ppt_off ? "removed (max)" : "on");

    char prof[48], epp[64];
    pw_val_e(prof, sizeof(prof),
             hw_profile_name(hw->profile),
             hw_profile_name((hw_profile_t)g_ui.pwv_profile));
    pw_val_e(epp, sizeof(epp),
             hw_epp_name(hw->epp), hw_epp_name((hw_epp_t)g_ui.pwv_epp));

    const char *vals[PW_ROWS] = {
        prof, epp,
        spl, sppt, fppt,
        "45/55/55", "60/75/75", "80/80/80",
        lim,
        nvb, nvt,
        pod, cb,
        cfq,
    };
    static const char *const labels[PW_ROWS] = {
        "Platform profile", "EPP preference",
        "SPL (sustained)", "SPPT (slow boost)", "FPPT (fast boost)",
        "Preset Q45", "Preset B60", "Preset P80",
        "PPT limits",
        "NV dynamic boost", "NV temp target",
        "Panel overdrive", "CPU boost", "CPU clock limit",
    };

    ui_putln(n, x, r->y + 1, w,
             "h/l stage · Enter apply · r revert · q quits twice when staged",
             pal->muted, false);
    /* while the toast is up, one list row yields its place to it */
    bool toast = g_ui.pw_msg_ms > 0 && now_ms() - g_ui.pw_msg_ms < PW_TOAST_MS;
    int rows = r->h - (toast ? 5 : 4);
    if (rows < 0)
        rows = 0;
    for (int i = 0; i < PW_ROWS && i < rows; i++) {
        int y = r->y + 2 + i;
        bool sel = (i == g_ui.pw_sel);
        ui_row(n, x, y, w, labels[i], vals[i], sel);
        tgt_register(x, y, w, 1, TGT(TGT_PANEL_WORKSPACE, ACT_WS_PW_BASE + i));
    }

    /* apply toast: what the last Apply changed, fading after PW_TOAST_MS */
    if (toast)
        ui_putln(n, x, r->y + r->h - 3, w, g_ui.pw_msg,
                 g_ui.pw_msg_fail ? PW_ERR_RGB : PW_OK_RGB, true);

    int by = r->y + r->h - 2;
    ui_btn(n, x, by, " Apply ", g_ui.pw_dirty, false,
           TGT(TGT_PANEL_WORKSPACE, ACT_WS_PW_APPLY));
    ui_btn(n, x + 10, by, " Revert ", false, false,
           TGT(TGT_PANEL_WORKSPACE, ACT_WS_PW_REVERT));
}

/* ---- LIGHT view -------------------------------------------------------- */

enum {
    LT_KBD = 0,
    LT_EFFECT,
    LT_COLOR,
    LT_HEX,
    LT_DAEMON,
    LT_START,
    LT_STOP,
    LT_INSTALL,
    LT_RELOAD,
    LT_EDITOR,
    LT_ROWS
};

static void lt_apply_row(int row)
{
    hw_state_t *hw = g_ui.hw;
    switch (row) {
    case LT_KBD:
        ctrl_set_kbd(hw, (hw_kbd_t)g_ui.ctl_kbd_idx);
        break;
    case LT_EFFECT:
    case LT_COLOR:
        ctrl_set_aura(hw, g_ui.lt_eff, g_ui.lt_col);
        break;
    case LT_HEX:
        if (strlen(g_ui.lt_hex.buf) == 6)
            ctrl_set_aura_hex(hw, g_ui.lt_hex.buf);
        else
            ut_log("hex: need 6 digits");
        break;
    case LT_START:
        if (db_start() != 0)
            ut_log("daeboard: start failed");
        break;
    case LT_STOP:
        if (db_quit() != 0)
            ut_log("daeboard: stop failed");
        break;
    case LT_INSTALL:
        if (db_install() != 0)
            ut_log("daeboard: install failed");
        break;
    case LT_EDITOR:
        editor_daeboard_open();
        break;
    case LT_RELOAD: {
        char err[64];
        if (db_reload(err, (int)sizeof err) != 0)
            ut_log("daeboard: %s", err[0] ? err : "reload failed");
        else
            ut_log("daeboard: binds reloaded");
        break;
    }
    default:
        break;
    }
}

static void draw_light(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int x = r->x + 2, w = r->w - 4;

    static const char *const KBDS[] = { "off", "low", "med", "high" };
    char hexline[128];
    if (g_ui.lt_hex_active)
        snprintf(hexline, sizeof(hexline), "#%s_", g_ui.lt_hex.buf);
    else
        snprintf(hexline, sizeof(hexline), "#%s (type 0-9a-f, Enter applies)",
                 g_ui.lt_hex.buf);

    static time_t probed;
    static int up;
    time_t now = time(NULL);
    char note[160];
    if (now != probed) {
        up = db_up();
        probed = now;
    }
    db_install_note(note, (int)sizeof note);
    const char *vals[LT_ROWS] = {
        KBDS[ut_clamp_i(g_ui.ctl_kbd_idx, 0, 3)],
        AURA_EFFECTS[ut_clamp_i(g_ui.lt_eff, 0, AURA_EFFECT_COUNT - 1)],
        AURA_COLOR_NAMES[ut_clamp_i(g_ui.lt_col, 0, AURA_COLOR_COUNT - 1)],
        hexline,
        up ? "running" : "stopped",
        "systemd-run",
        "quit",
        note,
        "daeboard.binds",
        "b",
    };
    static const char *const labels[LT_ROWS] = {
        "Kbd brightness", "Aura effect", "Aura color", "Custom hex",
        "Daemon", "Start", "Stop", "Install", "Reload binds", "Editor",
    };

    ui_putln(n, x, r->y + 1, w, "h/l change · Enter apply · b editor", pal->muted, false);
    int rows = r->h - 3;
    for (int i = 0; i < LT_ROWS && i < rows; i++) {
        int y = r->y + 2 + i;
        bool sel = (i == g_ui.lt_sel);
        ui_row(n, x, y, w, labels[i], vals[i], sel);
        tgt_register(x, y, w, 1, TGT(TGT_PANEL_WORKSPACE, ACT_WS_LT_BASE + i));
    }

    /* swatch */
    uint64_t ch = 0;
    unsigned int hr = 0, hg = 0, hb = 0;
    if (strlen(g_ui.lt_hex.buf) == 6)
        sscanf(g_ui.lt_hex.buf, "%02x%02x%02x", &hr, &hg, &hb);
    ncchannels_set_fg_rgb(&ch, (hr << 16) | (hg << 8) | hb);
    ncplane_set_channels(n, ch);
    ncplane_putstr_yx(n, r->y + 2 + LT_HEX, x + 40, "██████");
}

/* ---- HELP view --------------------------------------------------------- */

static void draw_help(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int x = r->x + 2, w = r->w - 4;
    const char *lines[] = {
        "GLOBAL",
        "  q            quit (saves settings)",
        "  1..4 / Tab   focus: profiles, controls, workspace, telemetry",
        "  esc / s      settings overlay · ? this help",
        "",
        "CONTROLS / LISTS",
        "  j k          move · h l change value · Enter apply",
        "",
        "POWER",
        "  j k          move · h/l (or arrows) stage the value (nothing writes)",
        "  Enter/w      apply all staged edits · r reverts to live values",
        "  q            quits; with staged edits pending it asks twice",
        "",
        "FAN EDITOR",
        "  c g          switch cpu/gpu curve",
        "  + -          add/remove point · w write to EC",
        "  h l j k      nudge selected point (temp / pwm)",
        "  t p          type exact temp/pwm, Enter sets",
        "  o            toggle curve on/off · click graph to add/select",
        "",
        "PROFILES",
        "  Enter apply · s save current · d delete · n new name",
        "",
        "PRIVILEGES",
        "  writes go asusctl -> direct sysfs -> sudo -n;",
        "  without passwordless sudo some nodes report failure in the log.",
    };
    int y = r->y + 1;
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]) && y < r->y + r->h - 1; i++, y++)
        ui_putln(n, x, y, w, lines[i],
                 lines[i][0] && lines[i][1] == ' ' ? pal->text : pal->accent,
                 lines[i][0] && lines[i][1] != ' ');
}

/* ---- dispatcher -------------------------------------------------------- */

void panel_workspace_draw(struct ncplane *n, const rect_t *r)
{
    char title[32];
    snprintf(title, sizeof(title), "%s%s",
             WS_NAMES[ut_clamp_i(g_ui.ws_view, 0, WSV_COUNT - 1)],
             (g_ui.ws_view == WSV_POWER && g_ui.pw_dirty) ? " ●" : "");
    ui_box(n, r, title, g_ui.focus == FOC_WORKSPACE);

    /* view tabs on the border row */
    int tx = r->x + r->w - 42;
    if (tx < r->x + 16)
        tx = r->x + 16;
    struct { const char *label; ws_view_t v; int id; } tabs[] = {
        { " FAN ", WSV_FAN, ACT_WS_TAB_FAN },
        { " POWER ", WSV_POWER, ACT_WS_TAB_POWER },
        { " LIGHT ", WSV_LIGHT, ACT_WS_TAB_LIGHT },
        { " ? ", WSV_HELP, ACT_WS_TAB_HELP },
    };
    for (size_t i = 0; i < sizeof(tabs) / sizeof(tabs[0]); i++) {
        ui_btn(n, tx, r->y, tabs[i].label, g_ui.ws_view == tabs[i].v, false,
               TGT(TGT_PANEL_WORKSPACE, tabs[i].id));
        tx += (int)strlen(tabs[i].label);
    }
    /* SET opens the fullscreen settings overlay */
    ui_btn(n, tx + 1, r->y, " SET ", g_ui.settings_overlay, false,
           TGT(TGT_PANEL_WORKSPACE, ACT_WS_TAB_SETTINGS));

    switch (g_ui.ws_view) {
    case WSV_FAN:      editor_fan_draw(n, r); break;
    case WSV_POWER:    draw_power(n, r); break;
    case WSV_LIGHT:    draw_light(n, r); break;
    case WSV_HELP:     draw_help(n, r); break;
    default: break;
    }
}

void panel_workspace_key(uint32_t key)
{
    /* hex typing for the LIGHT view */
    if (g_ui.ws_view == WSV_LIGHT && g_ui.lt_hex_active) {
        if (key == NCKEY_ESC) {
            g_ui.lt_hex_active = 0;
            return;
        }
        if (key == NCKEY_ENTER || key == '\r' || key == '\n') {
            g_ui.lt_hex_active = 0;
            lt_apply_row(LT_HEX);
            return;
        }
        tin_key(&g_ui.lt_hex, key);
        return;
    }

    if (!view_binds_key(g_ui.ws_view, key)) {
        switch (key) {
        case 'f':
        case 'F': ws_set_view(WSV_FAN); return;
        case 'p':
        case 'P': ws_set_view(WSV_POWER); return;
        case 'l':
        case 'L': ws_set_view(WSV_LIGHT); return;
        case 'e':
        case 'E': settings_open(); return;
        default:
            break;
        }
    }

    switch (g_ui.ws_view) {
    case WSV_FAN:
        editor_fan_key(key);
        return;
    case WSV_POWER: {
        switch (key) {
        case 'j': case NCKEY_DOWN: g_ui.pw_sel = (g_ui.pw_sel + 1) % PW_ROWS; return;
        case 'k': case NCKEY_UP:   g_ui.pw_sel = (g_ui.pw_sel + PW_ROWS - 1) % PW_ROWS; return;
        case 'h': case NCKEY_LEFT: pw_nudge_row(g_ui.pw_sel, -1); return;
        case 'l': case NCKEY_RIGHT:
            /* values step; presets stage their bundle; toggles flip */
            pw_nudge_row(g_ui.pw_sel, +1);
            return;
        case NCKEY_ENTER: case '\r': case '\n': case ' ':
        case 'w': case 'W':
            pw_apply();
            return;
        case 'r': case 'R': pw_sync_from_hw(); return; /* revert staged */
        default: return;
        }
    }
    case WSV_LIGHT: {
        switch (key) {
        case 'j': case NCKEY_DOWN: g_ui.lt_sel = (g_ui.lt_sel + 1) % LT_ROWS; return;
        case 'k': case NCKEY_UP:   g_ui.lt_sel = (g_ui.lt_sel + LT_ROWS - 1) % LT_ROWS; return;
        case 'h': case NCKEY_LEFT:
            switch (g_ui.lt_sel) {
            case LT_KBD:    g_ui.ctl_kbd_idx = (g_ui.ctl_kbd_idx + 3) % 4; break;
            case LT_EFFECT: g_ui.lt_eff = (g_ui.lt_eff + AURA_EFFECT_COUNT - 1) % AURA_EFFECT_COUNT; break;
            case LT_COLOR:  g_ui.lt_col = (g_ui.lt_col + AURA_COLOR_COUNT - 1) % AURA_COLOR_COUNT; break;
            default: break;
            }
            return;
        case 'l': case NCKEY_RIGHT:
            switch (g_ui.lt_sel) {
            case LT_KBD:    g_ui.ctl_kbd_idx = (g_ui.ctl_kbd_idx + 1) % 4; break;
            case LT_EFFECT: g_ui.lt_eff = (g_ui.lt_eff + 1) % AURA_EFFECT_COUNT; break;
            case LT_COLOR:  g_ui.lt_col = (g_ui.lt_col + 1) % AURA_COLOR_COUNT; break;
            default: break;
            }
            return;
        case 'b': case 'B':
            editor_daeboard_open();
            return;
        case 'x': case 'X':
            g_ui.lt_hex_active = 1;
            tin_clear(&g_ui.lt_hex);
            return;
        case NCKEY_ENTER: case '\r': case '\n': case ' ':
            lt_apply_row(g_ui.lt_sel);
            return;
        default: return;
        }
    }
    default:
        return;
    }
}

void panel_workspace_act(int id)
{
    switch (id) {
    case ACT_WS_TAB_FAN:      ws_set_view(WSV_FAN); return;
    case ACT_WS_TAB_POWER:    ws_set_view(WSV_POWER); return;
    case ACT_WS_TAB_LIGHT:    ws_set_view(WSV_LIGHT); return;
    case ACT_WS_TAB_SETTINGS: settings_open(); return;
    case ACT_WS_TAB_HELP:     ws_set_view(WSV_HELP); return;
    default:
        break;
    }

    if (id >= ACT_WS_PW_BASE && id < ACT_WS_PW_BASE + PW_ROWS) {
        int row = id - ACT_WS_PW_BASE;
        if (row == g_ui.pw_sel)
            pw_nudge_row(row, +1); /* second click stages/toggles the row */
        else
            g_ui.pw_sel = row;
        return;
    }
    switch (id) {
    case ACT_WS_PW_APPLY: pw_apply(); return;
    case ACT_WS_PW_REVERT: pw_sync_from_hw(); return;
    default:
        break;
    }
    if (id >= ACT_WS_LT_BASE && id < ACT_WS_LT_BASE + LT_ROWS) {
        int row = id - ACT_WS_LT_BASE;
        if (row == g_ui.lt_sel) {
            if (row == LT_HEX)
                g_ui.lt_hex_active = 1;
            else
                lt_apply_row(row);
        } else {
            g_ui.lt_sel = row;
        }
        return;
    }
}
