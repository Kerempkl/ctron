#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ui_internal.h"
#include "../cmds.h"
#include "../control.h"
#include "../util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* View 4: SNAPSHOTS — Two-pane Konsept C workspace:
 *   Left pane  : Saved .ctr snapshots ("group/name") with live diff badges
 *                (≡ live / Δ N). Enter applies, e/l opens field editor,
 *                s/n saves fresh live state, uu overwrites selected from live,
 *                r renames, c edits note, dd deletes with confirmation.
 *   Right pane : Live Inspector + Interactive Field Editor (12 fields).
 *                Any field can be stepped (h/l), typed (e), or toggled to
 *                "— (skip)" (x/Del) for selective/partial snapshots. */

enum {
    ACT_PL_ITEM     = 1,   /* + snapshot index (0..MAX_PROFILES-1) */
    ACT_PL_FIELD    = 200, /* + editor field index (0..SF_COUNT-1) */
    ACT_PL_BTN_EDIT = 300,
    ACT_PL_BTN_SAVE = 301,
    ACT_PL_BTN_SKIP = 302,
    ACT_PL_BTN_CURV = 303,
    ACT_PL_BTN_BACK = 304,
};

enum { PL_INPUT_NONE = 0, PL_INPUT_SAVE, PL_INPUT_RENAME, PL_INPUT_NOTE };

enum {
    SF_PROFILE = 0,
    SF_EPP,
    SF_BOOST,
    SF_FREQ,
    SF_PPT,
    SF_GPU_CLOCK,
    SF_HZ,
    SF_PANEL_OD,
    SF_BAT,
    SF_KBD,
    SF_FAN_CPU,
    SF_FAN_GPU,
    SF_COUNT
};

static const char *const SF_LABELS[SF_COUNT] = {
    "Platform profile",
    "Energy perf (EPP)",
    "CPU boost",
    "CPU ceiling",
    "PPT (SPL/SPPT/FPPT)",
    "GPU clock lock",
    "Refresh rate",
    "Panel overdrive",
    "Battery limit",
    "Keyboard light",
    "CPU fan curve",
    "GPU fan curve",
};

/* Cached parsed metadata for every listed snapshot — loaded once on list
 * reload, never read from disk inside panel_profiles_draw(). */
static profile_meta_t s_meta[MAX_PROFILES];
static bool s_meta_ok[MAX_PROFILES];

static void sync_edit_from_sel(void)
{
    g_ui.prof_edit_dirty = false;
    g_ui.prof_val_typing = false;
    if (g_ui.prof_n > 0 && g_ui.prof_sel >= 0 && g_ui.prof_sel < g_ui.prof_n &&
        s_meta_ok[g_ui.prof_sel]) {
        g_ui.prof_meta = s_meta[g_ui.prof_sel];
        g_ui.prof_meta_ok = true;
        g_ui.prof_meta_sel = g_ui.prof_sel;
        g_ui.prof_edit = s_meta[g_ui.prof_sel];
    } else {
        profile_meta_init(&g_ui.prof_meta);
        profile_meta_init(&g_ui.prof_edit);
        g_ui.prof_meta_ok = false;
        g_ui.prof_meta_sel = g_ui.prof_sel;
    }
}

void panel_profiles_reload(void)
{
    g_ui.prof_n = profile_list(g_ui.profs, MAX_PROFILES);
    if (g_ui.prof_n <= 0)
        g_ui.prof_sel = 0;
    else if (g_ui.prof_sel >= g_ui.prof_n)
        g_ui.prof_sel = g_ui.prof_n - 1;
    for (int i = 0; i < g_ui.prof_n; i++)
        s_meta_ok[i] = profile_meta_parse(g_ui.profs[i], &s_meta[i]) == 0;
    g_ui.prof_meta_sel = -1;
    sync_edit_from_sel();
}

static void select_snapshot(int idx)
{
    if (idx < 0 || idx >= g_ui.prof_n)
        return;
    if (idx != g_ui.prof_sel || g_ui.prof_meta_sel != idx) {
        g_ui.prof_sel = idx;
        sync_edit_from_sel();
    }
}

/* ---- field inspection & formatting -------------------------------------- */

static bool fan_csv_differs(const char *t, const char *p, const fan_curve_t *live)
{
    fan_curve_t fc;
    if (!t[0] || !p[0])
        return false;
    if (fan_from_csv(&fc, t, p) < 0 || fc.n != live->n)
        return true;
    for (int i = 0; i < fc.n && i < FAN_POINTS; i++)
        if (fc.temp_c[i] != live->temp_c[i] || fc.pwm[i] != live->pwm[i])
            return true;
    return false;
}

static bool cores_cap_differ(const hw_state_t *hw, const profile_meta_t *m)
{
    for (int i = 0; i < m->core_n; i++) {
        int id = m->core_ids[i];
        if (id >= 0 && id < HW_CPU_MAX && hw->cpu_mhz_core[id] > 0 &&
            abs(hw->cpu_mhz_core[id] - m->core_mhz[i]) > 2)
            return true;
    }
    if (m->freq > 0 && m->core_n == 0) {
        for (int c = 0; c < hw->cpu_n && c < HW_CPU_MAX; c++) {
            int id = hw->cpu_ids[c];
            if (hw->cpu_mhz_core[id] > 0 && hw->cpu_mhz_core[id] + 2 < m->freq)
                return true;
        }
    }
    return false;
}

static bool field_is_omitted(const profile_meta_t *m, int f)
{
    switch (f) {
    case SF_PROFILE:   return m->profile[0] == '\0';
    case SF_EPP:       return m->epp[0] == '\0';
    case SF_BOOST:     return m->cpu_boost < 0;
    case SF_FREQ:      return m->freq <= 0 && m->core_n <= 0;
    case SF_PPT:       return m->ppt_off != 1 && m->ppt_spl <= 0;
    case SF_GPU_CLOCK: return m->gpu_clock < 0;
    case SF_HZ:        return m->hz <= 0;
    case SF_PANEL_OD:  return m->panel_od < 0;
    case SF_BAT:       return m->bat <= 0;
    case SF_KBD:       return m->kbd[0] == '\0';
    case SF_FAN_CPU:   return m->fan_cpu_on < 0 && m->fan_cpu_t[0] == '\0';
    case SF_FAN_GPU:   return m->fan_gpu_on < 0 && m->fan_gpu_t[0] == '\0';
    default:           return true;
    }
}

