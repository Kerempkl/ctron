#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ui_internal.h"
#include "daeboard.h"
#include "../control.h"
#include "../util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Right workspace views:
 *   VIEW_POWER  — Platform profile, asusd auto-profiles, EPP, Quick mode,
 *                 PPT presets/limits, GPU clock lock, CPU boost & ceiling
 *                 (+ 'c' Core Limits overlay).
 *   VIEW_FANS   — Delegated to editor_fan.c.
 *   VIEW_SYSTEM — Display (Refresh Hz, Panel overdrive), Battery (charge
 *                 limit, one-shot 100%), Lighting (Kbd, Aura, Hex, daeboard).
 *
 * Every hardware field follows the same staged model: h/l mutates staging,
 * nothing reaches hardware until w / Enter; r reverts to live reads. */

enum {
    ACT_WS_PW_PRESET_BASE = 17, /* + preset index (0..2) */
    ACT_WS_PW_BASE        = 20, /* + PW_* row index */
    ACT_WS_SYS_BASE       = 50, /* + SYS_* row index */
};

/* ---- shared mode state (POWER Quick mode row + Settings modes) ---------- */

void ctl_capture_mode(const char *name, const char *steps)
{
    unsigned want = mode_touch_mask(steps);
    snprintf(g_ui.ctl_applied, sizeof(g_ui.ctl_applied), "%s", name ? name : "");
    g_ui.ctl_mode_mask = mode_snapshot(g_ui.hw, want, &g_ui.ctl_snap);
    g_ui.ctl_mode_pick = false;
}

static void pw_mode_val(char *out, size_t n)
{
    if (g_ui.mode_n == 0) {
        snprintf(out, n, "(none — ',' to add)");
        return;
    }
    g_ui.ctl_mode_idx = ut_clamp_i(g_ui.ctl_mode_idx, 0, g_ui.mode_n - 1);
    const char *cand = g_ui.modes[g_ui.ctl_mode_idx].name;
    if (!g_ui.ctl_applied[0]) {
        if (!g_ui.ctl_mode_pick)
            snprintf(out, n, "- (%s)", cand);
        else
            snprintf(out, n, "- → %s (Enter applies)", cand);
        return;
    }
    int drift = mode_drift_count(g_ui.hw, g_ui.ctl_mode_mask, &g_ui.ctl_snap);
    if (!strcmp(cand, g_ui.ctl_applied) || !g_ui.ctl_mode_pick)
        snprintf(out, n, "%s%s", g_ui.ctl_applied, drift ? "*" : "");
    else
        snprintf(out, n, "%s%s → %s (Enter applies)", g_ui.ctl_applied,
                 drift ? "*" : "", cand);
}

/* ---- helpers ------------------------------------------------------------ */

static int popcount_u(unsigned m)
{
    int c = 0;
    while (m) {
        c += (int)(m & 1u);
        m >>= 1;
    }
    return c;
}

static void sum_app(char *out, size_t n, int *off, const char *entry)
{
    const char *sep = *off > 0 ? " · " : "";
    if (*off + (int)strlen(sep) + (int)strlen(entry) >= (int)n - 2)
        return;
    *off += snprintf(out + *off, n - (size_t)*off, "%s%s", sep, entry);
}

static void fmt_val_int(char *out, size_t n, int live, int staged,
                        const char *unit, const char *range)
{
    char lb[16], sb[16];
    if (live > 0)   snprintf(lb, sizeof(lb), "%d%s", live, unit);
    else            snprintf(lb, sizeof(lb), "--");
    if (staged > 0) snprintf(sb, sizeof(sb), "%d%s", staged, unit);
    else            snprintf(sb, sizeof(sb), "--");
    if (live > 0 && staged != live)
        snprintf(out, n, "%s → %s ●", lb, sb);
    else if (live <= 0 && staged > 0)
        snprintf(out, n, "%s (?)", sb);
    else if (range && range[0])
        snprintf(out, n, "%s (%s)", lb, range);
    else
        snprintf(out, n, "%s", lb);
}

static void fmt_val_bool(char *out, size_t n, bool live, bool staged)
{
    if (staged != live)
        snprintf(out, n, "%s → %s ●", live ? "on" : "off",
                 staged ? "on" : "off");
    else
        snprintf(out, n, "%s", live ? "on" : "off");
}

static void fmt_val_enum(char *out, size_t n,
                         const char *live, const char *staged)
{
    if (strcmp(live, staged) != 0)
        snprintf(out, n, "%s → %s ●", live, staged);
    else
        snprintf(out, n, "%s", live);
}

static void draw_group_hdr(struct ncplane *n, int x, int y, int w,
                           const char *title)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    ui_putln(n, x, y, w, title, pal->muted, true);
    int tw = ut_cells(title) + 1;
    if (w - tw > 2)
        ui_hline(n, x + tw, y, w - tw, pal->border);
}

/* =========================================================================
 *  VIEW 1: POWER
 * ========================================================================= */

#define PW_FW_TEMP_ON  90
#define PW_FW_TEMP_OFF 88

enum {
    PW_PROFILE = 0,
    PW_AC_AUTO,
    PW_BAT_AUTO,
    PW_EPP,
    PW_MODE,            /* shortcut bundle from modes.ini */
    PW_PRESET,          /* Q45 / B60 / P80 */
    PW_SPL,
    PW_SPPT,
    PW_FPPT,
    PW_PPT_LIMITS,
    PW_GPUCLOCK,
    PW_CPUBOOST,
    PW_CPUFREQ,
    PW_ROWS
};

static const int PW_PRESET_WATTS[3][3] = {
    { 45, 55, 55 },
    { 60, 75, 75 },
    { 80, 80, 80 },
};
static const char *const PW_PRESET_NAMES[3] = { " Q45 ", " B60 ", " P80 " };
static const int PW_GPU_STEPS[] = { 0, 800, 1000, 1200, 1400, 1600, 1800, 2100 };

static const struct { int first; const char *name; } PW_GROUPS[] = {
    { PW_PROFILE,  "PLATFORM" },
    { PW_PRESET,   "POWER LIMITS (PPT)" },
    { PW_GPUCLOCK, "CLOCKS & BOOST" },
};
#define PW_GROUPS_N ((int)(sizeof(PW_GROUPS) / sizeof(PW_GROUPS[0])))

static const char *const PW_DESCS[PW_ROWS] = {
    "firmware power profile — fan, clock and EPP defaults follow it",
    "asusd re-applies this profile on AC power events (off = no takeover)",
    "asusd re-applies this profile on battery events (off = no takeover)",
    "scheduler energy-vs-performance hint (amd-pstate)",
    "modes.ini shortcut bundle — h/l pick, Enter applies bundle",
    "one pick stages the whole watt triple; custom = anything else",
    "long-term watt ceiling — main knob for thermals and battery",
    "watt ceiling during longer boosts (seconds-scale)",
    "watt ceiling for short bursts (sub-second)",
    "off removes the SMU caps (platform maxima) until reboot",
    "dGPU clock lock; driver default = no lock (session-only)",
    "off disables turbo frequencies entirely",
    "MHz ceiling for all cores; per-core / CCD ceilings via 'c' or Enter",
};

static const char *pw_group_name(int row)
{
    for (int g = 0; g < PW_GROUPS_N; g++)
        if (PW_GROUPS[g].first == row)
            return PW_GROUPS[g].name;
    return NULL;
}

static int pw_slot(int row)
{
    int s = row;
    for (int g = 0; g < PW_GROUPS_N; g++)
        if (PW_GROUPS[g].first <= row)
            s++;
    return s;
}

static void pw_view_clamp(int budget)
{
    int total = PW_ROWS + PW_GROUPS_N;
    if (budget >= total) {
        g_ui.pw_top = 0;
        return;
    }
    int top = (g_ui.pw_top >= 0 && g_ui.pw_top < PW_ROWS) ? g_ui.pw_top : 0;
    int sel = pw_slot(g_ui.pw_sel);
    for (int guard = 0; guard < PW_ROWS + 2; guard++) {
        int tl = pw_slot(top) - (pw_group_name(top) ? 1 : 0);
        if (sel < tl)
            top = g_ui.pw_sel;
        else if (sel > tl + budget - 1)
            top++;
        else
            break;
    }
    for (int t = PW_ROWS - 1; t >= 0; t--) {
        int tl = pw_slot(t) - (pw_group_name(t) ? 1 : 0);
        if (tl + budget - 1 >= total - 1 || t == 0) {
            if (top > t)
                top = t;
            break;
        }
    }
    g_ui.pw_top = top;
}