static void format_fan_val(int on, const char *t, const char *p,
                           bool live_on, const fan_curve_t *live_fc,
                           char *sv, size_t svn, char *lv, size_t lvn,
                           bool *diff)
{
    fan_curve_t fc;
    bool has_c = t[0] && p[0] && fan_from_csv(&fc, t, p) == 0 && fc.n > 0;
    const char *st = on == 0 ? "off" : (on == 1 ? "on" : "curve");
    if (has_c)
        snprintf(sv, svn, "%s · %dpt (%d-%d°C)", st, fc.n,
                 fc.temp_c[0], fc.temp_c[fc.n - 1]);
    else
        snprintf(sv, svn, "%s", st);
    snprintf(lv, lvn, "%s", live_on ? "on" : "off");
    *diff = (on >= 0 && on != (int)live_on) || fan_csv_differs(t, p, live_fc);
}

static void field_format(const profile_meta_t *m, const hw_state_t *hw, int f,
                         char *val, size_t vn, bool *omitted, bool *differs)
{
    *omitted = field_is_omitted(m, f);
    *differs = false;
    if (*omitted) {
        snprintf(val, vn, "— (skip)");
        return;
    }

    char sv[64] = "", lv[40] = "";
    switch (f) {
    case SF_PROFILE:
        snprintf(sv, sizeof(sv), "%s", m->profile);
        snprintf(lv, sizeof(lv), "%s", hw_profile_name(hw->profile));
        *differs = strcasecmp(sv, lv) != 0;
        break;
    case SF_EPP:
        snprintf(sv, sizeof(sv), "%s", m->epp);
        snprintf(lv, sizeof(lv), "%s", hw_epp_name(hw->epp));
        *differs = strcasecmp(sv, lv) != 0;
        break;
    case SF_BOOST:
        snprintf(sv, sizeof(sv), "%s", m->cpu_boost ? "on" : "off");
        snprintf(lv, sizeof(lv), "%s", hw->cpu_boost ? "on" : "off");
        *differs = m->cpu_boost != (int)hw->cpu_boost;
        break;
    case SF_FREQ:
        if (m->freq > 0 && m->core_n > 0)
            snprintf(sv, sizeof(sv), "%d MHz (+%d core)", m->freq, m->core_n);
        else if (m->freq > 0)
            snprintf(sv, sizeof(sv), "%d MHz", m->freq);
        else
            snprintf(sv, sizeof(sv), "%d core cap(s)", m->core_n);
        if (hw->cpu_mhz_limit > 0)
            snprintf(lv, sizeof(lv), "%d MHz", hw->cpu_mhz_limit);
        *differs = (m->freq > 0 && hw->cpu_mhz_limit > 0 &&
                    abs(m->freq - hw->cpu_mhz_limit) > 2) ||
                   cores_cap_differ(hw, m);
        break;
    case SF_PPT:
        if (m->ppt_off == 1) {
            snprintf(sv, sizeof(sv), "off (unconstrained)");
            *differs = !hw->ppt_off;
        } else {
            snprintf(sv, sizeof(sv), "%d / %d / %d W",
                     m->ppt_spl, m->ppt_sppt, m->ppt_fppt);
            *differs = hw->ppt_spl > 0 &&
                       (m->ppt_spl != hw->ppt_spl ||
                        m->ppt_sppt != hw->ppt_sppt ||
                        m->ppt_fppt != hw->ppt_fppt);
        }
        if (hw->ppt_off)
            snprintf(lv, sizeof(lv), "off");
        else if (hw->ppt_spl > 0)
            snprintf(lv, sizeof(lv), "%d/%d/%dW",
                     hw->ppt_spl, hw->ppt_sppt, hw->ppt_fppt);
        break;
    case SF_GPU_CLOCK:
        if (m->gpu_clock == 0)
            snprintf(sv, sizeof(sv), "reset (unlocked)");
        else
            snprintf(sv, sizeof(sv), "%d MHz", m->gpu_clock);
        if (hw->gpu_clock_lock > 0)
            snprintf(lv, sizeof(lv), "%d MHz", hw->gpu_clock_lock);
        else
            snprintf(lv, sizeof(lv), "unlocked");
        *differs = m->gpu_clock != hw->gpu_clock_lock;
        break;
    case SF_HZ:
        snprintf(sv, sizeof(sv), "%d Hz", m->hz);
        if (hw->hz_cur > 0)
            snprintf(lv, sizeof(lv), "%d Hz", hw->hz_cur);
        *differs = hw->hz_cur > 0 && m->hz != hw->hz_cur;
        break;
    case SF_PANEL_OD:
        snprintf(sv, sizeof(sv), "%s", m->panel_od ? "on" : "off");
        snprintf(lv, sizeof(lv), "%s", hw->panel_od ? "on" : "off");
        *differs = m->panel_od != (int)hw->panel_od;
        break;
    case SF_BAT:
        snprintf(sv, sizeof(sv), "%d%%", m->bat);
        if (hw->bat_limit > 0)
            snprintf(lv, sizeof(lv), "%d%%", hw->bat_limit);
        *differs = hw->bat_limit > 0 && m->bat != hw->bat_limit;
        break;
    case SF_KBD:
        snprintf(sv, sizeof(sv), "%s", m->kbd);
        snprintf(lv, sizeof(lv), "%s", hw_kbd_name(hw->kbd));
        *differs = strcasecmp(sv, lv) != 0;
        break;
    case SF_FAN_CPU:
        format_fan_val(m->fan_cpu_on, m->fan_cpu_t, m->fan_cpu_p,
                       hw->fan_cpu_on, &hw->fan_cpu,
                       sv, sizeof(sv), lv, sizeof(lv), differs);
        break;
    case SF_FAN_GPU:
        format_fan_val(m->fan_gpu_on, m->fan_gpu_t, m->fan_gpu_p,
                       hw->fan_gpu_on, &hw->fan_gpu,
                       sv, sizeof(sv), lv, sizeof(lv), differs);
        break;
    default:
        break;
    }

    if (*differs && lv[0])
        snprintf(val, vn, "%.64s  (live: %.32s)", sv, lv);
    else
        snprintf(val, vn, "%.96s", sv);
}

/* ---- interactive field editing ------------------------------------------ */

static void seed_field_from_live(profile_meta_t *m, const hw_state_t *hw, int f)
{
    profile_meta_t live;
    profile_meta_from_hw(hw, &live);
    switch (f) {
    case SF_PROFILE:
        snprintf(m->profile, sizeof(m->profile), "%s", live.profile);
        break;
    case SF_EPP:
        snprintf(m->epp, sizeof(m->epp), "%s", live.epp);
        break;
    case SF_BOOST:
        m->cpu_boost = live.cpu_boost;
        break;
    case SF_FREQ:
        m->freq = live.freq > 0 ? live.freq
                : (hw->cpu_mhz_max > 0 ? hw->cpu_mhz_max : 4500);
        break;
    case SF_PPT:
        if (live.ppt_off == 1) {
            m->ppt_off = 1;
            m->ppt_spl = m->ppt_sppt = m->ppt_fppt = 0;
        } else if (live.ppt_spl > 0) {
            m->ppt_off = -1;
            m->ppt_spl = live.ppt_spl;
            m->ppt_sppt = live.ppt_sppt;
            m->ppt_fppt = live.ppt_fppt;
        } else {
            m->ppt_off = -1;
            m->ppt_spl = 60; m->ppt_sppt = 75; m->ppt_fppt = 75;
        }
        break;
    case SF_GPU_CLOCK:
        m->gpu_clock = live.gpu_clock >= 0 ? live.gpu_clock : 0;
        break;
    case SF_HZ:
        m->hz = live.hz > 0 ? live.hz
              : (hw->hz_count > 0 ? hw->hz_modes[0] : 60);
        break;
    case SF_PANEL_OD:
        m->panel_od = live.panel_od;
        break;
    case SF_BAT:
        m->bat = live.bat > 0 ? live.bat : 80;
        break;
    case SF_KBD:
        snprintf(m->kbd, sizeof(m->kbd), "%s", live.kbd);
        break;
    case SF_FAN_CPU:
        m->fan_cpu_on = live.fan_cpu_on;
        snprintf(m->fan_cpu_t, sizeof(m->fan_cpu_t), "%s", live.fan_cpu_t);
        snprintf(m->fan_cpu_p, sizeof(m->fan_cpu_p), "%s", live.fan_cpu_p);
        break;
    case SF_FAN_GPU:
        m->fan_gpu_on = live.fan_gpu_on;
        snprintf(m->fan_gpu_t, sizeof(m->fan_gpu_t), "%s", live.fan_gpu_t);
        snprintf(m->fan_gpu_p, sizeof(m->fan_gpu_p), "%s", live.fan_gpu_p);
        break;
    default:
        break;
    }
}

static void toggle_skip_field(profile_meta_t *m, const hw_state_t *hw, int f)
{
    if (field_is_omitted(m, f)) {
        seed_field_from_live(m, hw, f);
    } else {
        switch (f) {
        case SF_PROFILE:   m->profile[0] = '\0'; break;
        case SF_EPP:       m->epp[0] = '\0'; break;
        case SF_BOOST:     m->cpu_boost = -1; break;
        case SF_FREQ:      m->freq = 0; m->core_n = 0; break;
        case SF_PPT:       m->ppt_off = -1; m->ppt_spl = m->ppt_sppt = m->ppt_fppt = 0; break;
        case SF_GPU_CLOCK: m->gpu_clock = -1; break;
        case SF_HZ:        m->hz = 0; break;
        case SF_PANEL_OD:  m->panel_od = -1; break;
        case SF_BAT:       m->bat = 0; break;
        case SF_KBD:       m->kbd[0] = '\0'; break;
        case SF_FAN_CPU:   m->fan_cpu_on = -1; m->fan_cpu_t[0] = m->fan_cpu_p[0] = '\0'; break;
        case SF_FAN_GPU:   m->fan_gpu_on = -1; m->fan_gpu_t[0] = m->fan_gpu_p[0] = '\0'; break;
        default: break;
        }
    }
    g_ui.prof_edit_dirty = true;
}

static int cycle_str_list(const char *cur, const char *const *opts, int n, int dir)
{
    int idx = 0;
    for (int i = 0; i < n; i++) {
        if (!strcasecmp(cur, opts[i])) {
            idx = i;
            break;
        }
    }
    return (idx + dir + n) % n;
}

static void step_field(profile_meta_t *m, const hw_state_t *hw, int f, int dir)
{
    if (field_is_omitted(m, f)) {
        seed_field_from_live(m, hw, f);
        g_ui.prof_edit_dirty = true;
        /* for numeric / enum rows, still advance once so h/l feels immediate */
    }

    switch (f) {
    case SF_PROFILE: {
        static const char *const P[] = { "Quiet", "Balanced", "Performance" };
        int nx = cycle_str_list(m->profile, P, 3, dir);
        snprintf(m->profile, sizeof(m->profile), "%s", P[nx]);
        break;
    }
    case SF_EPP: {
        static const char *const E[] = {
            "default", "performance", "balance_performance",
            "balance_power", "power"
        };
        int nx = cycle_str_list(m->epp, E, 5, dir);
        snprintf(m->epp, sizeof(m->epp), "%s", E[nx]);
        break;
    }
    case SF_BOOST:
        m->cpu_boost = m->cpu_boost ? 0 : 1;
        break;
    case SF_FREQ: {
        int hi = hw->cpu_mhz_max > 400 ? hw->cpu_mhz_max : 6000;
        int lo = hw->cpu_mhz_min > 0 ? hw->cpu_mhz_min : 400;
        if (m->freq <= 0)
            m->freq = hw->cpu_mhz_limit > 0 ? hw->cpu_mhz_limit : hi;
        m->freq = ut_clamp_i(m->freq + dir * 100, lo, hi);
        break;
    }
    case SF_PPT: {
        /* 0: Q45 (45/55/55), 1: B60 (60/75/75), 2: P80 (80/80/80), 3: off */
        int cur = 1;
        if (m->ppt_off == 1) cur = 3;
        else if (m->ppt_spl <= 45) cur = 0;
        else if (m->ppt_spl <= 65) cur = 1;
        else cur = 2;
        cur = (cur + dir + 4) % 4;
        if (cur == 0) {
            m->ppt_off = -1; m->ppt_spl = 45; m->ppt_sppt = 55; m->ppt_fppt = 55;
        } else if (cur == 1) {
            m->ppt_off = -1; m->ppt_spl = 60; m->ppt_sppt = 75; m->ppt_fppt = 75;
        } else if (cur == 2) {
            m->ppt_off = -1; m->ppt_spl = 80; m->ppt_sppt = 80; m->ppt_fppt = 80;
        } else {
            m->ppt_off = 1; m->ppt_spl = m->ppt_sppt = m->ppt_fppt = 0;
        }
        break;
    }
    case SF_GPU_CLOCK:
        if (m->gpu_clock <= 0) {
            m->gpu_clock = dir > 0 ? 1200 : 2100;
        } else {
            int nx = m->gpu_clock + dir * 75;
            if (nx < 600)
                m->gpu_clock = 0; /* 0 = reset (unlocked) */
            else
                m->gpu_clock = ut_clamp_i(nx, 600, 3000);
        }
        break;
    case SF_HZ:
        if (hw->hz_count > 0) {
            int idx = 0;
            for (int i = 0; i < hw->hz_count; i++)
                if (hw->hz_modes[i] == m->hz)
                    idx = i;
            idx = (idx + dir + hw->hz_count) % hw->hz_count;
            m->hz = hw->hz_modes[idx];
        } else {
            static const int HZS[] = { 60, 120, 144, 165, 240 };
            int idx = 0;
            for (int i = 0; i < 5; i++)
                if (HZS[i] == m->hz)
                    idx = i;
            idx = (idx + dir + 5) % 5;
            m->hz = HZS[idx];
        }
        break;
    case SF_PANEL_OD:
        m->panel_od = m->panel_od ? 0 : 1;
        break;
    case SF_BAT:
        if (m->bat <= 0)
            m->bat = 80;
        m->bat = ut_clamp_i(m->bat + dir * 5, 20, 100);
        break;
    case SF_KBD: {
        static const char *const K[] = { "off", "low", "med", "high" };
        int nx = cycle_str_list(m->kbd, K, 4, dir);
        snprintf(m->kbd, sizeof(m->kbd), "%s", K[nx]);
        break;
    }
    case SF_FAN_CPU:
        m->fan_cpu_on = (m->fan_cpu_on == 1) ? 0 : 1;
        if (m->fan_cpu_on == 1 && !m->fan_cpu_t[0])
            fan_to_csv(&hw->fan_cpu, m->fan_cpu_t, sizeof(m->fan_cpu_t),
                       m->fan_cpu_p, sizeof(m->fan_cpu_p));
        break;
    case SF_FAN_GPU:
        m->fan_gpu_on = (m->fan_gpu_on == 1) ? 0 : 1;
        if (m->fan_gpu_on == 1 && !m->fan_gpu_t[0])
            fan_to_csv(&hw->fan_gpu, m->fan_gpu_t, sizeof(m->fan_gpu_t),
                       m->fan_gpu_p, sizeof(m->fan_gpu_p));
        break;
    default:
        break;
    }
    g_ui.prof_edit_dirty = true;
}