static int pw_preset_match(int spl, int sppt, int fppt)
{
    for (int i = 0; i < 3; i++)
        if (spl == PW_PRESET_WATTS[i][0] && sppt == PW_PRESET_WATTS[i][1] &&
            fppt == PW_PRESET_WATTS[i][2])
            return i;
    return -1;
}

enum {
    PW_W_PROFILE = 1 << 0,
    PW_W_AC      = 1 << 1,
    PW_W_BAT     = 1 << 2,
    PW_W_EPP     = 1 << 3,
    PW_W_PPT_OFF = 1 << 4,
    PW_W_PPT     = 1 << 5,
    PW_W_GPU     = 1 << 6,
    PW_W_BOOST   = 1 << 8,
    PW_W_CPUFREQ = 1 << 9,
    PW_W_CORES   = 1 << 10,
};

static unsigned pw_write_mask(const hw_state_t *hw)
{
    unsigned m = 0;
    if (g_ui.pwv_profile != (int)hw->profile)
        m |= PW_W_PROFILE;
    if (hw->asusd_ac != -2 && g_ui.pwv_asusd_ac != hw->asusd_ac)
        m |= PW_W_AC;
    if (hw->asusd_bat != -2 && g_ui.pwv_asusd_bat != hw->asusd_bat)
        m |= PW_W_BAT;
    if (g_ui.pwv_epp != (int)hw->epp)
        m |= PW_W_EPP;
    if (g_ui.pwv_ppt_off != hw->ppt_off)
        m |= PW_W_PPT_OFF;
    if (!g_ui.pwv_ppt_off &&
        ((g_ui.pw_touched & PW_T_PPT) ||
         (hw->ppt_spl > 0 && g_ui.pwv_spl != hw->ppt_spl) ||
         (hw->ppt_sppt > 0 && g_ui.pwv_sppt != hw->ppt_sppt) ||
         (hw->ppt_fppt > 0 && g_ui.pwv_fppt != hw->ppt_fppt)))
        m |= PW_W_PPT;
    if (g_ui.pwv_gpuclock != hw->gpu_clock_lock)
        m |= PW_W_GPU;
    if (g_ui.pwv_cpuboost != hw->cpu_boost)
        m |= PW_W_BOOST;
    if ((g_ui.pw_touched & PW_T_CPUFREQ) &&
        g_ui.pwv_mhz > 0 && g_ui.pwv_mhz != hw->cpu_mhz_limit &&
        !(hw->cpu_mhz_limit <= 0 && g_ui.pwv_mhz >= hw->cpu_mhz_max))
        m |= PW_W_CPUFREQ;
    if (cf_staged_n() > 0)
        m |= PW_W_CORES;
    return m;
}

void pw_recompute_dirty(void)
{
    g_ui.pw_dirty = pw_write_mask(g_ui.hw) != 0;
}

int pw_staged_count(void)
{
    return popcount_u(pw_write_mask(g_ui.hw));
}

const char *pw_selected_desc(void)
{
    return PW_DESCS[ut_clamp_i(g_ui.pw_sel, 0, PW_ROWS - 1)];
}

void pw_sync_from_hw(void)
{
    const hw_state_t *hw = g_ui.hw;
    g_ui.pwv_profile = (int)hw->profile;
    if (hw->asusd_ac != -2)
        g_ui.pwv_asusd_ac = hw->asusd_ac;
    if (hw->asusd_bat != -2)
        g_ui.pwv_asusd_bat = hw->asusd_bat;
    g_ui.pwv_epp = (int)hw->epp;
    g_ui.pwv_spl  = hw->ppt_spl  > 0 ? hw->ppt_spl
                  : (g_ui.pwv_spl  > 0 ? g_ui.pwv_spl  : 45);
    g_ui.pwv_sppt = hw->ppt_sppt > 0 ? hw->ppt_sppt
                  : (g_ui.pwv_sppt > 0 ? g_ui.pwv_sppt : 55);
    g_ui.pwv_fppt = hw->ppt_fppt > 0 ? hw->ppt_fppt
                  : (g_ui.pwv_fppt > 0 ? g_ui.pwv_fppt : 55);
    g_ui.pwv_gpuclock = hw->gpu_clock_lock;
    g_ui.pwv_mhz = hw->cpu_mhz_limit > 0 ? hw->cpu_mhz_limit
                 : (g_ui.pwv_mhz > 0 ? g_ui.pwv_mhz : hw->cpu_mhz_max);
    g_ui.pwv_cpuboost = hw->cpu_boost;
    g_ui.pwv_ppt_off = hw->ppt_off;
    g_ui.pw_quit_warned = false;
    g_ui.pw_touched = 0;
    {
        int m = pw_preset_match(g_ui.pwv_spl, g_ui.pwv_sppt, g_ui.pwv_fppt);
        g_ui.pw_preset_sel = m >= 0 ? m : g_ui.pw_preset_sel;
    }
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
    g_ui.pwv_ppt_off = false;
    g_ui.pw_touched |= PW_T_PPT;
    pw_recompute_dirty();
}

static void pw_stage_preset_idx(int idx)
{
    g_ui.pw_preset_sel = idx;
    pw_stage_preset(PW_PRESET_WATTS[idx][0], PW_PRESET_WATTS[idx][1],
                    PW_PRESET_WATTS[idx][2]);
}