static void capture_live_curve(profile_meta_t *m, const hw_state_t *hw, int f)
{
    if (f == SF_FREQ) {
        m->core_n = 0;
        for (int c = 0; c < hw->cpu_n && c < HW_CPU_MAX; c++) {
            int id = hw->cpu_ids[c];
            if (hw->cpu_mhz_core[id] > 0 && hw->cpu_mhz_limit > 0 &&
                hw->cpu_mhz_core[id] < hw->cpu_mhz_limit &&
                m->core_n < HW_CPU_MAX) {
                m->core_ids[m->core_n] = id;
                m->core_mhz[m->core_n] = hw->cpu_mhz_core[id];
                m->core_n++;
            }
        }
        if (m->freq <= 0 && hw->cpu_mhz_limit > 0)
            m->freq = hw->cpu_mhz_limit;
        g_ui.prof_edit_dirty = true;
        ut_log("snapshot editor: captured %d live per-core cap(s)", m->core_n);
        return;
    }
    if (f == SF_FAN_CPU || f != SF_FAN_GPU) {
        fan_to_csv(&hw->fan_cpu, m->fan_cpu_t, sizeof(m->fan_cpu_t),
                   m->fan_cpu_p, sizeof(m->fan_cpu_p));
        m->fan_cpu_on = hw->fan_cpu_on ? 1 : 0;
    }
    if (f == SF_FAN_GPU || f != SF_FAN_CPU) {
        fan_to_csv(&hw->fan_gpu, m->fan_gpu_t, sizeof(m->fan_gpu_t),
                   m->fan_gpu_p, sizeof(m->fan_gpu_p));
        m->fan_gpu_on = hw->fan_gpu_on ? 1 : 0;
    }
    g_ui.prof_edit_dirty = true;
    ut_log("snapshot editor: captured live %s curve",
           f == SF_FAN_CPU ? "CPU fan"
         : f == SF_FAN_GPU ? "GPU fan"
                           : "CPU & GPU fan");
}

static void start_exact_edit(profile_meta_t *m, const hw_state_t *hw, int f)
{
    char seed[48] = "";
    switch (f) {
    case SF_FREQ:
        snprintf(seed, sizeof(seed), "%d",
                 m->freq > 0 ? m->freq
               : (hw->cpu_mhz_limit > 0 ? hw->cpu_mhz_limit : 4500));
        break;
    case SF_PPT:
        if (m->ppt_off == 1)
            snprintf(seed, sizeof(seed), "off");
        else if (m->ppt_spl > 0)
            snprintf(seed, sizeof(seed), "%d,%d,%d",
                     m->ppt_spl, m->ppt_sppt, m->ppt_fppt);
        else if (hw->ppt_spl > 0)
            snprintf(seed, sizeof(seed), "%d,%d,%d",
                     hw->ppt_spl, hw->ppt_sppt, hw->ppt_fppt);
        else
            snprintf(seed, sizeof(seed), "60,75,75");
        break;
    case SF_GPU_CLOCK:
        if (m->gpu_clock == 0)
            snprintf(seed, sizeof(seed), "reset");
        else if (m->gpu_clock > 0)
            snprintf(seed, sizeof(seed), "%d", m->gpu_clock);
        else if (hw->gpu_clock_lock > 0)
            snprintf(seed, sizeof(seed), "%d", hw->gpu_clock_lock);
        else
            snprintf(seed, sizeof(seed), "1800");
        break;
    case SF_HZ:
        snprintf(seed, sizeof(seed), "%d",
                 m->hz > 0 ? m->hz : (hw->hz_cur > 0 ? hw->hz_cur : 60));
        break;
    case SF_BAT:
        snprintf(seed, sizeof(seed), "%d",
                 m->bat > 0 ? m->bat : (hw->bat_limit > 0 ? hw->bat_limit : 80));
        break;
    default:
        /* enum / boolean rows: 'e' simply steps forward */
        step_field(m, hw, f, +1);
        return;
    }
    tin_set(&g_ui.prof_val_in, seed);
    g_ui.prof_val_typing = true;
}

static void commit_exact_edit(profile_meta_t *m, const hw_state_t *hw, int f)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%.63s", g_ui.prof_val_in.buf);
    char *s = ut_trim(buf);
    if (!*s || !strcmp(s, "-") || !strcasecmp(s, "skip")) {
        if (!field_is_omitted(m, f))
            toggle_skip_field(m, hw, f);
        return;
    }

    switch (f) {
    case SF_FREQ: {
        int hi = hw->cpu_mhz_max > 400 ? hw->cpu_mhz_max : 6000;
        int lo = hw->cpu_mhz_min > 0 ? hw->cpu_mhz_min : 400;
        int v = atoi(s);
        if (v > 0) {
            m->freq = ut_clamp_i(v, lo, hi);
            g_ui.prof_edit_dirty = true;
        }
        break;
    }
    case SF_PPT: {
        int a = 0, b = 0, c = 0;
        if (!strcasecmp(s, "off")) {
            m->ppt_off = 1; m->ppt_spl = m->ppt_sppt = m->ppt_fppt = 0;
            g_ui.prof_edit_dirty = true;
        } else if (!strcasecmp(s, "Q45")) {
            m->ppt_off = -1; m->ppt_spl = 45; m->ppt_sppt = 55; m->ppt_fppt = 55;
            g_ui.prof_edit_dirty = true;
        } else if (!strcasecmp(s, "B60")) {
            m->ppt_off = -1; m->ppt_spl = 60; m->ppt_sppt = 75; m->ppt_fppt = 75;
            g_ui.prof_edit_dirty = true;
        } else if (!strcasecmp(s, "P80")) {
            m->ppt_off = -1; m->ppt_spl = 80; m->ppt_sppt = 80; m->ppt_fppt = 80;
            g_ui.prof_edit_dirty = true;
        } else if (sscanf(s, "%d,%d,%d", &a, &b, &c) == 3 &&
                   a >= 5 && b >= 5 && c >= 5) {
            m->ppt_off = -1;
            m->ppt_spl  = ut_clamp_i(a, 5, 250);
            m->ppt_sppt = ut_clamp_i(b, m->ppt_spl, 250);
            m->ppt_fppt = ut_clamp_i(c, m->ppt_sppt, 250);
            g_ui.prof_edit_dirty = true;
        } else if (sscanf(s, "%d", &a) == 1 && a >= 5) {
            m->ppt_off = -1;
            a = ut_clamp_i(a, 5, 250);
            m->ppt_spl = m->ppt_sppt = m->ppt_fppt = a;
            g_ui.prof_edit_dirty = true;
        } else {
            ut_log("ppt expects SPL,SPPT,FPPT (e.g. 60,75,75) or Q45/B60/P80/off");
        }
        break;
    }
    case SF_GPU_CLOCK:
        if (!strcasecmp(s, "reset") || !strcasecmp(s, "off") || !strcmp(s, "0")) {
            m->gpu_clock = 0;
            g_ui.prof_edit_dirty = true;
        } else {
            int v = atoi(s);
            if (v >= 180 && v <= 3000) {
                m->gpu_clock = v;
                g_ui.prof_edit_dirty = true;
            } else {
                ut_log("gpu-clock expects 180..3000 MHz or 'reset'");
            }
        }
        break;
    case SF_HZ: {
        int v = atoi(s);
        if (v >= 30 && v <= 540) {
            m->hz = v;
            g_ui.prof_edit_dirty = true;
        }
        break;
    }
    case SF_BAT: {
        int v = atoi(s);
        if (v >= 20 && v <= 100) {
            m->bat = v;
            g_ui.prof_edit_dirty = true;
        }
        break;
    }
    default:
        break;
    }
}

static void save_edited_snapshot(bool exit_to_list)
{
    if (g_ui.prof_n <= 0 || g_ui.prof_sel < 0 || g_ui.prof_sel >= g_ui.prof_n)
        return;
    char name[PROFILE_NAME_MAX];
    snprintf(name, sizeof(name), "%s", g_ui.profs[g_ui.prof_sel]);
    if (profile_save_meta(name, &g_ui.prof_edit) == 0) {
        panel_profiles_reload();
        if (exit_to_list)
            g_ui.prof_in_edit = false;
        snprintf(g_ui.pw_msg, sizeof(g_ui.pw_msg),
                 "✓ snapshot '%s' saved", name);
        g_ui.pw_msg_fail = false;
        g_ui.pw_msg_ms = ut_mono_ms();
        ut_log("snapshot '%s' updated", name);
    } else {
        snprintf(g_ui.pw_msg, sizeof(g_ui.pw_msg),
                 "⚠ snapshot '%s': save FAILED", name);
        g_ui.pw_msg_fail = true;
        g_ui.pw_msg_ms = ut_mono_ms();
        ut_log("snapshot '%s': FAILED to save changes", name);
    }
}

/* ---- drawing ------------------------------------------------------------ */

static void draw_snap_row(struct ncplane *n, int x, int y, int w,
                          const char *name, const char *badge,
                          uint32_t badge_fg, bool sel, bool focused)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    uint32_t nfg = sel ? (focused ? pal->accent : pal->text) : pal->text;
    bool bg = sel && focused;

    ncplane_set_channels(n, ui_ch(nfg, bg));
    ncplane_set_styles(n, sel ? NCSTYLE_BOLD : NCSTYLE_NONE);
    ncplane_cursor_move_yx(n, y, x);
    for (int i = 0; i < w; i++)
        ncplane_putchar(n, ' ');

    int bw = badge && badge[0] ? ut_cells(badge) : 0;
    int max_l = bw > 0 ? (w - bw - 3) : (w - 2);
    if (max_l < 4)
        max_l = w - 2;

    char lbuf[96];
    snprintf(lbuf, sizeof(lbuf), "%s%s", sel ? "▸ " : "  ", name);
    ui_trunc(lbuf, max_l);
    ncplane_putstr_yx(n, y, x, lbuf);
    ncplane_set_styles(n, NCSTYLE_NONE);

    if (bw > 0 && w - bw - 1 > x + 4) {
        ncplane_set_channels(n, ui_ch(badge_fg, bg));
        ncplane_putstr_yx(n, y, x + w - bw - 1, badge);
    }
}

static void draw_field_row(struct ncplane *n, int x, int y, int w,
                           const char *label, const char *value,
                           uint32_t vfg, bool sel)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    ncplane_set_channels(n, ui_ch(sel ? pal->accent : pal->text, sel));
    ncplane_set_styles(n, sel ? NCSTYLE_BOLD : NCSTYLE_NONE);
    ncplane_cursor_move_yx(n, y, x);
    for (int i = 0; i < w; i++)
        ncplane_putchar(n, ' ');

    char lbuf[64];
    snprintf(lbuf, sizeof(lbuf), "%s%s", sel ? "▸ " : "  ", label);
    ui_trunc(lbuf, w / 2);
    ncplane_putstr_yx(n, y, x, lbuf);
    ncplane_set_styles(n, NCSTYLE_NONE);

    int lw = ut_cells(lbuf);
    int maxv = w - lw - 2;
    if (maxv > 2 && value && value[0]) {
        char vbuf[128];
        snprintf(vbuf, sizeof(vbuf), "%s", value);
        ui_trunc(vbuf, maxv);
        int vw = ut_cells(vbuf);
        int vx = x + w - vw - 1;
        if (vx < x + lw + 1)
            vx = x + lw + 1;
        ncplane_set_channels(n, ui_ch(sel ? pal->accent : vfg, sel));
        if (sel)
            ncplane_set_styles(n, NCSTYLE_BOLD);
        ncplane_putstr_yx(n, y, vx, vbuf);
        ncplane_set_styles(n, NCSTYLE_NONE);
    }
}