static void pw_nudge_row(int row, int dir)
{
    hw_state_t *hw = g_ui.hw;
    g_ui.pw_msg_ms = 0;
    switch (row) {
    case PW_PROFILE:
        g_ui.pwv_profile = (g_ui.pwv_profile +
                            (dir > 0 ? 1 : HW_PROF_COUNT - 1)) % HW_PROF_COUNT;
        g_ui.pw_touched |= PW_T_PROFILE;
        break;
    case PW_AC_AUTO:
    case PW_BAT_AUTO: {
        int live = row == PW_AC_AUTO ? hw->asusd_ac : hw->asusd_bat;
        if (live == -2) {
            ut_log("asusd auto-profile unavailable (asusd absent or unreadable)");
            return;
        }
        static const int cyc[4] = { -1, HW_QUIET, HW_BALANCED, HW_PERFORMANCE };
        int *v = row == PW_AC_AUTO ? &g_ui.pwv_asusd_ac : &g_ui.pwv_asusd_bat;
        int idx = 0;
        for (int c = 0; c < 4; c++)
            if (*v == cyc[c])
                idx = c;
        *v = cyc[(idx + (dir > 0 ? 1 : 3)) % 4];
        g_ui.pw_touched |= row == PW_AC_AUTO ? PW_T_AC_AUTO : PW_T_BAT_AUTO;
        break;
    }
    case PW_EPP:
        g_ui.pwv_epp = (g_ui.pwv_epp +
                        (dir > 0 ? 1 : HW_EPP_COUNT - 1)) % HW_EPP_COUNT;
        g_ui.pw_touched |= PW_T_EPP;
        break;
    case PW_MODE:
        if (g_ui.mode_n > 0) {
            g_ui.ctl_mode_idx = (g_ui.ctl_mode_idx +
                                 (dir > 0 ? 1 : g_ui.mode_n - 1)) % g_ui.mode_n;
            g_ui.ctl_mode_pick = true;
        }
        return;
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
        ctrl_ppt_order(&g_ui.pwv_spl, &g_ui.pwv_sppt, &g_ui.pwv_fppt,
                       smin, smax, pmin, pmax, fmin, fmax);
        g_ui.pw_touched |= PW_T_PPT;
        break;
    }
    case PW_PRESET:
        g_ui.pw_preset_sel = (g_ui.pw_preset_sel + (dir > 0 ? 1 : 2)) % 3;
        pw_stage_preset_idx(g_ui.pw_preset_sel);
        return;
    case PW_PPT_LIMITS:
        if (!g_ui.pwv_ppt_off) {
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
    case PW_GPUCLOCK: {
        int n = (int)(sizeof(PW_GPU_STEPS) / sizeof(PW_GPU_STEPS[0]));
        int idx = 0, best = 1 << 30;
        for (int i = 0; i < n; i++) {
            int d = abs(g_ui.pwv_gpuclock - PW_GPU_STEPS[i]);
            if (d < best) { best = d; idx = i; }
        }
        g_ui.pwv_gpuclock = PW_GPU_STEPS[(idx + (dir > 0 ? 1 : n - 1)) % n];
        g_ui.pw_touched |= PW_T_GPUCLOCK;
        break;
    }
    case PW_CPUBOOST:
        g_ui.pwv_cpuboost = !g_ui.pwv_cpuboost;
        break;
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

static const char *pw_asusd_name(int v)
{
    if (v == -1) return "off";
    if (v >= 0 && v < HW_PROF_COUNT) return hw_profile_name((hw_profile_t)v);
    return "--";
}

static void pw_diff_summary(char *out, size_t n)
{
    const hw_state_t *hw = g_ui.hw;
    unsigned wm = pw_write_mask(hw);
    char e[48];
    int off = 0;
    out[0] = '\0';

    if (wm & PW_W_PROFILE) {
        snprintf(e, sizeof(e), "profile %s", hw_profile_name((hw_profile_t)g_ui.pwv_profile));
        sum_app(out, n, &off, e);
    }
    if (wm & PW_W_AC) {
        snprintf(e, sizeof(e), "AC auto %s", pw_asusd_name(g_ui.pwv_asusd_ac));
        sum_app(out, n, &off, e);
    }
    if (wm & PW_W_BAT) {
        snprintf(e, sizeof(e), "bat auto %s", pw_asusd_name(g_ui.pwv_asusd_bat));
        sum_app(out, n, &off, e);
    }
    if (wm & PW_W_EPP) {
        snprintf(e, sizeof(e), "EPP %s", hw_epp_name((hw_epp_t)g_ui.pwv_epp));
        sum_app(out, n, &off, e);
    }
    if (wm & PW_W_PPT_OFF) {
        sum_app(out, n, &off, g_ui.pwv_ppt_off ? "PPT limits off" : "PPT limits on");
    } else if (wm & PW_W_PPT) {
        int watts = 0;
        if (hw->ppt_spl > 0 && g_ui.pwv_spl != hw->ppt_spl) {
            snprintf(e, sizeof(e), "SPL %d→%d W", hw->ppt_spl, g_ui.pwv_spl);
            sum_app(out, n, &off, e); watts++;
        } else if (hw->ppt_spl <= 0 && g_ui.pwv_spl > 0) {
            snprintf(e, sizeof(e), "SPL →%d W", g_ui.pwv_spl);
            sum_app(out, n, &off, e); watts++;
        }
        if (hw->ppt_sppt > 0 && g_ui.pwv_sppt != hw->ppt_sppt) {
            snprintf(e, sizeof(e), "SPPT %d→%d W", hw->ppt_sppt, g_ui.pwv_sppt);
            sum_app(out, n, &off, e); watts++;
        }
        if (hw->ppt_fppt > 0 && g_ui.pwv_fppt != hw->ppt_fppt) {
            snprintf(e, sizeof(e), "FPPT %d→%d W", hw->ppt_fppt, g_ui.pwv_fppt);
            sum_app(out, n, &off, e); watts++;
        }
        if (!watts) {
            snprintf(e, sizeof(e), "PPT %d/%d/%d W",
                     g_ui.pwv_spl, g_ui.pwv_sppt, g_ui.pwv_fppt);
            sum_app(out, n, &off, e);
        }
    }
    if (wm & PW_W_GPU) {
        if (g_ui.pwv_gpuclock > 0 && hw->gpu_clock_lock > 0)
            snprintf(e, sizeof(e), "GPU %d→%d MHz", hw->gpu_clock_lock, g_ui.pwv_gpuclock);
        else if (g_ui.pwv_gpuclock > 0)
            snprintf(e, sizeof(e), "GPU lock %d MHz", g_ui.pwv_gpuclock);
        else
            snprintf(e, sizeof(e), "GPU lock reset");
        sum_app(out, n, &off, e);
    }
    if (wm & PW_W_BOOST)
        sum_app(out, n, &off, g_ui.pwv_cpuboost ? "CPU boost on" : "CPU boost off");
    if (wm & PW_W_CPUFREQ) {
        if (hw->cpu_mhz_limit > 0)
            snprintf(e, sizeof(e), "CPU %d→%d MHz", hw->cpu_mhz_limit, g_ui.pwv_mhz);
        else
            snprintf(e, sizeof(e), "CPU →%d MHz", g_ui.pwv_mhz);
        sum_app(out, n, &off, e);
    }
    if (wm & PW_W_CORES) {
        snprintf(e, sizeof(e), "cores: %d staged", cf_staged_n());
        sum_app(out, n, &off, e);
    }
}

void pw_apply_writes(hw_state_t *hw, int *fails_out, char *sum, size_t sumn)
{
    int fails = 0;
    pw_diff_summary(sum, sumn);
    ut_progress("power: applying");

    if (pw_write_mask(hw) & PW_W_PROFILE) {
        if (ctrl_set_profile(hw, (hw_profile_t)g_ui.pwv_profile) != 0)
            fails++;
        int enf = hw_asusd_enforced(hw);
        if (enf >= 0 && g_ui.pwv_profile != enf)
            strncat(sum, " · asusd will revert (auto on)", sumn - strlen(sum) - 1);
    }
    if (pw_write_mask(hw) & PW_W_EPP) {
        if (ctrl_set_epp(hw, (hw_epp_t)g_ui.pwv_epp) != 0)
            fails++;
    }
    if (pw_write_mask(hw) & PW_W_PPT_OFF) {
        if (g_ui.pwv_ppt_off ? ctrl_ppt_off(hw) != 0 : ctrl_ppt_restore(hw) != 0)
            fails++;
    }
    if (pw_write_mask(hw) & PW_W_PPT) {
        if (ctrl_set_ppt(hw, g_ui.pwv_spl, g_ui.pwv_sppt, g_ui.pwv_fppt) != 0)
            fails++;
    }
    if (pw_write_mask(hw) & PW_W_GPU) {
        if (g_ui.pwv_gpuclock > 0) {
            if (ctrl_gpu_clock_lock(hw, g_ui.pwv_gpuclock) != 0)
                fails++;
        } else if (ctrl_gpu_clock_reset(hw) != 0) {
            fails++;
        }
    }
    if (pw_write_mask(hw) & PW_W_BOOST) {
        if (ctrl_set_cpu_boost(hw, g_ui.pwv_cpuboost) != 0)
            fails++;
    }
    int cf_ids[HW_CPU_MAX], cf_vals[HW_CPU_MAX], cf_n = 0;
    cf_capture_staged(hw, cf_ids, cf_vals, &cf_n);
    if (pw_write_mask(hw) & PW_W_CPUFREQ) {
        if (ctrl_set_cpu_max_mhz(hw, g_ui.pwv_mhz) != 0)
            fails++;
    }
    if (cf_n > 0) {
        ut_progress("core limits: writing");
        int cfails = 0;
        cf_apply_list(hw, cf_ids, cf_vals, cf_n, &cfails);
        fails += cfails;
    }
    if (pw_write_mask(hw) & (PW_W_AC | PW_W_BAT)) {
        int ac = (hw->asusd_ac != -2 && g_ui.pwv_asusd_ac != hw->asusd_ac)
                     ? g_ui.pwv_asusd_ac : -2;
        int bat = (hw->asusd_bat != -2 && g_ui.pwv_asusd_bat != hw->asusd_bat)
                      ? g_ui.pwv_asusd_bat : -2;
        if (ctrl_set_asusd_auto(hw, ac, bat) != 0)
            fails++;
    }
    hw_refresh_live(hw);
    *fails_out = fails;
}

static int pw_smin, pw_smax, pw_pmin, pw_pmax, pw_fmin, pw_fmax;
static long pw_lim_ms;

static void pw_refresh_limits(void)
{
    long t = ut_mono_ms();
    if (pw_lim_ms != 0 && t - pw_lim_ms <= 1000)
        return;
    ctrl_ppt_limits(g_ui.hw, &pw_smin, &pw_smax,
                    &pw_pmin, &pw_pmax, &pw_fmin, &pw_fmax);
    pw_lim_ms = t;
}

static void pw_row_range(int row, char *out, size_t n)
{
    switch (row) {
    case PW_SPL:  snprintf(out, n, "%d–%d", pw_smin, pw_smax); break;
    case PW_SPPT: snprintf(out, n, "%d–%d", pw_pmin, pw_pmax); break;
    case PW_FPPT: snprintf(out, n, "%d–%d", pw_fmin, pw_fmax); break;
    case PW_GPUCLOCK:
        if (g_ui.hw->gpu_mhz_max > 0)
            snprintf(out, n, "0=reset | 800–%d", g_ui.hw->gpu_mhz_max);
        else
            snprintf(out, n, "0=reset | 800+");
        break;
    case PW_CPUFREQ:
        if (g_ui.hw->cpu_mhz_max > 0)
            snprintf(out, n, "%d–%d", g_ui.hw->cpu_mhz_min, g_ui.hw->cpu_mhz_max);
        else
            out[0] = '\0';
        break;
    default:
        out[0] = '\0';
    }
}

static bool pw_row_numeric(int row)
{
    return row == PW_SPL || row == PW_SPPT || row == PW_FPPT ||
           row == PW_GPUCLOCK || row == PW_CPUFREQ;
}

static int pw_staged_value(int row)
{
    switch (row) {
    case PW_SPL:      return g_ui.pwv_spl;
    case PW_SPPT:     return g_ui.pwv_sppt;
    case PW_FPPT:     return g_ui.pwv_fppt;
    case PW_GPUCLOCK: return g_ui.pwv_gpuclock;
    case PW_CPUFREQ:  return g_ui.pwv_mhz;
    default:          return 0;
    }
}

static void pw_commit_typed(void)
{
    hw_state_t *hw = g_ui.hw;
    for (const char *p = g_ui.pw_input.buf; *p; p++) {
        if (*p < '0' || *p > '9') {
            ut_log("edit: digits only — dropped");
            return;
        }
    }
    int v = atoi(g_ui.pw_input.buf);
    g_ui.pw_msg_ms = 0;
    switch (g_ui.pw_sel) {
    case PW_SPL: case PW_SPPT: case PW_FPPT: {
        int smin, smax, pmin, pmax, fmin, fmax;
        ctrl_ppt_limits(hw, &smin, &smax, &pmin, &pmax, &fmin, &fmax);
        if (g_ui.pw_sel == PW_SPL) g_ui.pwv_spl = v;
        else if (g_ui.pw_sel == PW_SPPT) g_ui.pwv_sppt = v;
        else g_ui.pwv_fppt = v;
        g_ui.pwv_ppt_off = false;
        ctrl_ppt_order(&g_ui.pwv_spl, &g_ui.pwv_sppt, &g_ui.pwv_fppt,
                       smin, smax, pmin, pmax, fmin, fmax);
        g_ui.pw_touched |= PW_T_PPT;
        break;
    }
    case PW_GPUCLOCK:
        g_ui.pwv_gpuclock = v > 0 ? ut_clamp_i(v, 200,
                                               hw->gpu_mhz_max > 0 ? hw->gpu_mhz_max : 4096)
                                  : 0;
        g_ui.pw_touched |= PW_T_GPUCLOCK;
        break;
    case PW_CPUFREQ:
        if (hw->cpu_mhz_max > 0)
            v = ut_clamp_i(v, hw->cpu_mhz_min, hw->cpu_mhz_max);
        g_ui.pwv_mhz = v;
        g_ui.pw_touched |= PW_T_CPUFREQ;
        break;
    default:
        return;
    }
    pw_recompute_dirty();
}

static void draw_power_list(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    const hw_state_t *hw = g_ui.hw;
    int x = r->x, w = r->w;
    if (w < 16 || r->h < 5)
        return;

    char spl[56], sppt[56], fppt[56], gclk[56], cfq[56];
    char cb[32], lim[48], modev[80];
    char rspl[24], rsppt[24], rfppt[24], rcfq[32];
    pw_refresh_limits();
    snprintf(rspl,  sizeof(rspl),  "%d–%d", pw_smin, pw_smax);
    snprintf(rsppt, sizeof(rsppt), "%d–%d", pw_pmin, pw_pmax);
    snprintf(rfppt, sizeof(rfppt), "%d–%d", pw_fmin, pw_fmax);
    if (hw->cpu_mhz_max > 0)
        snprintf(rcfq, sizeof(rcfq), "%d–%d", hw->cpu_mhz_min, hw->cpu_mhz_max);
    else
        rcfq[0] = '\0';

    fmt_val_int(spl,  sizeof(spl),  hw->ppt_spl,  g_ui.pwv_spl,  " W",  rspl);
    fmt_val_int(sppt, sizeof(sppt), hw->ppt_sppt, g_ui.pwv_sppt, " W", rsppt);
    fmt_val_int(fppt, sizeof(fppt), hw->ppt_fppt, g_ui.pwv_fppt, " W", rfppt);
    {
        char lg[24], sg[24];
        if (hw->gpu_clock_lock > 0) snprintf(lg, sizeof(lg), "%d MHz", hw->gpu_clock_lock);
        else snprintf(lg, sizeof(lg), "driver default");
        if (g_ui.pwv_gpuclock > 0) snprintf(sg, sizeof(sg), "%d MHz", g_ui.pwv_gpuclock);
        else snprintf(sg, sizeof(sg), "driver default");
        fmt_val_enum(gclk, sizeof(gclk), lg, sg);
    }
    fmt_val_int(cfq, sizeof(cfq), hw->cpu_mhz_limit, g_ui.pwv_mhz, " MHz", rcfq);
    {
        static int fw_badge;
        if (!fw_badge && hw->cpu_temp >= PW_FW_TEMP_ON) {
            fw_badge = 1;
            ut_log("cpu ceiling: firmware-managed at %d °C — oscillation expected",
                   hw->cpu_temp);
        } else if (fw_badge && hw->cpu_temp > 0 && hw->cpu_temp <= PW_FW_TEMP_OFF) {
            fw_badge = 0;
        }
        if (fw_badge) {
            size_t cl = strlen(cfq);
            snprintf(cfq + cl, sizeof(cfq) - cl, " · firmware");
        }
    }
    fmt_val_bool(cb, sizeof(cb), hw->cpu_boost, g_ui.pwv_cpuboost);
    if (g_ui.pwv_ppt_off != hw->ppt_off)
        snprintf(lim, sizeof(lim), "%s → %s ●",
                 hw->ppt_off ? "removed (max)" : "on",
                 g_ui.pwv_ppt_off ? "removed (max)" : "on");
    else
        snprintf(lim, sizeof(lim), "%s", hw->ppt_off ? "removed (max)" : "on");

    char prof[64], epp[64];
    fmt_val_enum(prof, sizeof(prof), hw_profile_name(hw->profile),
                 hw_profile_name((hw_profile_t)g_ui.pwv_profile));
    {
        static int prev_conflict = 0, prev_enf = -1;
        int enf = hw_asusd_enforced(hw);
        bool conflict = enf >= 0 && enf != (int)hw->profile;
        if (conflict) {
            char mark[40];
            if (g_ui.pwv_profile == (int)hw->profile)
                snprintf(mark, sizeof(mark), " ⚠asusd: %s",
                         hw_profile_name((hw_profile_t)enf));
            else
                snprintf(mark, sizeof(mark), " ⚠asusd");
            size_t pl = strlen(prof);
            snprintf(prof + pl, sizeof(prof) - pl, "%s", mark);
            if (!prev_conflict || prev_enf != enf)
                ut_log("asusd %s takeover: %s will return on next power event",
                       hw->ac_online ? "AC" : "battery",
                       hw_profile_name((hw_profile_t)enf));
        }
        prev_conflict = conflict;
        prev_enf = conflict ? enf : -1;
    }

    char acv[48], bav[48];
    if (hw->asusd_ac == -2)
        snprintf(acv, sizeof(acv), "-- (asusd absent)");
    else
        fmt_val_enum(acv, sizeof(acv), pw_asusd_name(hw->asusd_ac),
                     pw_asusd_name(g_ui.pwv_asusd_ac));
    if (hw->asusd_bat == -2)
        snprintf(bav, sizeof(bav), "-- (asusd absent)");
    else
        fmt_val_enum(bav, sizeof(bav), pw_asusd_name(hw->asusd_bat),
                     pw_asusd_name(g_ui.pwv_asusd_bat));
    fmt_val_enum(epp, sizeof(epp), hw_epp_name(hw->epp),
                 hw_epp_name((hw_epp_t)g_ui.pwv_epp));
    pw_mode_val(modev, sizeof(modev));

    int armed = pw_preset_match(g_ui.pwv_spl, g_ui.pwv_sppt, g_ui.pwv_fppt);

    const char *vals[PW_ROWS] = {
        prof, acv, bav, epp, modev,
        "", spl, sppt, fppt, lim,
        gclk, cb, cfq,
    };
    static const char *const labels[PW_ROWS] = {
        "Platform profile", "AC auto-profile", "Battery auto-profile",
        "EPP preference", "Quick mode",
        "Presets", "SPL (sustained)", "SPPT (slow boost)", "FPPT (fast boost)",
        "PPT limits",
        "GPU clock limit", "CPU boost", "CPU clock limit",
    };

    char sum[160];
    pw_diff_summary(sum, sizeof(sum));
    int y0 = r->y;
    if (g_ui.pw_typing) {
        char disp[64], line[144], rng[32];
        tin_format(&g_ui.pw_input, disp, sizeof(disp), true);
        pw_row_range(g_ui.pw_sel, rng, sizeof(rng));
        if (rng[0])
            snprintf(line, sizeof(line), "edit %s (%s): %s   (Enter stage · Esc cancel)",
                     labels[g_ui.pw_sel], rng, disp);
        else
            snprintf(line, sizeof(line), "edit %s: %s   (Enter stage · Esc cancel)",
                     labels[g_ui.pw_sel], disp);
        ui_putln(n, x, y0, w, line, pal->accent, true);
        y0 += 2;
    }

    int foot_h = sum[0] ? 2 : 0;
    int budget = (r->y + r->h) - y0 - foot_h;
    if (budget < 1)
        budget = 1;
    pw_view_clamp(budget);
    int top_line = pw_slot(g_ui.pw_top) - (pw_group_name(g_ui.pw_top) ? 1 : 0);

    int ln = 0;
    for (int i = g_ui.pw_top; i < PW_ROWS && ln < budget; i++) {
        int y = y0 + ln;
        const char *gn = pw_group_name(i);
        if (gn) {
            draw_group_hdr(n, x, y, w, gn);
            ln++;
            if (ln >= budget)
                break;
            y = y0 + ln;
        }
        bool sel = !g_ui.pw_in_cores && (i == g_ui.pw_sel);
        if (i == PW_PRESET) {
            int lw = w * 42 / 100;
            if (lw < 18) lw = 18;
            if (lw > 28) lw = 28;
            char plab[32];
            snprintf(plab, sizeof(plab), "%s%s", sel ? "▸ " : "  ", labels[i]);
            ui_putln(n, x, y, lw, plab, sel ? pal->accent : pal->text, sel);
            ui_btndef_t btns[3];
            for (int b = 0; b < 3; b++)
                btns[b] = (ui_btndef_t){ PW_PRESET_NAMES[b], b == armed,
                                         TGT(TGT_PANEL_WORKSPACE,
                                             ACT_WS_PW_PRESET_BASE + b) };
            int cx = ui_btn_row(n, y, x + lw, w - lw, btns, 3);
            if (armed < 0 && cx + 7 <= x + w)
                ui_putln(n, cx, y, 7, "custom", pal->muted, false);
            tgt_register(x, y, lw, 1, TGT(TGT_PANEL_WORKSPACE, ACT_WS_PW_BASE + i));
        } else {
            ui_row(n, x, y, w, labels[i], vals[i], sel);
            tgt_register(x, y, w, 1, TGT(TGT_PANEL_WORKSPACE, ACT_WS_PW_BASE + i));
        }
        ln++;
    }

    bool more_below = top_line + budget - 1 < PW_ROWS + PW_GROUPS_N - 1;
    if (g_ui.pw_top > 0 || more_below) {
        char mk[8] = "";
        if (g_ui.pw_top > 0) strcat(mk, "▲");
        if (more_below) strcat(mk, "▼");
        ui_putln(n, x + w - 2, r->y, 3, mk, pal->muted, false);
    }

    if (sum[0] && r->h >= 5) {
        int py = r->y + r->h - 1;
        char prev[176];
        snprintf(prev, sizeof(prev), "will write: %s", sum);
        ui_putln(n, x, py, w, prev, pal->warn, false);
    }
}

static void draw_power(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    const hw_state_t *hw = g_ui.hw;

    if (hw->cpu_n <= 0) {
        draw_power_list(n, r);
        return;
    }

    if (r->w >= 89) {
        /* wide terminal: Power list left + hairline + inline CORE LIMITS right */
        int lw = ut_clamp_i(r->w * 54 / 100, 50, 62);
        rect_t pr = { r->x, r->y, lw, r->h };
        rect_t cr = { r->x + lw + 3, r->y, r->w - lw - 3, r->h };
        draw_power_list(n, &pr);
        ui_vline(n, r->x + lw + 1, r->y, r->h, pal->border);
        editor_corefreq_draw(n, &cr);
    } else if (r->h >= 28) {
        /* narrower but tall: Power list top + inline CORE LIMITS below */
        int ph = 18;
        rect_t pr = { r->x, r->y, r->w, ph };
        rect_t cr = { r->x, r->y + ph + 1, r->w, r->h - ph - 1 };
        draw_power_list(n, &pr);
        ui_hline(n, r->x, r->y + ph, r->w, pal->border);
        editor_corefreq_draw(n, &cr);
    } else if (g_ui.pw_in_cores) {
        /* tiny terminal: focused pane fills the workspace cleanly */
        editor_corefreq_draw(n, r);
    } else {
        draw_power_list(n, r);
    }
}

/* =========================================================================
 *  VIEW 3: DISPLAY, BATTERY & LIGHTING (VIEW_SYSTEM)
 * ========================================================================= */

enum {
    SYS_HZ = 0,
    SYS_PANEL_OD,
    SYS_BAT,
    SYS_BAT_ONESHOT,
    SYS_KBD,
    SYS_EFFECT,
    SYS_COLOR,
    SYS_HEX,
    SYS_DB_STATUS,
    SYS_DB_RELOAD,
    SYS_DB_EDITOR,
    SYS_DB_INSTALL,
    SYS_ROWS
};

static const struct { int first; const char *name; } SYS_GROUPS[] = {
    { SYS_HZ,        "DISPLAY" },
    { SYS_BAT,       "BATTERY" },
    { SYS_KBD,       "LIGHTING" },
    { SYS_DB_STATUS, "DAEBOARD (LED DAEMON)" },
};
#define SYS_GROUPS_N ((int)(sizeof(SYS_GROUPS) / sizeof(SYS_GROUPS[0])))

static const char *const SYS_DESCS[SYS_ROWS] = {
    "panel refresh rate via compositor backend (read-back verified)",
    "faster LCD pixel response (panel_od); costs a little battery",
    "battery charge ceiling (20–100 %) — h/l ±5 %, e exact, w applies",
    "charge to 100 % once until next plug cycle (Enter triggers immediately)",
    "keyboard backlight level (asus::kbd_backlight)",
    "single-zone AURA keyboard effect (applied on w / Enter)",
    "single-zone AURA keyboard preset color (applied on w / Enter)",
    "custom 6-digit hex color — press e/x to type, Enter applies",
    "optional daeboard LED daemon — Enter starts or stops it",
    "recompile ~/.config/ctron/daeboard.binds without restarting daemon",
    "open interactive daeboard key-binds editor (shortcut: b)",
    "install daeboard daemon helper",
};

static const char *sys_group_name(int row)
{
    for (int g = 0; g < SYS_GROUPS_N; g++)
        if (SYS_GROUPS[g].first == row)
            return SYS_GROUPS[g].name;
    return NULL;
}

static int sys_slot(int row)
{
    int s = row;
    for (int g = 0; g < SYS_GROUPS_N; g++)
        if (SYS_GROUPS[g].first <= row)
            s++;
    return s;
}

static void sys_view_clamp(int budget)
{
    int total = SYS_ROWS + SYS_GROUPS_N;
    if (budget >= total) {
        g_ui.sys_top = 0;
        return;
    }
    int top = (g_ui.sys_top >= 0 && g_ui.sys_top < SYS_ROWS) ? g_ui.sys_top : 0;
    int sel = sys_slot(g_ui.sys_sel);
    for (int guard = 0; guard < SYS_ROWS + 2; guard++) {
        int tl = sys_slot(top) - (sys_group_name(top) ? 1 : 0);
        if (sel < tl) top = g_ui.sys_sel;
        else if (sel > tl + budget - 1) top++;
        else break;
    }
    for (int t = SYS_ROWS - 1; t >= 0; t--) {
        int tl = sys_slot(t) - (sys_group_name(t) ? 1 : 0);
        if (tl + budget - 1 >= total - 1 || t == 0) {
            if (top > t) top = t;
            break;
        }
    }
    g_ui.sys_top = top;
}

enum {
    SYS_W_HZ       = 1 << 0,
    SYS_W_PANEL_OD = 1 << 1,
    SYS_W_BAT      = 1 << 2,
    SYS_W_KBD      = 1 << 3,
    SYS_W_AURA     = 1 << 4,
};

static unsigned sys_write_mask(const hw_state_t *hw)
{
    unsigned m = 0;
    if (hw->hz_count > 0 && (g_ui.sys_touched & SYS_T_HZ)) {
        int idx = ut_clamp_i(g_ui.sys_hz_idx, 0, hw->hz_count - 1);
        if (hw->hz_modes[idx] != hw->hz_cur)
            m |= SYS_W_HZ;
    }
    if (g_ui.sys_panel_od != hw->panel_od)
        m |= SYS_W_PANEL_OD;
    if ((g_ui.sys_touched & SYS_T_BAT) ||
        (hw->bat_limit > 0 && g_ui.sys_bat != hw->bat_limit))
        m |= SYS_W_BAT;
    if (g_ui.sys_kbd_idx != (int)hw->kbd)
        m |= SYS_W_KBD;
    if (g_ui.sys_touched & SYS_T_AURA)
        m |= SYS_W_AURA;
    return m;
}

void sys_recompute_dirty(void)
{
    g_ui.sys_dirty = sys_write_mask(g_ui.hw) != 0;
}

int sys_staged_count(void)
{
    return popcount_u(sys_write_mask(g_ui.hw));
}

const char *sys_selected_desc(void)
{
    return SYS_DESCS[ut_clamp_i(g_ui.sys_sel, 0, SYS_ROWS - 1)];
}

void sys_sync_from_hw(void)
{
    const hw_state_t *hw = g_ui.hw;
    g_ui.sys_hz_idx = 0;
    for (int i = 1; i < hw->hz_count; i++) {
        if (hw->hz_cur > 0 &&
            abs(hw->hz_modes[i] - hw->hz_cur) <
                abs(hw->hz_modes[g_ui.sys_hz_idx] - hw->hz_cur))
            g_ui.sys_hz_idx = i;
    }
    g_ui.sys_panel_od = hw->panel_od;
    g_ui.sys_bat = hw->bat_limit > 0 ? hw->bat_limit : 80;
    g_ui.sys_kbd_idx = (int)hw->kbd;
    g_ui.sys_touched = 0;
    sys_recompute_dirty();
}

static void sys_diff_summary(char *out, size_t n)
{
    const hw_state_t *hw = g_ui.hw;
    unsigned wm = sys_write_mask(hw);
    char e[48];
    int off = 0;
    out[0] = '\0';

    if (wm & SYS_W_HZ && hw->hz_count > 0) {
        int hz = hw->hz_modes[ut_clamp_i(g_ui.sys_hz_idx, 0, hw->hz_count - 1)];
        snprintf(e, sizeof(e), "refresh %d Hz", hz);
        sum_app(out, n, &off, e);
    }
    if (wm & SYS_W_PANEL_OD)
        sum_app(out, n, &off, g_ui.sys_panel_od ? "panel OD on" : "panel OD off");
    if (wm & SYS_W_BAT) {
        snprintf(e, sizeof(e), "battery %d%%", g_ui.sys_bat);
        sum_app(out, n, &off, e);
    }
    if (wm & SYS_W_KBD) {
        static const char *const KBDS[] = { "off", "low", "med", "high" };
        snprintf(e, sizeof(e), "kbd %s", KBDS[ut_clamp_i(g_ui.sys_kbd_idx, 0, 3)]);
        sum_app(out, n, &off, e);
    }
    if (wm & SYS_W_AURA) {
        snprintf(e, sizeof(e), "aura %s %s",
                 AURA_EFFECTS[ut_clamp_i(g_ui.lt_eff, 0, AURA_EFFECT_COUNT - 1)],
                 AURA_COLOR_NAMES[ut_clamp_i(g_ui.lt_col, 0, AURA_COLOR_COUNT - 1)]);
        sum_app(out, n, &off, e);
    }
}

void sys_apply_writes(hw_state_t *hw, int *fails_out, char *sum, size_t sumn)
{
    int fails = 0;
    sys_diff_summary(sum, sumn);
    ut_progress("system: applying");

    if (sys_write_mask(hw) & SYS_W_HZ && hw->hz_count > 0) {
        int hz = hw->hz_modes[ut_clamp_i(g_ui.sys_hz_idx, 0, hw->hz_count - 1)];
        if (ctrl_set_hz(hw, hz) != 0)
            fails++;
    }
    if (sys_write_mask(hw) & SYS_W_PANEL_OD) {
        if (ctrl_set_panel_od(hw, g_ui.sys_panel_od) != 0)
            fails++;
    }
    if (sys_write_mask(hw) & SYS_W_BAT) {
        if (ctrl_set_battery_limit(hw, g_ui.sys_bat) != 0)
            fails++;
    }
    if (sys_write_mask(hw) & SYS_W_KBD) {
        if (ctrl_set_kbd(hw, (hw_kbd_t)ut_clamp_i(g_ui.sys_kbd_idx, 0, 3)) != 0)
            fails++;
    }
    if (sys_write_mask(hw) & SYS_W_AURA) {
        if (ctrl_set_aura(hw, g_ui.lt_eff, g_ui.lt_col) != 0)
            fails++;
    }
    hw_refresh_live(hw);
    *fails_out = fails;
}

static void sys_nudge_row(int row, int dir)
{
    hw_state_t *hw = g_ui.hw;
    g_ui.pw_msg_ms = 0;
    switch (row) {
    case SYS_HZ:
        if (hw->hz_count > 0) {
            g_ui.sys_hz_idx = (g_ui.sys_hz_idx +
                               (dir > 0 ? 1 : hw->hz_count - 1)) % hw->hz_count;
            g_ui.sys_touched |= SYS_T_HZ;
        }
        break;
    case SYS_PANEL_OD:
        g_ui.sys_panel_od = !g_ui.sys_panel_od;
        g_ui.sys_touched |= SYS_T_PANEL_OD;
        break;
    case SYS_BAT:
        g_ui.sys_bat = ut_clamp_i(g_ui.sys_bat + 5 * dir, 20, 100);
        g_ui.sys_touched |= SYS_T_BAT;
        break;
    case SYS_KBD:
        g_ui.sys_kbd_idx = (g_ui.sys_kbd_idx + (dir > 0 ? 1 : 3)) % 4;
        g_ui.sys_touched |= SYS_T_KBD;
        break;
    case SYS_EFFECT:
        g_ui.lt_eff = (g_ui.lt_eff + (dir > 0 ? 1 : AURA_EFFECT_COUNT - 1)) % AURA_EFFECT_COUNT;
        g_ui.sys_touched |= SYS_T_AURA;
        break;
    case SYS_COLOR:
        g_ui.lt_col = (g_ui.lt_col + (dir > 0 ? 1 : AURA_COLOR_COUNT - 1)) % AURA_COLOR_COUNT;
        g_ui.sys_touched |= SYS_T_AURA;
        break;
    default:
        return;
    }
    sys_recompute_dirty();
}

static void sys_action_row(int row)
{
    hw_state_t *hw = g_ui.hw;
    switch (row) {
    case SYS_BAT_ONESHOT:
        if (ctrl_battery_oneshot() == 0)
            ut_log("battery: one-shot 100%% charge armed");
        else
            ut_log("battery: one-shot FAILED");
        break;
    case SYS_HEX:
        if (strlen(g_ui.lt_hex.buf) == 6)
            ctrl_set_aura_hex(hw, g_ui.lt_hex.buf);
        else
            ut_log("hex: need 6 digits (press e to edit)");
        break;
    case SYS_DB_STATUS:
        if (db_up_cached(1)) {
            if (db_quit() != 0)
                ut_log("daeboard: stop failed");
        } else {
            if (db_start() != 0)
                ut_log("daeboard: start failed");
        }
        break;
    case SYS_DB_RELOAD: {
        char err[64];
        if (db_reload(err, (int)sizeof(err)) != 0)
            ut_log("daeboard: %s", err[0] ? err : "reload failed");
        else
            ut_log("daeboard: binds reloaded");
        break;
    }
    case SYS_DB_EDITOR:
        editor_daeboard_open();
        break;
    case SYS_DB_INSTALL:
        if (db_install() != 0)
            ut_log("daeboard: install failed");
        break;
    default:
        if (sys_staged_count() > 0)
            ui_spawn_apply(APPLY_SYSTEM, "display & lighting", "");
        break;
    }
}

static void draw_system(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    const hw_state_t *hw = g_ui.hw;
    int x = r->x, w = r->w;
    if (w < 16 || r->h < 5)
        return;

    static const char *const KBDS[] = { "off", "low", "med", "high" };
    char hzv[48], odv[32], batv[48], kbdv[48], effv[48], colv[48], hexv[144], note[160];

    if (hw->hz_count == 0) {
        snprintf(hzv, sizeof(hzv), "-- (no display backend)");
    } else {
        int idx = ut_clamp_i(g_ui.sys_hz_idx, 0, hw->hz_count - 1);
        char lh[16], sh[16];
        if (hw->hz_cur > 0) snprintf(lh, sizeof(lh), "%d Hz", hw->hz_cur);
        else snprintf(lh, sizeof(lh), "--");
        snprintf(sh, sizeof(sh), "%d Hz", hw->hz_modes[idx]);
        if (hw->hz_cur > 0 && hw->hz_modes[idx] != hw->hz_cur)
            snprintf(hzv, sizeof(hzv), "%s → %s ●", lh, sh);
        else
            snprintf(hzv, sizeof(hzv), "%s", sh);
    }
    fmt_val_bool(odv, sizeof(odv), hw->panel_od, g_ui.sys_panel_od);
    fmt_val_int(batv, sizeof(batv), hw->bat_limit, g_ui.sys_bat, "%", "20–100");
    fmt_val_enum(kbdv, sizeof(kbdv),
                 KBDS[ut_clamp_i((int)hw->kbd, 0, 3)],
                 KBDS[ut_clamp_i(g_ui.sys_kbd_idx, 0, 3)]);
    snprintf(effv, sizeof(effv), "%s%s",
             AURA_EFFECTS[ut_clamp_i(g_ui.lt_eff, 0, AURA_EFFECT_COUNT - 1)],
             (g_ui.sys_touched & SYS_T_AURA) ? " ●" : "");
    snprintf(colv, sizeof(colv), "%s%s",
             AURA_COLOR_NAMES[ut_clamp_i(g_ui.lt_col, 0, AURA_COLOR_COUNT - 1)],
             (g_ui.sys_touched & SYS_T_AURA) ? " ●" : "");
    {
        char disp[96];
        tin_format(&g_ui.lt_hex, disp, sizeof(disp), g_ui.lt_hex_active != 0);
        if (g_ui.lt_hex_active)
            snprintf(hexv, sizeof(hexv), "#%s  (Enter applies · Esc cancels)", disp);
        else
            snprintf(hexv, sizeof(hexv), "#%s  (e to edit)", disp);
    }
    int up = db_up_cached(0);
    db_install_note(note, (int)sizeof(note));

    const char *vals[SYS_ROWS] = {
        hzv, odv,
        batv, "Enter to charge 100% once",
        kbdv, effv, colv, hexv,
        up ? "running (Enter stops)" : "stopped (Enter starts)",
        "daeboard.binds",
        "open editor (b)",
        note,
    };
    static const char *const labels[SYS_ROWS] = {
        "Refresh rate", "Panel overdrive",
        "Charge limit", "Charge one-shot",
        "Kbd brightness", "Aura effect", "Aura color", "Custom hex",
        "Daemon status", "Reload binds", "Binds editor", "Install",
    };

    char sum[160];
    sys_diff_summary(sum, sizeof(sum));
    int y0 = r->y;
    if (g_ui.sys_bat_typing) {
        char disp[32], line[128];
        tin_format(&g_ui.sys_bat_input, disp, sizeof(disp), true);
        snprintf(line, sizeof(line), "edit Charge limit (20–100): %s   (Enter stage · Esc cancel)",
                 disp);
        ui_putln(n, x, y0, w, line, pal->accent, true);
        y0 += 2;
    }

    int foot_h = sum[0] ? 2 : 0;
    int budget = (r->y + r->h) - y0 - foot_h;
    if (budget < 1)
        budget = 1;
    sys_view_clamp(budget);
    int top_line = sys_slot(g_ui.sys_top) - (sys_group_name(g_ui.sys_top) ? 1 : 0);

    int hex_row_y = -1;
    int ln = 0;
    for (int i = g_ui.sys_top; i < SYS_ROWS && ln < budget; i++) {
        int y = y0 + ln;
        const char *gn = sys_group_name(i);
        if (gn) {
            draw_group_hdr(n, x, y, w, gn);
            ln++;
            if (ln >= budget)
                break;
            y = y0 + ln;
        }
        bool sel = (i == g_ui.sys_sel);
        ui_row(n, x, y, w, labels[i], vals[i], sel);
        tgt_register(x, y, w, 1, TGT(TGT_PANEL_WORKSPACE, ACT_WS_SYS_BASE + i));
        if (i == SYS_HEX)
            hex_row_y = y;
        ln++;
    }

    /* hex color swatch right-aligned on the Custom hex row */
    if (hex_row_y >= 0 && w >= 42) {
        int sw_x = x + w - 7;
        unsigned int hr = 0, hg = 0, hb = 0;
        if (strlen(g_ui.lt_hex.buf) == 6)
            sscanf(g_ui.lt_hex.buf, "%02x%02x%02x", &hr, &hg, &hb);
        uint64_t ch = ui_ch((hr << 16) | (hg << 8) | hb, g_ui.sys_sel == SYS_HEX);
        ncplane_set_channels(n, ch);
        ncplane_putstr_yx(n, hex_row_y, sw_x, "██████");
    }

    bool more_below = top_line + budget - 1 < SYS_ROWS + SYS_GROUPS_N - 1;
    if (g_ui.sys_top > 0 || more_below) {
        char mk[8] = "";
        if (g_ui.sys_top > 0) strcat(mk, "▲");
        if (more_below) strcat(mk, "▼");
        ui_putln(n, x + w - 2, r->y, 3, mk, pal->muted, false);
    }

    if (sum[0] && r->h >= 5) {
        int py = r->y + r->h - 1;
        char prev[176];
        snprintf(prev, sizeof(prev), "will write: %s", sum);
        ui_putln(n, x, py, w, prev, pal->warn, false);
    }
}

/* =========================================================================
 *  WORKSPACE DISPATCHER (VIEW_POWER, VIEW_FANS, VIEW_SYSTEM)
 * ========================================================================= */

void panel_workspace_draw(struct ncplane *n, const rect_t *r)
{
    switch (g_ui.view) {
    case VIEW_POWER:  draw_power(n, r); break;
    case VIEW_FANS:   editor_fan_draw(n, r); break;
    case VIEW_SYSTEM: draw_system(n, r); break;
    default: break;
    }
}

void panel_workspace_key(uint32_t key)
{
    if (g_ui.view == VIEW_FANS) {
        editor_fan_key(key);
        return;
    }

    if (g_ui.view == VIEW_POWER) {
        if (g_ui.pw_in_cores) {
            editor_corefreq_key(key);
            return;
        }
        if (g_ui.pw_typing) {
            if (key == NCKEY_ESC) {
                g_ui.pw_typing = false;
                return;
            }
            if (key == NCKEY_ENTER || key == '\r' || key == '\n') {
                g_ui.pw_typing = false;
                if (g_ui.pw_input.buf[0])
                    pw_commit_typed();
                return;
            }
            tin_key(&g_ui.pw_input, key);
            return;
        }
        switch (key) {
        case 'j': case NCKEY_DOWN: g_ui.pw_sel = (g_ui.pw_sel + 1) % PW_ROWS; return;
        case 'k': case NCKEY_UP:   g_ui.pw_sel = (g_ui.pw_sel + PW_ROWS - 1) % PW_ROWS; return;
        case 'h': case NCKEY_LEFT: pw_nudge_row(g_ui.pw_sel, -1); return;
        case 'l': case NCKEY_RIGHT: pw_nudge_row(g_ui.pw_sel, +1); return;
        case 'w': case 'W':
            ui_spawn_apply(APPLY_POWER, "power settings", "");
            return;
        case NCKEY_ENTER: case '\r': case '\n': case ' ':
            if (g_ui.pw_sel == PW_MODE) {
                if (g_ui.mode_n > 0)
                    ui_spawn_apply(APPLY_MODE, "mode",
                                   g_ui.modes[ut_clamp_i(g_ui.ctl_mode_idx, 0,
                                                         g_ui.mode_n - 1)].name);
            } else if (g_ui.pw_sel == PW_CPUFREQ && !g_ui.pw_dirty) {
                editor_corefreq_open();
            } else if (g_ui.pw_sel == PW_CPUBOOST && key == ' ') {
                pw_nudge_row(PW_CPUBOOST, +1);
            } else {
                ui_spawn_apply(APPLY_POWER, "power settings", "");
            }
            return;
        case 'r': case 'R':
            pw_sync_from_hw();
            cf_reset_staging();
            return;
        case 'c': case 'C':
            editor_corefreq_open();
            return;
        case 'e': case 'E':
            if (pw_row_numeric(g_ui.pw_sel)) {
                char seed[16];
                snprintf(seed, sizeof(seed), "%d", pw_staged_value(g_ui.pw_sel));
                tin_set(&g_ui.pw_input, seed);
                g_ui.pw_typing = true;
                g_ui.pw_msg_ms = 0;
            }
            return;
        default:
            return;
        }
    }

    if (g_ui.view == VIEW_SYSTEM) {
        if (g_ui.lt_hex_active) {
            if (key == NCKEY_ESC) {
                g_ui.lt_hex_active = 0;
                return;
            }
            if (key == NCKEY_ENTER || key == '\r' || key == '\n') {
                g_ui.lt_hex_active = 0;
                sys_action_row(SYS_HEX);
                return;
            }
            tin_key(&g_ui.lt_hex, key);
            return;
        }
        if (g_ui.sys_bat_typing) {
            if (key == NCKEY_ESC) {
                g_ui.sys_bat_typing = false;
                return;
            }
            if (key == NCKEY_ENTER || key == '\r' || key == '\n') {
                g_ui.sys_bat_typing = false;
                if (g_ui.sys_bat_input.buf[0]) {
                    g_ui.sys_bat = ut_clamp_i(atoi(g_ui.sys_bat_input.buf), 20, 100);
                    g_ui.sys_touched |= SYS_T_BAT;
                    sys_recompute_dirty();
                }
                return;
            }
            if ((key >= '0' && key <= '9') || tin_is_edit_key(key))
                tin_key(&g_ui.sys_bat_input, key);
            return;
        }
        switch (key) {
        case 'j': case NCKEY_DOWN: g_ui.sys_sel = (g_ui.sys_sel + 1) % SYS_ROWS; return;
        case 'k': case NCKEY_UP:   g_ui.sys_sel = (g_ui.sys_sel + SYS_ROWS - 1) % SYS_ROWS; return;
        case 'h': case NCKEY_LEFT: sys_nudge_row(g_ui.sys_sel, -1); return;
        case 'l': case NCKEY_RIGHT: sys_nudge_row(g_ui.sys_sel, +1); return;
        case ' ':
            if (g_ui.sys_sel == SYS_PANEL_OD)
                sys_nudge_row(SYS_PANEL_OD, +1);
            else
                sys_action_row(g_ui.sys_sel);
            return;
        case 'w': case 'W':
            if (sys_staged_count() > 0)
                ui_spawn_apply(APPLY_SYSTEM, "display & lighting", "");
            else
                ut_log("system: nothing staged");
            return;
        case 'r': case 'R':
            sys_sync_from_hw();
            return;
        case 'e': case 'E':
            if (g_ui.sys_sel == SYS_HEX) {
                g_ui.lt_hex_active = 1;
            } else if (g_ui.sys_sel == SYS_BAT) {
                char seed[16];
                snprintf(seed, sizeof(seed), "%d", g_ui.sys_bat);
                tin_set(&g_ui.sys_bat_input, seed);
                g_ui.sys_bat_typing = true;
            }
            return;
        case 'x': case 'X':
            g_ui.sys_sel = SYS_HEX;
            g_ui.lt_hex_active = 1;
            tin_clear(&g_ui.lt_hex);
            return;
        case 'b': case 'B':
            editor_daeboard_open();
            return;
        case NCKEY_ENTER: case '\r': case '\n':
            sys_action_row(g_ui.sys_sel);
            return;
        default:
            return;
        }
    }
}

void panel_workspace_act(int id)
{
    g_ui.pw_typing = false;
    g_ui.sys_bat_typing = false;

    if (id >= ACT_FE_BASE && id < ACT_FE_END) {
        if (g_ui.view == VIEW_FANS)
            editor_fan_act(id, 0, 0);
        return;
    }
    if (id >= ACT_WS_PW_PRESET_BASE && id < ACT_WS_PW_PRESET_BASE + 3) {
        if (g_ui.view == VIEW_POWER) {
            g_ui.pw_in_cores = false;
            pw_stage_preset_idx(id - ACT_WS_PW_PRESET_BASE);
        }
        return;
    }
    if (id >= ACT_WS_PW_BASE && id < ACT_WS_PW_BASE + PW_ROWS) {
        int row = id - ACT_WS_PW_BASE;
        bool was_cores = g_ui.pw_in_cores;
        g_ui.pw_in_cores = false;
        if (!was_cores && row == g_ui.pw_sel) {
            if (row == PW_MODE && g_ui.mode_n > 0)
                ui_spawn_apply(APPLY_MODE, "mode",
                               g_ui.modes[ut_clamp_i(g_ui.ctl_mode_idx, 0,
                                                     g_ui.mode_n - 1)].name);
            else
                pw_nudge_row(row, +1);
        } else {
            g_ui.pw_sel = row;
        }
        return;
    }
    if (id >= ACT_WS_SYS_BASE && id < ACT_WS_SYS_BASE + SYS_ROWS) {
        int row = id - ACT_WS_SYS_BASE;
        if (row == g_ui.sys_sel) {
            if (row == SYS_HEX)
                g_ui.lt_hex_active = 1;
            else if (row == SYS_BAT_ONESHOT || row >= SYS_DB_STATUS)
                sys_action_row(row);
            else
                sys_nudge_row(row, +1);
        } else {
            g_ui.sys_sel = row;
        }
        return;
    }
}