static void draw_list_pane(struct ncplane *n, const rect_t *r, bool show_foot)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int x = r->x, y = r->y, w = r->w;
    if (w < 12 || r->h < 4)
        return;

    bool focused = !g_ui.prof_in_edit;
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "SNAPSHOTS (%d)", g_ui.prof_n);
    ui_putln(n, x, y++, w, hdr, focused ? pal->accent : pal->muted, true);
    ui_hline(n, x, y++, w, pal->border);

    int foot_h = show_foot ? 3 : 0;
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
        ui_putln(n, x, y, w, "(none — press s to save current state)",
                  pal->muted, false);
        return;
    }

    for (int i = g_ui.prof_top, shown = 0;
         i < g_ui.prof_n && shown < rows; i++, shown++) {
        int ry = y + shown;
        bool sel = (i == g_ui.prof_sel);
        char badge[20] = "";
        uint32_t bfg = pal->muted;
        if (sel && g_ui.prof_del_arm == i) {
            snprintf(badge, sizeof(badge), "DELETE?");
            bfg = pal->err;
        } else if (sel && g_ui.prof_upd_arm == i) {
            snprintf(badge, sizeof(badge), "UPDATE?");
            bfg = pal->warn;
        } else if (s_meta_ok[i]) {
            int nd = profile_meta_diff_count(g_ui.hw, &s_meta[i]);
            if (nd == 0) {
                snprintf(badge, sizeof(badge), "≡ live");
                bfg = pal->ok;
            } else {
                snprintf(badge, sizeof(badge), "Δ %d", nd);
                bfg = pal->warn;
            }
        }
        draw_snap_row(n, x, ry, w, g_ui.profs[i], badge, bfg, sel, focused);
        tgt_register(x, ry, w, 1, TGT(TGT_PANEL_PROFILES, ACT_PL_ITEM + i));
    }

    /* narrow-mode compact footer when right inspector pane is hidden */
    if (show_foot && g_ui.prof_sel < g_ui.prof_n && s_meta_ok[g_ui.prof_sel]) {
        int fy = r->y + r->h - foot_h;
        if (fy > y) {
            ui_hline(n, x, fy, w, pal->border);
            char sum[160], diff[128];
            profile_meta_summary(&s_meta[g_ui.prof_sel], sum, sizeof(sum));
            profile_meta_diff(g_ui.hw, &s_meta[g_ui.prof_sel], diff, sizeof(diff));
            bool synced = strncmp(diff, "≡", 3) == 0;
            ui_putln(n, x, fy + 1, w, sum, pal->text, false);
            ui_putln(n, x, fy + 2, w, diff, synced ? pal->ok : pal->warn, false);
        }
    }
}

static void draw_editor_pane(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int x = r->x, y = r->y, w = r->w;
    if (w < 16 || r->h < 5)
        return;

    if (g_ui.prof_n == 0 || g_ui.prof_sel >= g_ui.prof_n) {
        ui_putln(n, x, y++, w, "INSPECTOR", pal->muted, true);
        ui_hline(n, x, y++, w, pal->border);
        ui_putln(n, x, y + 1, w,
                 "Save a snapshot with [s] (custom name) or [n] (auto-name).",
                 pal->muted, false);
        ui_putln(n, x, y + 2, w,
                 "Then press [e] to inspect, tweak, or skip individual fields.",
                 pal->muted, false);
        return;
    }

    if (g_ui.prof_meta_sel != g_ui.prof_sel && !g_ui.prof_in_edit)
        sync_edit_from_sel();

    const char *name = g_ui.profs[g_ui.prof_sel];
    const profile_meta_t *m = g_ui.prof_in_edit ? &g_ui.prof_edit
                                                : &s_meta[g_ui.prof_sel];

    /* Header + clickable action pills */
    char hdr[96];
    if (g_ui.prof_in_edit) {
        snprintf(hdr, sizeof(hdr), "EDIT SNAPSHOT · %.48s%s",
                 name, g_ui.prof_edit_dirty ? " ●" : "");
        ui_putln(n, x, y, w, hdr,
                 g_ui.prof_edit_dirty ? pal->warn : pal->accent, true);
        const ui_btndef_t btns[] = {
            { "Save (w)", g_ui.prof_edit_dirty, TGT(TGT_PANEL_PROFILES, ACT_PL_BTN_SAVE) },
            { "Skip (x)", false,                TGT(TGT_PANEL_PROFILES, ACT_PL_BTN_SKIP) },
            { "Curve (f)", false,               TGT(TGT_PANEL_PROFILES, ACT_PL_BTN_CURV) },
            { "Back", false,                    TGT(TGT_PANEL_PROFILES, ACT_PL_BTN_BACK) },
        };
        int bw = 38;
        if (w > bw + 24)
            ui_btn_row(n, y, x + w - bw, bw, btns, 4);
    } else {
        snprintf(hdr, sizeof(hdr), "INSPECTOR · %.48s", name);
        ui_putln(n, x, y, w, hdr, pal->muted, true);
        const ui_btndef_t btns[] = {
            { "Edit fields (e)", false, TGT(TGT_PANEL_PROFILES, ACT_PL_BTN_EDIT) },
        };
        int bw = 18;
        if (w > bw + 20)
            ui_btn_row(n, y, x + w - bw, bw, btns, 1);
    }
    y++;
    ui_hline(n, x, y++, w, pal->border);

    /* Metadata + live diff banner */
    char meta_ln[160];
    snprintf(meta_ln, sizeof(meta_ln), "saved %s · note: %s",
             m->saved[0] ? m->saved : "-",
             m->note[0] ? m->note : "(none — press c)");
    ui_putln(n, x, y++, w, meta_ln, pal->muted, false);

    char diff_ln[160];
    profile_meta_diff(g_ui.hw, m, diff_ln, sizeof(diff_ln));
    bool synced = strncmp(diff_ln, "≡", 3) == 0;
    ui_putln(n, x, y++, w, diff_ln, synced ? pal->ok : pal->warn, false);
    y++; /* breathing space before field rows */

    int bot_reserve = g_ui.prof_in_edit ? 2 : 1;
    int avail = (r->y + r->h) - y - bot_reserve;
    if (avail < 1)
        avail = 1;

    if (SF_COUNT <= avail) {
        g_ui.prof_edit_top = 0;
    } else {
        int cur = g_ui.prof_in_edit ? g_ui.prof_edit_sel : 0;
        if (g_ui.prof_edit_top > cur)
            g_ui.prof_edit_top = cur;
        if (cur >= g_ui.prof_edit_top + avail)
            g_ui.prof_edit_top = cur - avail + 1;
        if (g_ui.prof_edit_top + avail > SF_COUNT)
            g_ui.prof_edit_top = SF_COUNT - avail;
        if (g_ui.prof_edit_top < 0)
            g_ui.prof_edit_top = 0;
    }

    for (int f = g_ui.prof_edit_top, shown = 0;
         f < SF_COUNT && shown < avail; f++, shown++) {
        int ry = y + shown;
        bool sel = g_ui.prof_in_edit && (f == g_ui.prof_edit_sel);
        char val[128];
        bool omitted = false, differs = false;

        if (sel && g_ui.prof_val_typing) {
            char disp[72];
            tin_format(&g_ui.prof_val_in, disp, sizeof(disp), true);
            snprintf(val, sizeof(val), "[ %s ]", disp);
            draw_field_row(n, x, ry, w, SF_LABELS[f], val, pal->accent, true);
        } else {
            field_format(m, g_ui.hw, f, val, sizeof(val), &omitted, &differs);
            uint32_t vfg = omitted ? pal->muted
                         : differs ? pal->warn
                                   : pal->text;
            draw_field_row(n, x, ry, w, SF_LABELS[f], val, vfg, sel);
        }
        tgt_register(x, ry, w, 1, TGT(TGT_PANEL_PROFILES, ACT_PL_FIELD + f));
    }

    int hy = r->y + r->h - 1;
    if (hy > y) {
        if (g_ui.prof_in_edit && g_ui.prof_val_typing) {
            const char *fmt_hint =
                g_ui.prof_edit_sel == SF_PPT
                    ? "Enter ok · Esc cancel · SPL,SPPT,FPPT (e.g. 60,75,75) or Q45/B60/P80/off"
                : g_ui.prof_edit_sel == SF_GPU_CLOCK
                    ? "Enter ok · Esc cancel · MHz (180..3000) or 'reset' to unlock"
                    : "Enter confirm · Esc cancel · '-' to skip field";
            ui_putln(n, x, hy, w, fmt_hint, pal->accent, false);
        } else if (g_ui.prof_in_edit) {
            ui_putln(n, x, hy, w,
                     "h/l change · x toggle skip (—) · e exact · f live curve/cores · w/Enter save",
                     pal->muted, false);
        } else {
            ui_putln(n, x, hy, w,
                     "Press e (or click a field) to edit · omitted (—) fields stay untouched on apply",
                     pal->muted, false);
        }
    }
}

void panel_profiles_draw(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int x = r->x, y = r->y, w = r->w;
    if (w < 12 || r->h < 5)
        return;

    if (g_ui.prof_del_arm >= g_ui.prof_n || g_ui.prof_del_arm != g_ui.prof_sel)
        g_ui.prof_del_arm = -1;
    if (g_ui.prof_upd_arm >= g_ui.prof_n || g_ui.prof_upd_arm != g_ui.prof_sel)
        g_ui.prof_upd_arm = -1;

    /* Top action / confirmation banner (full workspace width) */
    if (g_ui.prof_input != PL_INPUT_NONE) {
        const char *act = g_ui.prof_input == PL_INPUT_SAVE   ? "save"
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
        }
        ui_hline(n, x, y++, w, pal->border);
    } else if (g_ui.prof_del_arm >= 0 && g_ui.prof_del_arm == g_ui.prof_sel) {
        char warn[140];
        snprintf(warn, sizeof(warn),
                 "press d again to delete '%.48s' · any other key cancels",
                 g_ui.profs[g_ui.prof_sel]);
        ui_putln(n, x, y++, w, warn, pal->err, true);
        ui_hline(n, x, y++, w, pal->border);
    } else if (g_ui.prof_upd_arm >= 0 && g_ui.prof_upd_arm == g_ui.prof_sel) {
        char warn[160];
        snprintf(warn, sizeof(warn),
                 "press u again to overwrite '%.48s' with live hardware · any other key cancels",
                 g_ui.profs[g_ui.prof_sel]);
        ui_putln(n, x, y++, w, warn, pal->warn, true);
        ui_hline(n, x, y++, w, pal->border);
    }

    int rem_h = (r->y + r->h) - y;
    if (rem_h < 3)
        return;

    if (w >= 82) {
        int lw = ut_clamp_i((w * 35) / 100, 26, 38);
        rect_t rl = { x, y, lw, rem_h };
        rect_t rr = { x + lw + 3, y, w - (lw + 3), rem_h };
        draw_list_pane(n, &rl, false);
        ui_vline(n, x + lw + 1, y, rem_h, pal->border);
        draw_editor_pane(n, &rr);
    } else if (g_ui.prof_in_edit) {
        rect_t rr = { x, y, w, rem_h };
        draw_editor_pane(n, &rr);
    } else {
        rect_t rl = { x, y, w, rem_h };
        draw_list_pane(n, &rl, true);
    }
}

/* ---- list actions & key handling ---------------------------------------- */

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
    hw_refresh_live(g_ui.hw);
    if (profile_export(g_ui.prof_name.buf, g_ui.hw) == 0) {
        char safe[PROFILE_NAME_MAX];
        profile_sanitize_name(g_ui.prof_name.buf, safe, sizeof(safe));
        panel_profiles_reload();
        for (int i = 0; i < g_ui.prof_n; i++)
            if (!strcmp(g_ui.profs[i], safe))
                select_snapshot(i);
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
    panel_profiles_reload();
    for (int i = 0; i < g_ui.prof_n; i++)
        if (!strcmp(g_ui.profs[i], safe))
            select_snapshot(i);
}

static void note_selected(void)
{
    if (g_ui.prof_n == 0 || g_ui.prof_sel >= g_ui.prof_n)
        return;
    if (profile_set_note(g_ui.profs[g_ui.prof_sel], g_ui.prof_note.buf) == 0)
        panel_profiles_reload();
}

static void handle_editor_key(uint32_t key)
{
    if (g_ui.prof_val_typing) {
        if (key == NCKEY_ESC) {
            g_ui.prof_val_typing = false;
            return;
        }
        if (key == NCKEY_ENTER || key == '\r' || key == '\n') {
            commit_exact_edit(&g_ui.prof_edit, g_ui.hw, g_ui.prof_edit_sel);
            g_ui.prof_val_typing = false;
            return;
        }
        tin_key(&g_ui.prof_val_in, key);
        return;
    }

    switch (key) {
    case NCKEY_ESC:
        if (g_ui.prof_edit_dirty) {
            sync_edit_from_sel();
            ut_log("snapshot edit discarded");
        }
        g_ui.prof_in_edit = false;
        break;
    case 'j':
    case NCKEY_DOWN:
        if (g_ui.prof_edit_sel + 1 < SF_COUNT)
            g_ui.prof_edit_sel++;
        break;
    case 'k':
    case NCKEY_UP:
        if (g_ui.prof_edit_sel > 0)
            g_ui.prof_edit_sel--;
        break;
    case 'h':
    case NCKEY_LEFT:
        step_field(&g_ui.prof_edit, g_ui.hw, g_ui.prof_edit_sel, -1);
        break;
    case 'l':
    case NCKEY_RIGHT:
    case ' ':
        step_field(&g_ui.prof_edit, g_ui.hw, g_ui.prof_edit_sel, +1);
        break;
    case 'x':
    case 'X':
    case NCKEY_DEL:
    case NCKEY_BACKSPACE:
    case 0x7f:
        toggle_skip_field(&g_ui.prof_edit, g_ui.hw, g_ui.prof_edit_sel);
        break;
    case 'e':
    case 'E':
        start_exact_edit(&g_ui.prof_edit, g_ui.hw, g_ui.prof_edit_sel);
        break;
    case 'f':
    case 'F':
        capture_live_curve(&g_ui.prof_edit, g_ui.hw, g_ui.prof_edit_sel);
        break;
    case 'r':
    case 'R':
        sync_edit_from_sel();
        ut_log("snapshot fields reverted to saved file");
        break;
    case 'w':
    case 'W':
        save_edited_snapshot(false);
        break;
    case NCKEY_ENTER:
    case '\r':
    case '\n':
        save_edited_snapshot(true);
        break;
    default:
        break;
    }
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

    if (g_ui.prof_in_edit) {
        handle_editor_key(key);
        return;
    }

    if (key != 'd' && key != 'D')
        g_ui.prof_del_arm = -1;
    if (key != 'u' && key != 'U')
        g_ui.prof_upd_arm = -1;

    switch (key) {
    case 'j':
    case NCKEY_DOWN:
        if (g_ui.prof_sel + 1 < g_ui.prof_n)
            select_snapshot(g_ui.prof_sel + 1);
        break;
    case 'k':
    case NCKEY_UP:
        if (g_ui.prof_sel > 0)
            select_snapshot(g_ui.prof_sel - 1);
        break;
    case NCKEY_ENTER:
    case '\r':
    case '\n':
    case ' ':
        apply_selected();
        break;
    case 'e':
    case 'E':
    case 'l':
    case NCKEY_RIGHT:
        if (g_ui.prof_n > 0 && g_ui.prof_sel < g_ui.prof_n) {
            sync_edit_from_sel();
            g_ui.prof_in_edit = true;
        }
        break;
    case 's':
    case 'S':
        hw_refresh_live(g_ui.hw);
        if (!g_ui.prof_name.buf[0])
            tin_set(&g_ui.prof_name, "my-snapshot");
        g_ui.prof_input = PL_INPUT_SAVE;
        break;
    case 'n':
    case 'N': {
        hw_refresh_live(g_ui.hw);
        char name[32];
        profile_gen_name(name, sizeof(name));
        tin_set(&g_ui.prof_name, name);
        g_ui.prof_input = PL_INPUT_SAVE;
        break;
    }
    case 'u':
    case 'U':
        if (g_ui.prof_n > 0 && g_ui.prof_sel < g_ui.prof_n) {
            if (g_ui.prof_upd_arm == g_ui.prof_sel) {
                g_ui.prof_upd_arm = -1;
                char nm[PROFILE_NAME_MAX];
                snprintf(nm, sizeof(nm), "%s", g_ui.profs[g_ui.prof_sel]);
                hw_refresh_live(g_ui.hw);
                if (profile_export(nm, g_ui.hw) == 0) {
                    panel_profiles_reload();
                    snprintf(g_ui.pw_msg, sizeof(g_ui.pw_msg),
                             "✓ snapshot '%s' overwritten from live", nm);
                    g_ui.pw_msg_fail = false;
                    g_ui.pw_msg_ms = ut_mono_ms();
                    ut_log("snapshot '%s' overwritten from live hardware", nm);
                }
            } else {
                g_ui.prof_upd_arm = g_ui.prof_sel;
                ut_log("press u again to overwrite '%s' from live (any other key cancels)",
                       g_ui.profs[g_ui.prof_sel]);
            }
        }
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
            tin_set(&g_ui.prof_note,
                    s_meta_ok[g_ui.prof_sel] ? s_meta[g_ui.prof_sel].note : "");
            g_ui.prof_input = PL_INPUT_NOTE;
        }
        break;
    case 'd':
    case 'D':
        if (g_ui.prof_n > 0 && g_ui.prof_sel < g_ui.prof_n) {
            if (g_ui.prof_del_arm == g_ui.prof_sel) {
                g_ui.prof_del_arm = -1;
                profile_delete(g_ui.profs[g_ui.prof_sel]);
                panel_profiles_reload();
            } else {
                g_ui.prof_del_arm = g_ui.prof_sel;
                ut_log("press d again to delete '%s' (any other key cancels)",
                       g_ui.profs[g_ui.prof_sel]);
            }
        }
        break;
    default:
        break;
    }
}

void panel_profiles_act(int id)
{
    g_ui.prof_del_arm = -1;
    g_ui.prof_upd_arm = -1;
    g_ui.prof_val_typing = false;

    if (id >= ACT_PL_ITEM && id < ACT_PL_ITEM + MAX_PROFILES) {
        int i = id - ACT_PL_ITEM;
        if (i < g_ui.prof_n) {
            if (g_ui.prof_in_edit) {
                g_ui.prof_in_edit = false;
                select_snapshot(i);
            } else if (i == g_ui.prof_sel) {
                apply_selected();
            } else {
                select_snapshot(i);
            }
        }
        return;
    }
    if (id >= ACT_PL_FIELD && id < ACT_PL_FIELD + SF_COUNT) {
        if (g_ui.prof_n <= 0 || g_ui.prof_sel >= g_ui.prof_n)
            return;
        int f = id - ACT_PL_FIELD;
        if (!g_ui.prof_in_edit) {
            sync_edit_from_sel();
            g_ui.prof_in_edit = true;
            g_ui.prof_edit_sel = f;
        } else if (g_ui.prof_edit_sel == f) {
            step_field(&g_ui.prof_edit, g_ui.hw, f, +1);
        } else {
            g_ui.prof_edit_sel = f;
        }
        return;
    }
    switch (id) {
    case ACT_PL_BTN_EDIT:
        if (g_ui.prof_n > 0 && g_ui.prof_sel < g_ui.prof_n) {
            sync_edit_from_sel();
            g_ui.prof_in_edit = true;
        }
        break;
    case ACT_PL_BTN_SAVE:
        if (g_ui.prof_in_edit)
            save_edited_snapshot(false);
        break;
    case ACT_PL_BTN_SKIP:
        if (g_ui.prof_in_edit)
            toggle_skip_field(&g_ui.prof_edit, g_ui.hw, g_ui.prof_edit_sel);
        break;
    case ACT_PL_BTN_CURV:
        if (g_ui.prof_in_edit)
            capture_live_curve(&g_ui.prof_edit, g_ui.hw, g_ui.prof_edit_sel);
        break;
    case ACT_PL_BTN_BACK:
        if (g_ui.prof_in_edit) {
            if (g_ui.prof_edit_dirty)
                sync_edit_from_sel();
            g_ui.prof_in_edit = false;
        }
        break;
    default:
        break;
    }
}
