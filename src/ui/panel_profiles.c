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

/* View 4: SNAPSHOTS & SAVED FAN CURVES — Two-pane Konsept C workspace:
 *   Left pane (split into 2 sections, 'f' or click switches):
 *     Top    : Saved .ctr snapshots ("group/name") with live diff badges
 *              (≡ live / Δ N). Enter applies, e/l edits fields, s/n saves,
 *              uu overwrites from live, r renames, c note, dd deletes.
 *     Bottom : Saved .fc fan curves ("group/name") with live diff badges.
 *              Enter applies to EC, e/l edits points, o opens in 2 Fans
 *              graph, s/n saves, uu overwrites, r renames, c note, dd del.
 *   Right pane:
 *     - Snapshot Inspector + 15-Field Interactive Editor (when top list
 *       is active): CPU/GPU fan curve rows step through saved fan curves,
 *       factory refs, live curve, or off ('a' applies curve to both fans).
 *     - Fan Curve Inspector + 18-Row Interactive Editor (when bottom list
 *       is active): edit CPU/GPU custom mode + 8 points each (PWM %, °C). */

enum {
    ACT_PL_ITEM     = 1,   /* + snapshot index (0..MAX_PROFILES-1) */
    ACT_PL_FC_ITEM  = 100, /* + fan curve index (0..MAX_FCURVES-1) */
    ACT_PL_FIELD    = 200, /* + snapshot editor field index (0..SF_COUNT-1) */
    ACT_PL_FC_FIELD = 240, /* + fan curve editor row index (0..FCF_COUNT-1) */
    ACT_PL_BTN_EDIT = 300,
    ACT_PL_BTN_SAVE = 301,
    ACT_PL_BTN_SKIP = 302,
    ACT_PL_BTN_CURV = 303,
    ACT_PL_BTN_BACK = 304,
    ACT_PL_BTN_OPEN = 305, /* open saved fan curve in 2 Fans graph */
    ACT_PL_HDR_SNAP = 310,
    ACT_PL_HDR_FC   = 311,
};

enum { PL_INPUT_NONE = 0, PL_INPUT_SAVE, PL_INPUT_RENAME, PL_INPUT_NOTE };

enum {
    SF_PROFILE = 0,
    SF_EPP,
    SF_BOOST,
    SF_FREQ,
    SF_PPT,
    SF_GPU_CLOCK,
    SF_ASUSD_AC,
    SF_ASUSD_BAT,
    SF_HZ,
    SF_PANEL_OD,
    SF_BAT,
    SF_KBD,
    SF_AURA,
    SF_FAN_CPU,
    SF_FAN_GPU,
    SF_COUNT
};

enum {
    FCF_CPU_MODE = 0,
    FCF_CPU_PT1  = 1,  /* 1..8 */
    FCF_GPU_MODE = 9,
    FCF_GPU_PT1  = 10, /* 10..17 */
    FCF_COUNT    = 18
};

static const char *const SF_LABELS[SF_COUNT] = {
    "Platform profile",
    "Energy perf (EPP)",
    "CPU boost",
    "CPU ceiling",
    "PPT (SPL/SPPT/FPPT)",
    "GPU clock lock",
    "Asusd AC profile",
    "Asusd battery profile",
    "Refresh rate",
    "Panel overdrive",
    "Battery limit",
    "Keyboard light",
    "Keyboard colour",
    "CPU fan curve",
    "GPU fan curve",
};

/* Cached parsed metadata — loaded once on reload, zero per-frame disk I/O. */
static profile_meta_t s_meta[MAX_PROFILES];
static bool s_meta_ok[MAX_PROFILES];
static fcurve_meta_t s_fc_meta[MAX_FCURVES];
static bool s_fc_ok[MAX_FCURVES];

const fcurve_meta_t *panel_profiles_fcurve_meta(int idx)
{
    if (idx < 0 || idx >= g_ui.fc_n || !s_fc_ok[idx])
        return NULL;
    return &s_fc_meta[idx];
}

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

static void sync_fc_edit_from_sel(void)
{
    g_ui.fc_edit_dirty = false;
    g_ui.prof_val_typing = false;
    if (g_ui.fc_n > 0 && g_ui.fc_sel >= 0 && g_ui.fc_sel < g_ui.fc_n &&
        s_fc_ok[g_ui.fc_sel]) {
        g_ui.fc_edit = s_fc_meta[g_ui.fc_sel];
        fan_to_hw8(&g_ui.fc_edit.cpu, &g_ui.fc_edit.cpu);
        fan_to_hw8(&g_ui.fc_edit.gpu, &g_ui.fc_edit.gpu);
    } else {
        fcurve_meta_init(&g_ui.fc_edit);
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

    g_ui.fc_n = fcurve_list(g_ui.fcurves, MAX_FCURVES);
    if (g_ui.fc_n <= 0)
        g_ui.fc_sel = 0;
    else if (g_ui.fc_sel >= g_ui.fc_n)
        g_ui.fc_sel = g_ui.fc_n - 1;
    for (int i = 0; i < g_ui.fc_n; i++)
        s_fc_ok[i] = fcurve_parse(g_ui.fcurves[i], &s_fc_meta[i]) == 0;

    g_ui.prof_meta_sel = -1;
    sync_edit_from_sel();
    sync_fc_edit_from_sel();
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

static void select_fcurve(int idx)
{
    if (idx < 0 || idx >= g_ui.fc_n)
        return;
    g_ui.fc_sel = idx;
    sync_fc_edit_from_sel();
}

/* ---- saved fan curve matching & selection for snapshots ----------------- */

/* Check if an 8-point curve matches a saved fan curve or OEM reference.
 * Returns >= 0 saved curve index, -2..-4 for Quiet/Balanced/Perf ref, -1 none. */
static int identify_curve_source(const fan_curve_t *fc8, bool gpu,
                                 char *tag, size_t tagn)
{
    if (tag && tagn)
        tag[0] = '\0';
    for (int i = 0; i < g_ui.fc_n; i++) {
        if (!s_fc_ok[i])
            continue;
        bool has = gpu ? s_fc_meta[i].has_gpu : s_fc_meta[i].has_cpu;
        const fan_curve_t *cand = gpu ? &s_fc_meta[i].gpu : &s_fc_meta[i].cpu;
        if (has && fan_curves_equal(fc8, cand)) {
            if (tag && tagn)
                snprintf(tag, tagn, "[%s]", g_ui.fcurves[i]);
            return i;
        }
    }
    for (int p = 0; p < 3; p++) {
        fan_curve_t ref_c, ref_g;
        if (fan_preset_builtin((fan_preset_t)p, &ref_c, &ref_g) == 0 &&
            fan_curves_equal(fc8, gpu ? &ref_g : &ref_c)) {
            if (tag && tagn)
                snprintf(tag, tagn, "[%s ref]", fan_preset_name((fan_preset_t)p));
            return -2 - p;
        }
    }
    return -1;
}

static int find_fcurve_by_name(const char *query)
{
    if (!query || !query[0])
        return -1;
    char safe[FCURVE_NAME_MAX];
    profile_sanitize_name(query, safe, sizeof(safe));
    for (int i = 0; i < g_ui.fc_n; i++) {
        if (ut_ascii_ieq(g_ui.fcurves[i], query) ||
            ut_ascii_ieq(g_ui.fcurves[i], safe))
            return i;
    }
    return -1;
}

static void apply_fcurve_to_snap_side(profile_meta_t *m, const fcurve_meta_t *fm,
                                      bool gpu)
{
    if (!gpu) {
        if (fm->cpu_on == 0 && !fm->has_cpu) {
            m->fan_cpu_on = 0;
            m->fan_cpu_t[0] = m->fan_cpu_p[0] = '\0';
        } else if (fm->has_cpu) {
            m->fan_cpu_on = fm->cpu_on == 0 ? 0 : 1;
            fan_to_csv(&fm->cpu, m->fan_cpu_t, sizeof(m->fan_cpu_t),
                       m->fan_cpu_p, sizeof(m->fan_cpu_p));
        }
    } else {
        if (fm->gpu_on == 0 && !fm->has_gpu) {
            m->fan_gpu_on = 0;
            m->fan_gpu_t[0] = m->fan_gpu_p[0] = '\0';
        } else if (fm->has_gpu) {
            m->fan_gpu_on = fm->gpu_on == 0 ? 0 : 1;
            fan_to_csv(&fm->gpu, m->fan_gpu_t, sizeof(m->fan_gpu_t),
                       m->fan_gpu_p, sizeof(m->fan_gpu_p));
        }
    }
}

/* Step SF_FAN_CPU / SF_FAN_GPU through:
 *   0: off (factory auto)
 *   1..K: every saved fan curve carrying this fan
 *   K+1..K+3: Quiet / Balanced / Performance factory reference
 *   last (optional): live EC curve when distinct from all above */
static void step_snap_fan_field(profile_meta_t *m, const hw_state_t *hw,
                                bool gpu, int dir)
{
    enum { MAX_CHOICES = MAX_FCURVES + 6 };
    struct {
        int kind; /* 0 off, 1 saved idx, 2 builtin preset, 3 live */
        int arg;
        fan_curve_t fc8;
    } ch[MAX_CHOICES];
    int nch = 0;

    ch[nch].kind = 0;
    ch[nch].arg = 0;
    fan_default(&ch[nch].fc8);
    nch++;

    for (int i = 0; i < g_ui.fc_n && nch < MAX_CHOICES - 4; i++) {
        if (!s_fc_ok[i])
            continue;
        bool has = gpu ? s_fc_meta[i].has_gpu : s_fc_meta[i].has_cpu;
        if (!has)
            continue;
        ch[nch].kind = 1;
        ch[nch].arg = i;
        ch[nch].fc8 = gpu ? s_fc_meta[i].gpu : s_fc_meta[i].cpu;
        nch++;
    }

    for (int p = 0; p < 3 && nch < MAX_CHOICES - 1; p++) {
        fan_curve_t rc, rg;
        if (fan_preset_builtin((fan_preset_t)p, &rc, &rg) == 0) {
            ch[nch].kind = 2;
            ch[nch].arg = p;
            ch[nch].fc8 = gpu ? rg : rc;
            nch++;
        }
    }

    fan_curve_t live8;
    fan_to_hw8(gpu ? &hw->fan_gpu : &hw->fan_cpu, &live8);
    bool live_dup = false;
    for (int i = 1; i < nch; i++) {
        if (fan_curves_equal(&ch[i].fc8, &live8)) {
            live_dup = true;
            break;
        }
    }
    if (!live_dup && nch < MAX_CHOICES) {
        ch[nch].kind = 3;
        ch[nch].arg = 0;
        ch[nch].fc8 = live8;
        nch++;
    }

    int cur_on = gpu ? m->fan_gpu_on : m->fan_cpu_on;
    const char *ct = gpu ? m->fan_gpu_t : m->fan_cpu_t;
    const char *cp = gpu ? m->fan_gpu_p : m->fan_cpu_p;
    fan_curve_t cur_fc, cur8;
    bool have_cur = ct[0] && cp[0] && fan_from_csv(&cur_fc, ct, cp) > 0;
    if (have_cur)
        fan_to_hw8(&cur_fc, &cur8);

    int cur_idx = -1;
    if (cur_on == 0) {
        cur_idx = 0;
    } else if (have_cur) {
        for (int i = 1; i < nch; i++) {
            if (fan_curves_equal(&ch[i].fc8, &cur8)) {
                cur_idx = i;
                break;
            }
        }
    }
    int next = cur_idx < 0 ? (dir > 0 ? 1 % nch : 0)
                           : (cur_idx + dir + nch) % nch;

    char *dst_t = gpu ? m->fan_gpu_t : m->fan_cpu_t;
    char *dst_p = gpu ? m->fan_gpu_p : m->fan_cpu_p;
    size_t cap_t = sizeof(m->fan_cpu_t);
    size_t cap_p = sizeof(m->fan_cpu_p);
    int *dst_on = gpu ? &m->fan_gpu_on : &m->fan_cpu_on;

    if (ch[next].kind == 0) {
        *dst_on = 0;
        dst_t[0] = '\0';
        dst_p[0] = '\0';
        ut_log("snapshot %s fan: off (factory auto)", gpu ? "GPU" : "CPU");
    } else {
        *dst_on = 1;
        fan_to_csv(&ch[next].fc8, dst_t, cap_t, dst_p, cap_p);
        if (ch[next].kind == 1)
            ut_log("snapshot %s fan: saved curve '%s' (press 'a' for both fans)",
                   gpu ? "GPU" : "CPU", g_ui.fcurves[ch[next].arg]);
        else if (ch[next].kind == 2)
            ut_log("snapshot %s fan: %s factory ref",
                   gpu ? "GPU" : "CPU",
                   fan_preset_name((fan_preset_t)ch[next].arg));
        else
            ut_log("snapshot %s fan: live EC curve", gpu ? "GPU" : "CPU");
    }
}

static void apply_snap_fan_both(profile_meta_t *m, bool from_gpu)
{
    const char *ct = from_gpu ? m->fan_gpu_t : m->fan_cpu_t;
    const char *cp = from_gpu ? m->fan_gpu_p : m->fan_cpu_p;
    int on = from_gpu ? m->fan_gpu_on : m->fan_cpu_on;
    if (on == 0) {
        m->fan_cpu_on = m->fan_gpu_on = 0;
        m->fan_cpu_t[0] = m->fan_cpu_p[0] = '\0';
        m->fan_gpu_t[0] = m->fan_gpu_p[0] = '\0';
        g_ui.prof_edit_dirty = true;
        ut_log("snapshot editor: set both CPU & GPU fans to factory auto (off)");
        return;
    }
    fan_curve_t fc, fc8;
    if (!ct[0] || !cp[0] || fan_from_csv(&fc, ct, cp) < 0)
        return;
    fan_to_hw8(&fc, &fc8);
    int src = identify_curve_source(&fc8, from_gpu, NULL, 0);
    if (src >= 0 && src < g_ui.fc_n && s_fc_ok[src]) {
        apply_fcurve_to_snap_side(m, &s_fc_meta[src], false);
        apply_fcurve_to_snap_side(m, &s_fc_meta[src], true);
        g_ui.prof_edit_dirty = true;
        ut_log("snapshot editor: applied saved curve '%s' to CPU & GPU",
               g_ui.fcurves[src]);
        return;
    }
    if (src <= -2 && src >= -4) {
        fan_preset_t p = (fan_preset_t)(-2 - src);
        fan_curve_t rc, rg;
        if (fan_preset_builtin(p, &rc, &rg) == 0) {
            m->fan_cpu_on = m->fan_gpu_on = 1;
            fan_to_csv(&rc, m->fan_cpu_t, sizeof(m->fan_cpu_t),
                       m->fan_cpu_p, sizeof(m->fan_cpu_p));
            fan_to_csv(&rg, m->fan_gpu_t, sizeof(m->fan_gpu_t),
                       m->fan_gpu_p, sizeof(m->fan_gpu_p));
            g_ui.prof_edit_dirty = true;
            ut_log("snapshot editor: applied %s ref to CPU & GPU",
                   fan_preset_name(p));
            return;
        }
    }
    m->fan_cpu_on = m->fan_gpu_on = 1;
    fan_to_csv(&fc8, m->fan_cpu_t, sizeof(m->fan_cpu_t),
               m->fan_cpu_p, sizeof(m->fan_cpu_p));
    fan_to_csv(&fc8, m->fan_gpu_t, sizeof(m->fan_gpu_t),
               m->fan_gpu_p, sizeof(m->fan_gpu_p));
    g_ui.prof_edit_dirty = true;
    ut_log("snapshot editor: copied curve to both CPU & GPU");
}

/* ---- field inspection & formatting -------------------------------------- */

static bool fan_csv_differs(int on, const char *t, const char *p,
                            const fan_curve_t *live)
{
    fan_curve_t fc, fc8, lv8;
    if (on == 0 || !t[0] || !p[0])
        return false;
    if (fan_from_csv(&fc, t, p) < 0)
        return true;
    fan_to_hw8(&fc, &fc8);
    fan_to_hw8(live, &lv8);
    return !fan_curves_equal(&fc8, &lv8);
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
    case SF_AURA:      return m->aura[0] == '\0';
    case SF_ASUSD_AC:  return m->asusd_ac < -1;
    case SF_ASUSD_BAT: return m->asusd_bat < -1;
    case SF_FAN_CPU:   return m->fan_cpu_on < 0 && m->fan_cpu_t[0] == '\0';
    case SF_FAN_GPU:   return m->fan_gpu_on < 0 && m->fan_gpu_t[0] == '\0';
    default:           return true;
    }
}

static const char *asusd_opt(int v)
{
    return v == -1 ? "off"
                   : hw_profile_name((hw_profile_t)ut_clamp_i(v, 0, HW_PROF_COUNT - 1));
}

static void aura_val(const char *aura, char *out, size_t n)
{
    char eff[32], hex[8];
    if (profile_aura_split(aura, eff, sizeof(eff), hex, sizeof(hex)) != 0) {
        snprintf(out, n, "%s", aura);
        return;
    }
    snprintf(out, n, "%s #%s", eff, hex);
}

static void format_fan_val(int on, const char *t, const char *p, bool gpu,
                           bool live_on, const fan_curve_t *live_fc,
                           char *sv, size_t svn, char *lv, size_t lvn,
                           bool *diff)
{
    fan_curve_t fc, fc8;
    bool has_c = t[0] && p[0] && fan_from_csv(&fc, t, p) > 0;
    if (on == 0) {
        snprintf(sv, svn, "off (factory auto)");
    } else if (has_c) {
        char tag[64];
        fan_to_hw8(&fc, &fc8);
        const char *st = on == 1 ? "on" : "curve";
        if (identify_curve_source(&fc8, gpu, tag, sizeof(tag)) != -1)
            snprintf(sv, svn, "%s · %.32s (%d-%d°C)", st, tag,
                     fc8.temp_c[0], fc8.temp_c[fc8.n - 1]);
        else
            snprintf(sv, svn, "%s · %dpt (%d-%d°C)", st, fc.n,
                     fc.temp_c[0], fc.temp_c[fc.n - 1]);
    } else {
        snprintf(sv, svn, "%s", on == 1 ? "on" : "curve");
    }
    snprintf(lv, lvn, "%s", live_on ? "on" : "off");
    *diff = (on >= 0 && on != (int)live_on) ||
            fan_csv_differs(on, t, p, live_fc);
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
    case SF_AURA:
        aura_val(m->aura, sv, sizeof(sv));
        if (hw->has_aura && hw->aura_rgb >= 0) {
            char lab[24];
            snprintf(lv, sizeof(lv), "%s #%06x",
                     hw_aura_mode_label(hw->aura_mode, lab, sizeof(lab)),
                     hw->aura_rgb);
        } else if (hw->has_aura) {
            char lab[24];
            snprintf(lv, sizeof(lv), "%s",
                     hw_aura_mode_label(hw->aura_mode, lab, sizeof(lab)));
        }
        *differs = profile_aura_differs(hw, m->aura);
        break;
    case SF_ASUSD_AC:
    case SF_ASUSD_BAT: {
        int v = f == SF_ASUSD_AC ? m->asusd_ac : m->asusd_bat;
        int lvv = f == SF_ASUSD_AC ? hw->asusd_ac : hw->asusd_bat;
        snprintf(sv, sizeof(sv), "%s", asusd_opt(v));
        if (lvv >= -1)
            snprintf(lv, sizeof(lv), "%s", asusd_opt(lvv));
        *differs = lvv >= -1 && v != lvv;
        break;
    }
    case SF_FAN_CPU:
        format_fan_val(m->fan_cpu_on, m->fan_cpu_t, m->fan_cpu_p, false,
                       hw->fan_cpu_on, &hw->fan_cpu,
                       sv, sizeof(sv), lv, sizeof(lv), differs);
        break;
    case SF_FAN_GPU:
        format_fan_val(m->fan_gpu_on, m->fan_gpu_t, m->fan_gpu_p, true,
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

/* ---- snapshot interactive field editing --------------------------------- */

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
    case SF_AURA:
        snprintf(m->aura, sizeof(m->aura), "%s",
                 live.aura[0] ? live.aura : "static 00ffff");
        break;
    case SF_ASUSD_AC:
        m->asusd_ac = live.asusd_ac >= -1 ? live.asusd_ac : -1;
        break;
    case SF_ASUSD_BAT:
        m->asusd_bat = live.asusd_bat >= -1 ? live.asusd_bat : -1;
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
        case SF_ASUSD_AC:  m->asusd_ac = -2; break;
        case SF_ASUSD_BAT: m->asusd_bat = -2; break;
        case SF_HZ:        m->hz = 0; break;
        case SF_PANEL_OD:  m->panel_od = -1; break;
        case SF_BAT:       m->bat = 0; break;
        case SF_KBD:       m->kbd[0] = '\0'; break;
        case SF_AURA:      m->aura[0] = '\0'; break;
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
    if (f != SF_FAN_CPU && f != SF_FAN_GPU && field_is_omitted(m, f)) {
        seed_field_from_live(m, hw, f);
        g_ui.prof_edit_dirty = true;
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
        int smin, smax, pmin, pmax, fmin, fmax;
        ctrl_ppt_limits(hw, &smin, &smax, &pmin, &pmax, &fmin, &fmax);
        if (m->ppt_off == 1 || m->ppt_spl <= 0) {
            m->ppt_off = -1;
            m->ppt_spl = 60;
            m->ppt_sppt = 75;
            m->ppt_fppt = 75;
        }
        m->ppt_spl  += 5 * dir;
        m->ppt_sppt += 5 * dir;
        m->ppt_fppt += 5 * dir;
        ctrl_ppt_order(&m->ppt_spl, &m->ppt_sppt, &m->ppt_fppt,
                       smin, smax, pmin, pmax, fmin, fmax);
        break;
    }
    case SF_GPU_CLOCK:
        if (m->gpu_clock <= 0) {
            m->gpu_clock = dir > 0 ? 1200 : 2100;
        } else {
            int nx = m->gpu_clock + dir * 75;
            if (nx < 600)
                m->gpu_clock = 0;
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
    case SF_ASUSD_AC: {
        int v = (m->asusd_ac < -1 ? -1 : m->asusd_ac) + 1;
        m->asusd_ac = (v + dir + 4) % 4 - 1;
        break;
    }
    case SF_ASUSD_BAT: {
        int v = (m->asusd_bat < -1 ? -1 : m->asusd_bat) + 1;
        m->asusd_bat = (v + dir + 4) % 4 - 1;
        break;
    }
    case SF_AURA: {
        char eff[32], hex[8];
        profile_aura_split(m->aura, eff, sizeof(eff), hex, sizeof(hex));
        int idx = AURA_COLOR_COUNT;
        for (int i = 0; i < AURA_COLOR_COUNT; i++)
            if (!strcasecmp(AURA_COLOR_HEX[i], hex))
                idx = i;
        idx = idx == AURA_COLOR_COUNT ? (dir > 0 ? 0 : AURA_COLOR_COUNT - 1)
                                      : (idx + dir + AURA_COLOR_COUNT) % AURA_COLOR_COUNT;
        snprintf(m->aura, sizeof(m->aura), "%.16s %.6s",
                 eff[0] ? eff : "static", AURA_COLOR_HEX[idx]);
        break;
    }
    case SF_FAN_CPU:
        step_snap_fan_field(m, hw, false, dir);
        break;
    case SF_FAN_GPU:
        step_snap_fan_field(m, hw, true, dir);
        break;
    default:
        break;
    }
    g_ui.prof_edit_dirty = true;
}

static void capture_live_curve(profile_meta_t *m, const hw_state_t *hw, int f)
{
    if (f == SF_AURA || f == SF_ASUSD_AC || f == SF_ASUSD_BAT) {
        seed_field_from_live(m, hw, f);
        g_ui.prof_edit_dirty = true;
        ut_log("snapshot editor: captured live %s", SF_LABELS[f]);
        return;
    }
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
    fan_curve_t c8, g8;
    fan_to_hw8(&hw->fan_cpu, &c8);
    fan_to_hw8(&hw->fan_gpu, &g8);
    if (f == SF_FAN_CPU || f != SF_FAN_GPU) {
        fan_to_csv(&c8, m->fan_cpu_t, sizeof(m->fan_cpu_t),
                   m->fan_cpu_p, sizeof(m->fan_cpu_p));
        m->fan_cpu_on = hw->fan_cpu_on ? 1 : 0;
    }
    if (f == SF_FAN_GPU || f != SF_FAN_CPU) {
        fan_to_csv(&g8, m->fan_gpu_t, sizeof(m->fan_gpu_t),
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
    char seed[64] = "";
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
    case SF_ASUSD_AC:
    case SF_ASUSD_BAT: {
        int v = f == SF_ASUSD_AC ? m->asusd_ac : m->asusd_bat;
        snprintf(seed, sizeof(seed), "%s", asusd_opt(v < -1 ? -1 : v));
        break;
    }
    case SF_AURA:
        snprintf(seed, sizeof(seed), "%s",
                 m->aura[0] ? m->aura : "static 00ffff");
        break;
    case SF_FAN_CPU:
    case SF_FAN_GPU: {
        bool gpu = (f == SF_FAN_GPU);
        int on = gpu ? m->fan_gpu_on : m->fan_cpu_on;
        const char *ct = gpu ? m->fan_gpu_t : m->fan_cpu_t;
        const char *cp = gpu ? m->fan_gpu_p : m->fan_cpu_p;
        fan_curve_t fc, fc8;
        if (on == 0) {
            snprintf(seed, sizeof(seed), "off");
        } else if (ct[0] && cp[0] && fan_from_csv(&fc, ct, cp) > 0) {
            fan_to_hw8(&fc, &fc8);
            int src = identify_curve_source(&fc8, gpu, NULL, 0);
            if (src >= 0 && src < g_ui.fc_n)
                snprintf(seed, sizeof(seed), "%s", g_ui.fcurves[src]);
            else if (src <= -2 && src >= -4)
                snprintf(seed, sizeof(seed), "%s",
                         fan_preset_name((fan_preset_t)(-2 - src)));
            else if (g_ui.fc_n > 0)
                snprintf(seed, sizeof(seed), "%s", g_ui.fcurves[0]);
            else
                snprintf(seed, sizeof(seed), "balanced");
        } else if (g_ui.fc_n > 0) {
            snprintf(seed, sizeof(seed), "%s", g_ui.fcurves[0]);
        } else {
            snprintf(seed, sizeof(seed), "balanced");
        }
        break;
    }
    default:
        step_field(m, hw, f, +1);
        return;
    }
    tin_set(&g_ui.prof_val_in, seed);
    g_ui.prof_val_typing = true;
}

static void commit_exact_edit(profile_meta_t *m, const hw_state_t *hw, int f)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "%.159s", g_ui.prof_val_in.buf);
    char *s = ut_trim(buf);
    if (!*s || !strcmp(s, "-") || ut_ascii_ieq(s, "skip")) {
        if (!field_is_omitted(m, f))
            toggle_skip_field(m, hw, f);
        return;
    }

    switch (f) {
    case SF_FREQ: {
        char rerr[128];
        if (profile_core_rule(m, hw, s, rerr, sizeof(rerr)) != 0)
            ut_log("%s", rerr);
        else
            g_ui.prof_edit_dirty = true;
        break;
    }
    case SF_ASUSD_AC:
    case SF_ASUSD_BAT: {
        int v = -1;
        if (!ut_ascii_ieq(s, "off")) {
            v = hw_profile_from_name(s);
            if (v < 0) {
                ut_log("asusd profile expects off|quiet|balanced|performance");
                break;
            }
        }
        if (f == SF_ASUSD_AC)
            m->asusd_ac = v;
        else
            m->asusd_bat = v;
        g_ui.prof_edit_dirty = true;
        break;
    }
    case SF_AURA: {
        char norm[24], eff[32], hex[8];
        if (profile_aura_normalize(s, norm, sizeof(norm)) != 0) {
            ut_log("keyboard colour expects '<effect> <colour|hex>', e.g. static 1a2b3c");
            break;
        }
        snprintf(m->aura, sizeof(m->aura), "%s", norm);
        g_ui.prof_edit_dirty = true;
        if (profile_aura_split(norm, eff, sizeof(eff), hex, sizeof(hex)) == 0 &&
            !ctrl_aura_supported(hw, ctrl_aura_effect_idx(eff)))
            ut_log("note: '%s' is not supported by this keyboard", eff);
        break;
    }
    case SF_PPT: {
        int a = 0, b = 0, c = 0;
        if (ut_ascii_ieq(s, "off")) {
            int smin, smax, pmin, pmax, fmin, fmax;
            m->ppt_off = 1; m->ppt_spl = m->ppt_sppt = m->ppt_fppt = 0;
            g_ui.prof_edit_dirty = true;
            if (!ctrl_ppt_limits(hw, &smin, &smax, &pmin, &pmax, &fmin, &fmax))
                ut_log("note: 'ppt = off' needs BIOS max_value (unavailable on this machine)");
        } else if (ut_ascii_ieq(s, "Q45")) {
            m->ppt_off = -1; m->ppt_spl = 45; m->ppt_sppt = 55; m->ppt_fppt = 55;
            g_ui.prof_edit_dirty = true;
        } else if (ut_ascii_ieq(s, "B60")) {
            m->ppt_off = -1; m->ppt_spl = 60; m->ppt_sppt = 75; m->ppt_fppt = 75;
            g_ui.prof_edit_dirty = true;
        } else if (ut_ascii_ieq(s, "P80")) {
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
            ut_log("ppt expects watts SPL,SPPT,FPPT (e.g. 60,75,75; one value = all three) or off");
        }
        break;
    }
    case SF_GPU_CLOCK:
        if (ut_ascii_ieq(s, "reset") || ut_ascii_ieq(s, "off") || !strcmp(s, "0")) {
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
    case SF_FAN_CPU:
    case SF_FAN_GPU: {
        bool gpu = (f == SF_FAN_GPU);
        char *dst_t = gpu ? m->fan_gpu_t : m->fan_cpu_t;
        char *dst_p = gpu ? m->fan_gpu_p : m->fan_cpu_p;
        int *dst_on = gpu ? &m->fan_gpu_on : &m->fan_cpu_on;

        if (ut_ascii_ieq(s, "off") || ut_ascii_ieq(s, "auto") ||
            ut_ascii_ieq(s, "stock")) {
            *dst_on = 0;
            dst_t[0] = dst_p[0] = '\0';
            g_ui.prof_edit_dirty = true;
            break;
        }
        int fci = find_fcurve_by_name(s);
        if (fci >= 0 && s_fc_ok[fci]) {
            apply_fcurve_to_snap_side(m, &s_fc_meta[fci], gpu);
            g_ui.prof_edit_dirty = true;
            ut_log("snapshot %s fan: saved curve '%s'",
                   gpu ? "GPU" : "CPU", g_ui.fcurves[fci]);
            break;
        }
        int pr = hw_profile_from_name(s);
        if (pr >= 0) {
            fan_curve_t rc, rg;
            if (fan_preset_builtin((fan_preset_t)pr, &rc, &rg) == 0) {
                *dst_on = 1;
                fan_to_csv(gpu ? &rg : &rc, dst_t, 64, dst_p, 64);
                g_ui.prof_edit_dirty = true;
                break;
            }
        }
        char tcsv[64] = {0}, pcsv[64] = {0};
        fan_curve_t fc, fc8;
        if (sscanf(s, "%63s %63s", tcsv, pcsv) == 2 &&
            fan_from_csv(&fc, tcsv, pcsv) > 0) {
            fan_to_hw8(&fc, &fc8);
            *dst_on = 1;
            fan_to_csv(&fc8, dst_t, 64, dst_p, 64);
            g_ui.prof_edit_dirty = true;
            break;
        }
        ut_log("fan curve expects a saved curve name, quiet|balanced|performance, off, or '<temps> <pwms>'");
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

/* ---- saved fan curve interactive editor --------------------------------- */

static bool fc_row_is_gpu(int row)
{
    return row >= FCF_GPU_MODE;
}

static int fc_row_pt(int row)
{
    if (row >= FCF_CPU_PT1 && row < FCF_CPU_PT1 + FAN_POINTS)
        return row - FCF_CPU_PT1;
    if (row >= FCF_GPU_PT1 && row < FCF_GPU_PT1 + FAN_POINTS)
        return row - FCF_GPU_PT1;
    return -1;
}

static void fc_ensure_side_seeded(fcurve_meta_t *fm, const hw_state_t *hw, bool gpu)
{
    if (!gpu) {
        if (!fm->has_cpu) {
            fan_to_hw8(&hw->fan_cpu, &fm->cpu);
            fm->has_cpu = true;
        }
        if (fm->cpu_on < 0)
            fm->cpu_on = 1;
    } else {
        if (!fm->has_gpu) {
            fan_to_hw8(&hw->fan_gpu, &fm->gpu);
            fm->has_gpu = true;
        }
        if (fm->gpu_on < 0)
            fm->gpu_on = 1;
    }
}

static void fc_toggle_skip(fcurve_meta_t *fm, const hw_state_t *hw, int row)
{
    bool gpu = fc_row_is_gpu(row);
    int *on = gpu ? &fm->gpu_on : &fm->cpu_on;
    bool *has = gpu ? &fm->has_gpu : &fm->has_cpu;
    if (*on < 0 && !*has) {
        fc_ensure_side_seeded(fm, hw, gpu);
    } else {
        *on = -1;
        *has = false;
    }
    g_ui.fc_edit_dirty = true;
}

static void fc_step_row(fcurve_meta_t *fm, const hw_state_t *hw,
                        int row, int dtemp, int dpwm)
{
    bool gpu = fc_row_is_gpu(row);
    int pt = fc_row_pt(row);
    fc_ensure_side_seeded(fm, hw, gpu);
    if (pt < 0) {
        int *on = gpu ? &fm->gpu_on : &fm->cpu_on;
        *on = (*on == 1) ? 0 : 1;
    } else {
        int *on = gpu ? &fm->gpu_on : &fm->cpu_on;
        *on = 1;
        fan_curve_t *fc = gpu ? &fm->gpu : &fm->cpu;
        fan_to_hw8(fc, fc);
        int np = fan_nudge_point(fc, pt, dtemp, dpwm);
        if (np >= 0 && np < FAN_POINTS)
            g_ui.fc_edit_sel = (gpu ? FCF_GPU_PT1 : FCF_CPU_PT1) + np;
    }
    g_ui.fc_edit_dirty = true;
}

static void fc_cycle_ref(fcurve_meta_t *fm, int row)
{
    static int s_ref_idx;
    bool gpu = fc_row_is_gpu(row);
    fan_curve_t rc, rg;
    fan_preset_t p = (fan_preset_t)(s_ref_idx % 3);
    s_ref_idx = (s_ref_idx + 1) % 3;
    if (fan_preset_builtin(p, &rc, &rg) != 0)
        return;
    if (!gpu) {
        fm->cpu = rc;
        fm->has_cpu = true;
        fm->cpu_on = 1;
    } else {
        fm->gpu = rg;
        fm->has_gpu = true;
        fm->gpu_on = 1;
    }
    g_ui.fc_edit_dirty = true;
    ut_log("fan curve editor: loaded %s factory reference into %s",
           fan_preset_name(p), gpu ? "GPU" : "CPU");
}

static void fc_capture_live(fcurve_meta_t *fm, const hw_state_t *hw, int row)
{
    bool gpu = fc_row_is_gpu(row);
    if (!gpu) {
        fan_to_hw8(&hw->fan_cpu, &fm->cpu);
        fm->has_cpu = true;
        fm->cpu_on = 1;
    } else {
        fan_to_hw8(&hw->fan_gpu, &fm->gpu);
        fm->has_gpu = true;
        fm->gpu_on = 1;
    }
    g_ui.fc_edit_dirty = true;
    ut_log("fan curve editor: captured live %s curve", gpu ? "GPU" : "CPU");
}

static void fc_open_in_graph(const fcurve_meta_t *fm, const char *name)
{
    if (fm->has_cpu)
        g_ui.hw->fan_cpu = fm->cpu;
    if (fm->cpu_on >= 0)
        g_ui.hw->fan_cpu_on = (fm->cpu_on != 0);
    if (fm->has_gpu)
        g_ui.hw->fan_gpu = fm->gpu;
    if (fm->gpu_on >= 0)
        g_ui.hw->fan_gpu_on = (fm->gpu_on != 0);
    g_ui.hw->fan_staged = true;
    snprintf(g_ui.fe_loaded, sizeof(g_ui.fe_loaded), "%s", name ? name : "");
    g_ui.fe_gpu = (g_ui.prof_in_edit && fc_row_is_gpu(g_ui.fc_edit_sel)) ? 1 : 0;
    ui_set_view(VIEW_FANS);
    ut_log("opened fan curve '%s' in 2 Fans graph (s saves back, w writes EC)",
           name ? name : "");
}

static void fc_start_exact_edit(fcurve_meta_t *fm, const hw_state_t *hw, int row)
{
    int pt = fc_row_pt(row);
    if (pt < 0) {
        fc_step_row(fm, hw, row, 0, 0);
        return;
    }
    bool gpu = fc_row_is_gpu(row);
    fc_ensure_side_seeded(fm, hw, gpu);
    const fan_curve_t *fc = gpu ? &fm->gpu : &fm->cpu;
    char seed[32];
    snprintf(seed, sizeof(seed), "%d %d%%",
             fc->temp_c[pt], fan_pwm_to_pct(fc->pwm[pt]));
    tin_set(&g_ui.prof_val_in, seed);
    g_ui.prof_val_typing = true;
}

static int parse_fc_pwm_tok(const char *tok)
{
    if (strchr(tok, '%'))
        return fan_pct_to_pwm(atoi(tok));
    return ut_clamp_i(atoi(tok), 0, 255);
}

static void fc_commit_exact_edit(fcurve_meta_t *fm, const hw_state_t *hw, int row)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%.63s", g_ui.prof_val_in.buf);
    char *s = ut_trim(buf);
    if (!*s || !strcmp(s, "-") || ut_ascii_ieq(s, "skip")) {
        fc_toggle_skip(fm, hw, row);
        return;
    }
    int pt = fc_row_pt(row);
    if (pt < 0)
        return;

    bool gpu = fc_row_is_gpu(row);
    fc_ensure_side_seeded(fm, hw, gpu);
    fan_curve_t *fc = gpu ? &fm->gpu : &fm->cpu;
    int *(on) = gpu ? &fm->gpu_on : &fm->cpu_on;
    *on = 1;

    for (char *p = s; *p; p++)
        if (*p == ',' || *p == ':')
            *p = ' ';

    char w1[24] = {0}, w2[24] = {0};
    int n = sscanf(s, "%23s %23s", w1, w2);
    int temp = fc->temp_c[pt], pwm;
    if (n >= 2) {
        temp = atoi(w1);
        pwm = parse_fc_pwm_tok(w2);
    } else if (n == 1) {
        pwm = parse_fc_pwm_tok(w1);
    } else {
        ut_log("point expects '<temp> <pwm|pct%%>' (e.g. '65 50%%' or '65 128')");
        return;
    }
    int np = fan_set_point(fc, pt, temp, pwm);
    if (np >= 0 && np < FAN_POINTS)
        g_ui.fc_edit_sel = (gpu ? FCF_GPU_PT1 : FCF_CPU_PT1) + np;
    g_ui.fc_edit_dirty = true;
}

static void save_edited_fcurve(bool exit_to_list)
{
    if (g_ui.fc_n <= 0 || g_ui.fc_sel < 0 || g_ui.fc_sel >= g_ui.fc_n)
        return;
    char name[FCURVE_NAME_MAX];
    snprintf(name, sizeof(name), "%s", g_ui.fcurves[g_ui.fc_sel]);
    if (fcurve_save(name, &g_ui.fc_edit) == 0) {
        panel_profiles_reload();
        if (exit_to_list)
            g_ui.prof_in_edit = false;
        snprintf(g_ui.pw_msg, sizeof(g_ui.pw_msg),
                 "✓ fan curve '%s' saved", name);
        g_ui.pw_msg_fail = false;
        g_ui.pw_msg_ms = ut_mono_ms();
        ut_log("fan curve '%s' updated", name);
    } else {
        snprintf(g_ui.pw_msg, sizeof(g_ui.pw_msg),
                 "⚠ fan curve '%s': save FAILED", name);
        g_ui.pw_msg_fail = true;
        g_ui.pw_msg_ms = ut_mono_ms();
    }
}

/* ---- per-core grid (snapshot mode) -------------------------------------- */

static void prof_cores_load(void)
{
    memset(g_ui.prof_cf_staged, 0, sizeof(g_ui.prof_cf_staged));
    for (int i = 0; i < g_ui.prof_edit.core_n && i < HW_CPU_MAX; i++) {
        int id = g_ui.prof_edit.core_ids[i];
        if (id >= 0 && id < HW_CPU_MAX && g_ui.prof_edit.core_mhz[i] > 0)
            g_ui.prof_cf_staged[id] = g_ui.prof_edit.core_mhz[i];
    }
}

static void prof_cores_open(void)
{
    g_ui.prof_core_undo = g_ui.prof_edit;
    prof_cores_load();
    g_ui.cf_sel = 0;
    g_ui.cf_input_active = 0;
    g_ui.prof_in_cores = true;
}

void panel_profiles_cores_reload(void)
{
    if (!g_ui.prof_in_cores)
        return;
    g_ui.prof_edit = g_ui.prof_core_undo;
    prof_cores_load();
    g_ui.cf_input_active = 0;
    ut_log("core grid: back to the saved per-core list");
}

void panel_profiles_cores_close(int keep)
{
    if (!g_ui.prof_in_cores)
        return;
    profile_meta_t res = g_ui.prof_core_undo;
    bool changed = false;
    if (keep) {
        const hw_state_t *hw = g_ui.hw;
        bool has_agg = res.freq > 0;
        int n = 0;
        for (int c = 0; c < hw->cpu_n && c < HW_CPU_MAX; c++) {
            int id = hw->cpu_ids[c];
            int v = g_ui.prof_cf_staged[id];
            if (v > 0 && (!has_agg || v != res.freq) && n < HW_CPU_MAX) {
                res.core_ids[n] = id;
                res.core_mhz[n] = v;
                n++;
            }
        }
        changed = n != res.core_n ||
                  memcmp(res.core_ids, g_ui.prof_core_undo.core_ids,
                         (size_t)n * sizeof(int)) != 0 ||
                  memcmp(res.core_mhz, g_ui.prof_core_undo.core_mhz,
                         (size_t)n * sizeof(int)) != 0;
        res.core_n = n;
    }
    g_ui.prof_edit = res;
    if (changed) {
        g_ui.prof_edit_dirty = true;
        ut_log("core grid: %d thread(s) stored in the snapshot", res.core_n);
    }
    memset(g_ui.prof_cf_staged, 0, sizeof(g_ui.prof_cf_staged));
    g_ui.cf_input_active = 0;
    g_ui.prof_in_cores = false;
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

/* Split left column: top = SNAPSHOTS (.ctr), bottom = FAN CURVES (.fc). */
static void draw_list_pane(struct ncplane *n, const rect_t *r, bool show_foot)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int x = r->x, y = r->y, w = r->w;
    if (w < 12 || r->h < 6)
        return;

    int foot_h = show_foot ? 3 : 0;
    int body_h = r->h - foot_h;
    if (body_h < 6)
        body_h = r->h;

    int top_h = (body_h + 1) / 2;
    if (top_h < 3)
        top_h = 3;

    /* ---- Top half: SNAPSHOTS ---- */
    bool snap_foc = !g_ui.prof_in_fcurves && !g_ui.prof_in_edit;
    char hdr[72];
    snprintf(hdr, sizeof(hdr), "%sSNAPSHOTS (%d)",
             !g_ui.prof_in_fcurves ? "▸ " : "  ", g_ui.prof_n);
    ui_putln(n, x, y, w, hdr, snap_foc ? pal->accent : pal->muted,
             !g_ui.prof_in_fcurves);
    tgt_register(x, y, w, 1, TGT(TGT_PANEL_PROFILES, ACT_PL_HDR_SNAP));
    ui_hline(n, x, y + 1, w, pal->border);

    int s_rows = top_h - 2;
    if (s_rows < 1)
        s_rows = 1;
    if (g_ui.prof_n <= s_rows) {
        g_ui.prof_top = 0;
    } else {
        if (g_ui.prof_top > g_ui.prof_sel)
            g_ui.prof_top = g_ui.prof_sel;
        if (g_ui.prof_sel >= g_ui.prof_top + s_rows)
            g_ui.prof_top = g_ui.prof_sel - s_rows + 1;
        if (g_ui.prof_top + s_rows > g_ui.prof_n)
            g_ui.prof_top = g_ui.prof_n - s_rows;
        if (g_ui.prof_top < 0)
            g_ui.prof_top = 0;
    }

    if (g_ui.prof_n == 0) {
        ui_putln(n, x, y + 2, w, "(none — press s to save snapshot)",
                 pal->muted, false);
    } else {
        for (int i = g_ui.prof_top, shown = 0;
             i < g_ui.prof_n && shown < s_rows; i++, shown++) {
            int ry = y + 2 + shown;
            bool sel = (i == g_ui.prof_sel);
            char badge[20] = "";
            uint32_t bfg = pal->muted;
            if (!g_ui.prof_in_fcurves && sel && g_ui.prof_del_arm == i) {
                snprintf(badge, sizeof(badge), "DELETE?");
                bfg = pal->err;
            } else if (!g_ui.prof_in_fcurves && sel && g_ui.prof_upd_arm == i) {
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
            draw_snap_row(n, x, ry, w, g_ui.profs[i], badge, bfg,
                          sel && !g_ui.prof_in_fcurves, snap_foc);
            tgt_register(x, ry, w, 1, TGT(TGT_PANEL_PROFILES, ACT_PL_ITEM + i));
        }
    }

    /* ---- Bottom half: SAVED FAN CURVES ---- */
    int by = y + top_h;
    if (by + 2 < r->y + r->h - foot_h) {
        bool fc_foc = g_ui.prof_in_fcurves && !g_ui.prof_in_edit;
        snprintf(hdr, sizeof(hdr), "%sFAN CURVES (%d)  [f]",
                 g_ui.prof_in_fcurves ? "▸ " : "  ", g_ui.fc_n);
        ui_putln(n, x, by, w, hdr, fc_foc ? pal->accent : pal->muted,
                 g_ui.prof_in_fcurves);
        tgt_register(x, by, w, 1, TGT(TGT_PANEL_PROFILES, ACT_PL_HDR_FC));
        ui_hline(n, x, by + 1, w, pal->border);

        int f_rows = (r->y + body_h) - (by + 2);
        if (f_rows < 1)
            f_rows = 1;
        if (g_ui.fc_n <= f_rows) {
            g_ui.fc_top = 0;
        } else {
            if (g_ui.fc_top > g_ui.fc_sel)
                g_ui.fc_top = g_ui.fc_sel;
            if (g_ui.fc_sel >= g_ui.fc_top + f_rows)
                g_ui.fc_top = g_ui.fc_sel - f_rows + 1;
            if (g_ui.fc_top + f_rows > g_ui.fc_n)
                g_ui.fc_top = g_ui.fc_n - f_rows;
            if (g_ui.fc_top < 0)
                g_ui.fc_top = 0;
        }

        if (g_ui.fc_n == 0) {
            ui_putln(n, x, by + 2, w,
                     g_ui.prof_in_fcurves
                         ? "(none — press s to save fan curve)"
                         : "(none — f then s, or s in 2 Fans)",
                     pal->muted, false);
        } else {
            for (int i = g_ui.fc_top, shown = 0;
                 i < g_ui.fc_n && shown < f_rows; i++, shown++) {
                int ry = by + 2 + shown;
                bool sel = (i == g_ui.fc_sel);
                char badge[20] = "";
                uint32_t bfg = pal->muted;
                if (g_ui.prof_in_fcurves && sel && g_ui.prof_del_arm == i) {
                    snprintf(badge, sizeof(badge), "DELETE?");
                    bfg = pal->err;
                } else if (g_ui.prof_in_fcurves && sel && g_ui.prof_upd_arm == i) {
                    snprintf(badge, sizeof(badge), "UPDATE?");
                    bfg = pal->warn;
                } else if (s_fc_ok[i]) {
                    int nd = fcurve_diff_count(g_ui.hw, &s_fc_meta[i]);
                    if (nd == 0) {
                        snprintf(badge, sizeof(badge), "≡ live");
                        bfg = pal->ok;
                    } else {
                        snprintf(badge, sizeof(badge), "Δ %d", nd);
                        bfg = pal->warn;
                    }
                }
                draw_snap_row(n, x, ry, w, g_ui.fcurves[i], badge, bfg,
                              sel && g_ui.prof_in_fcurves, fc_foc);
                tgt_register(x, ry, w, 1,
                             TGT(TGT_PANEL_PROFILES, ACT_PL_FC_ITEM + i));
            }
        }
    }

    /* Narrow-mode compact footer when right inspector pane is hidden */
    if (show_foot && foot_h > 0) {
        int fy = r->y + r->h - foot_h;
        char sum[160] = "", diff[128] = "";
        bool have = false;
        if (g_ui.prof_in_fcurves && g_ui.fc_sel < g_ui.fc_n &&
            s_fc_ok[g_ui.fc_sel]) {
            fcurve_summary(&s_fc_meta[g_ui.fc_sel], sum, sizeof(sum));
            fcurve_diff(g_ui.hw, &s_fc_meta[g_ui.fc_sel], diff, sizeof(diff));
            have = true;
        } else if (!g_ui.prof_in_fcurves && g_ui.prof_sel < g_ui.prof_n &&
                   s_meta_ok[g_ui.prof_sel]) {
            profile_meta_summary(&s_meta[g_ui.prof_sel], sum, sizeof(sum));
            profile_meta_diff(g_ui.hw, &s_meta[g_ui.prof_sel], diff, sizeof(diff));
            have = true;
        }
        if (have && fy > y) {
            ui_hline(n, x, fy, w, pal->border);
            bool synced = strncmp(diff, "≡", 3) == 0;
            ui_putln(n, x, fy + 1, w, sum, pal->text, false);
            ui_putln(n, x, fy + 2, w, diff, synced ? pal->ok : pal->warn, false);
        }
    }
}

static void draw_fcurve_editor_pane(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int x = r->x, y = r->y, w = r->w;
    if (w < 16 || r->h < 5)
        return;

    if (g_ui.fc_n == 0 || g_ui.fc_sel >= g_ui.fc_n) {
        ui_putln(n, x, y++, w, "FAN CURVE INSPECTOR", pal->muted, true);
        ui_hline(n, x, y++, w, pal->border);
        ui_putln(n, x, y + 1, w,
                 "Save a fan curve with [s] (custom name), [n] (auto-name), or [s] in 2 Fans.",
                 pal->muted, false);
        ui_putln(n, x, y + 2, w,
                 "Saved fan curves can be edited here, renamed [r], or selected inside Snapshots!",
                 pal->muted, false);
        return;
    }

    const char *name = g_ui.fcurves[g_ui.fc_sel];
    const fcurve_meta_t *fm = g_ui.prof_in_edit ? &g_ui.fc_edit
                                                : &s_fc_meta[g_ui.fc_sel];

    char hdr[96];
    if (g_ui.prof_in_edit) {
        snprintf(hdr, sizeof(hdr), "EDIT FAN CURVE · %.44s%s",
                 name, g_ui.fc_edit_dirty ? " ●" : "");
        ui_putln(n, x, y, w, hdr,
                 g_ui.fc_edit_dirty ? pal->warn : pal->accent, true);
        const ui_btndef_t btns[] = {
            { "Save (w)", g_ui.fc_edit_dirty, TGT(TGT_PANEL_PROFILES, ACT_PL_BTN_SAVE) },
            { "Skip (x)", false,              TGT(TGT_PANEL_PROFILES, ACT_PL_BTN_SKIP) },
            { "Live (f)", false,              TGT(TGT_PANEL_PROFILES, ACT_PL_BTN_CURV) },
            { "Graph (o)", false,             TGT(TGT_PANEL_PROFILES, ACT_PL_BTN_OPEN) },
            { "Back", false,                  TGT(TGT_PANEL_PROFILES, ACT_PL_BTN_BACK) },
        };
        int bw = 47;
        if (w > bw + 24)
            ui_btn_row(n, y, x + w - bw, bw, btns, 5);
    } else {
        snprintf(hdr, sizeof(hdr), "FAN CURVE · %.48s", name);
        ui_putln(n, x, y, w, hdr, pal->muted, true);
        const ui_btndef_t btns[] = {
            { "Edit curve (e)", false, TGT(TGT_PANEL_PROFILES, ACT_PL_BTN_EDIT) },
            { "Graph (o)", false,      TGT(TGT_PANEL_PROFILES, ACT_PL_BTN_OPEN) },
        };
        int bw = 28;
        if (w > bw + 20)
            ui_btn_row(n, y, x + w - bw, bw, btns, 2);
    }
    y++;
    ui_hline(n, x, y++, w, pal->border);

    char meta_ln[160];
    snprintf(meta_ln, sizeof(meta_ln), "saved %s · note: %s",
             fm->saved[0] ? fm->saved : "-",
             fm->note[0] ? fm->note : "(none — press c)");
    ui_putln(n, x, y++, w, meta_ln, pal->muted, false);

    char diff_ln[160];
    fcurve_diff(g_ui.hw, fm, diff_ln, sizeof(diff_ln));
    bool synced = strncmp(diff_ln, "≡", 3) == 0;
    ui_putln(n, x, y++, w, diff_ln, synced ? pal->ok : pal->warn, false);
    y++;

    fan_curve_t lv_cpu, lv_gpu;
    fan_to_hw8(&g_ui.hw->fan_cpu, &lv_cpu);
    fan_to_hw8(&g_ui.hw->fan_gpu, &lv_gpu);

    int avail = (r->y + r->h) - y - 1;
    if (avail < 1)
        avail = 1;

    if (FCF_COUNT <= avail) {
        g_ui.fc_edit_top = 0;
    } else {
        int cur = g_ui.prof_in_edit ? g_ui.fc_edit_sel : 0;
        if (g_ui.fc_edit_top > cur)
            g_ui.fc_edit_top = cur;
        if (cur >= g_ui.fc_edit_top + avail)
            g_ui.fc_edit_top = cur - avail + 1;
        if (g_ui.fc_edit_top + avail > FCF_COUNT)
            g_ui.fc_edit_top = FCF_COUNT - avail;
        if (g_ui.fc_edit_top < 0)
            g_ui.fc_edit_top = 0;
    }

    for (int row = g_ui.fc_edit_top, shown = 0;
         row < FCF_COUNT && shown < avail; row++, shown++) {
        int ry = y + shown;
        bool sel = g_ui.prof_in_edit && (row == g_ui.fc_edit_sel);
        bool gpu = fc_row_is_gpu(row);
        int pt = fc_row_pt(row);
        int on = gpu ? fm->gpu_on : fm->cpu_on;
        bool has = gpu ? fm->has_gpu : fm->has_cpu;
        const fan_curve_t *fc = gpu ? &fm->gpu : &fm->cpu;
        const fan_curve_t *lv = gpu ? &lv_gpu : &lv_cpu;
        bool lv_on = gpu ? g_ui.hw->fan_gpu_on : g_ui.hw->fan_cpu_on;

        char label[40], val[128];
        uint32_t vfg;

        if (pt < 0) {
            snprintf(label, sizeof(label), "%s custom mode", gpu ? "GPU" : "CPU");
            if (on < 0 && !has) {
                snprintf(val, sizeof(val), "— (skip)");
                vfg = pal->muted;
            } else {
                int eff_on = (on == 0) ? 0 : 1;
                bool df = (eff_on != (int)lv_on);
                snprintf(val, sizeof(val), "%s%s",
                         eff_on ? "on (custom curve)" : "off (factory auto)",
                         df ? (lv_on ? "  (live: on)" : "  (live: off)") : "");
                vfg = df ? pal->warn : pal->text;
            }
        } else {
            snprintf(label, sizeof(label), "  %s point %d",
                     gpu ? "GPU" : "CPU", pt + 1);
            if (sel && g_ui.prof_val_typing) {
                char disp[72];
                tin_format(&g_ui.prof_val_in, disp, sizeof(disp), true);
                snprintf(val, sizeof(val), "[ %s ]", disp);
                draw_field_row(n, x, ry, w, label, val, pal->accent, true);
                tgt_register(x, ry, w, 1,
                             TGT(TGT_PANEL_PROFILES, ACT_PL_FC_FIELD + row));
                continue;
            }
            if ((on < 0 && !has) || !has) {
                snprintf(val, sizeof(val), "—");
                vfg = pal->muted;
            } else if (on == 0) {
                snprintf(val, sizeof(val), "%3d °C  ·  %3d%% (%d)  [inactive]",
                         fc->temp_c[pt], fan_pwm_to_pct(fc->pwm[pt]), fc->pwm[pt]);
                vfg = pal->muted;
            } else {
                bool df = (fc->temp_c[pt] != lv->temp_c[pt] ||
                           fc->pwm[pt] != lv->pwm[pt]);
                if (df)
                    snprintf(val, sizeof(val),
                             "%3d °C  →  %3d%% (%3d)  (live: %d°:%d%%)",
                             fc->temp_c[pt], fan_pwm_to_pct(fc->pwm[pt]),
                             fc->pwm[pt], lv->temp_c[pt],
                             fan_pwm_to_pct(lv->pwm[pt]));
                else
                    snprintf(val, sizeof(val), "%3d °C  →  %3d%% (%3d)",
                             fc->temp_c[pt], fan_pwm_to_pct(fc->pwm[pt]),
                             fc->pwm[pt]);
                vfg = df ? pal->warn : pal->text;
            }
        }
        draw_field_row(n, x, ry, w, label, val, vfg, sel);
        tgt_register(x, ry, w, 1, TGT(TGT_PANEL_PROFILES, ACT_PL_FC_FIELD + row));
    }

    int hy = r->y + r->h - 1;
    if (hy > y) {
        if (g_ui.prof_in_edit && g_ui.prof_val_typing) {
            ui_putln(n, x, hy, w,
                     "Enter ok · Esc cancel · '<temp> <pwm|pct%>' (e.g. '65 50%' or '65 128') or '-' to skip",
                     pal->accent, false);
        } else if (g_ui.prof_in_edit) {
            ui_putln(n, x, hy, w,
                     "h/l PWM ±5 · </> Temp ±1°C · e exact · m factory ref · f live · o graph · w/Enter save",
                     pal->muted, false);
        } else {
            ui_putln(n, x, hy, w,
                     "Enter apply to EC · e edit points · o open in 2 Fans graph · r rename · f back to snapshots",
                     pal->muted, false);
        }
    }
}

static void draw_editor_pane(struct ncplane *n, const rect_t *r)
{
    if (g_ui.prof_in_fcurves) {
        draw_fcurve_editor_pane(n, r);
        return;
    }

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
                 "Press [f] to switch to Saved Fan Curves on the bottom left.",
                 pal->muted, false);
        return;
    }

    if (g_ui.prof_meta_sel != g_ui.prof_sel && !g_ui.prof_in_edit)
        sync_edit_from_sel();

    const char *name = g_ui.profs[g_ui.prof_sel];
    const profile_meta_t *m = g_ui.prof_in_edit ? &g_ui.prof_edit
                                                : &s_meta[g_ui.prof_sel];

    char hdr[96];
    if (g_ui.prof_in_edit) {
        snprintf(hdr, sizeof(hdr), "EDIT SNAPSHOT · %.48s%s",
                 name, g_ui.prof_edit_dirty ? " ●" : "");
        ui_putln(n, x, y, w, hdr,
                 g_ui.prof_edit_dirty ? pal->warn : pal->accent, true);
        if (g_ui.prof_in_cores) {
            const ui_btndef_t btns[] = {
                { "Save (w)", g_ui.prof_edit_dirty, TGT(TGT_PANEL_PROFILES, ACT_PL_BTN_SAVE) },
                { "Back", false,                    TGT(TGT_PANEL_PROFILES, ACT_PL_BTN_BACK) },
            };
            int bw = 17;
            if (w > bw + 24)
                ui_btn_row(n, y, x + w - bw, bw, btns, 2);
        } else {
            const ui_btndef_t btns[] = {
                { "Save (w)", g_ui.prof_edit_dirty, TGT(TGT_PANEL_PROFILES, ACT_PL_BTN_SAVE) },
                { "Skip (x)", false,                TGT(TGT_PANEL_PROFILES, ACT_PL_BTN_SKIP) },
                { "Live (f)", false,                TGT(TGT_PANEL_PROFILES, ACT_PL_BTN_CURV) },
                { "Back", false,                    TGT(TGT_PANEL_PROFILES, ACT_PL_BTN_BACK) },
            };
            int bw = 37;
            if (w > bw + 24)
                ui_btn_row(n, y, x + w - bw, bw, btns, 4);
        }
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

    if (g_ui.prof_in_cores) {
        rect_t cr = { x, y, w, (r->y + r->h) - y };
        editor_corefreq_draw(n, &cr);
        return;
    }

    char meta_ln[160];
    snprintf(meta_ln, sizeof(meta_ln), "saved %s · note: %s",
             m->saved[0] ? m->saved : "-",
             m->note[0] ? m->note : "(none — press c)");
    ui_putln(n, x, y++, w, meta_ln, pal->muted, false);

    char diff_ln[160];
    profile_meta_diff(g_ui.hw, m, diff_ln, sizeof(diff_ln));
    bool synced = strncmp(diff_ln, "≡", 3) == 0;
    ui_putln(n, x, y++, w, diff_ln, synced ? pal->ok : pal->warn, false);
    y++;

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
                    ? "Enter ok · Esc cancel · watts SPL,SPPT,FPPT (e.g. 60,75,75 · one value = all three) or off"
                : g_ui.prof_edit_sel == SF_GPU_CLOCK
                    ? "Enter ok · Esc cancel · MHz (180..3000) or 'reset' to unlock"
                : g_ui.prof_edit_sel == SF_FREQ
                    ? "Enter ok · Esc cancel · <mhz> | core <cpu-id> <mhz> | ccd <n> <mhz> | max | clear"
                : g_ui.prof_edit_sel == SF_AURA
                    ? "Enter ok · Esc cancel · <effect> <colour|hex>, e.g. static 1a2b3c"
                : g_ui.prof_edit_sel == SF_ASUSD_AC || g_ui.prof_edit_sel == SF_ASUSD_BAT
                    ? "Enter ok · Esc cancel · off|quiet|balanced|performance"
                : g_ui.prof_edit_sel == SF_FAN_CPU || g_ui.prof_edit_sel == SF_FAN_GPU
                    ? "Enter ok · Esc cancel · saved curve name, quiet|balanced|performance, off, or '-' skip"
                    : "Enter confirm · Esc cancel · '-' to skip field";
            ui_putln(n, x, hy, w, fmt_hint, pal->accent, false);
        } else if (g_ui.prof_in_edit &&
                   (g_ui.prof_edit_sel == SF_FAN_CPU ||
                    g_ui.prof_edit_sel == SF_FAN_GPU)) {
            ui_putln(n, x, hy, w,
                     "h/l pick saved fan curve / ref / off · a apply to CPU+GPU · e type name · x skip · w save",
                     pal->accent, false);
        } else if (g_ui.prof_in_edit) {
            ui_putln(n, x, hy, w,
                     "h/l change · x skip (—) · e exact · f live · c cores · w/Enter save",
                     pal->muted, false);
        } else {
            ui_putln(n, x, hy, w,
                     "Press e to edit snapshot · f switches left list to Saved Fan Curves",
                     pal->muted, false);
        }
    }
}

void panel_profiles_draw(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int x = r->x, y = r->y, w = r->w;
    if (w < 12 || r->h < 6)
        return;

    int cur_n   = g_ui.prof_in_fcurves ? g_ui.fc_n : g_ui.prof_n;
    int cur_sel = g_ui.prof_in_fcurves ? g_ui.fc_sel : g_ui.prof_sel;
    const char *cur_name = (cur_n > 0 && cur_sel >= 0 && cur_sel < cur_n)
        ? (g_ui.prof_in_fcurves ? g_ui.fcurves[cur_sel] : g_ui.profs[cur_sel])
        : "";

    if (g_ui.prof_del_arm >= cur_n || g_ui.prof_del_arm != cur_sel)
        g_ui.prof_del_arm = -1;
    if (g_ui.prof_upd_arm >= cur_n || g_ui.prof_upd_arm != cur_sel)
        g_ui.prof_upd_arm = -1;

    /* Top action / confirmation banner (full workspace width) */
    if (g_ui.prof_input != PL_INPUT_NONE) {
        const char *kind = g_ui.prof_in_fcurves ? "fan curve " : "";
        const char *act = g_ui.prof_input == PL_INPUT_SAVE   ? "save"
                        : g_ui.prof_input == PL_INPUT_RENAME ? "rename"
                                                             : "note";
        const tinput_t *fld = g_ui.prof_input == PL_INPUT_NOTE
                                  ? &g_ui.prof_note : &g_ui.prof_name;
        char disp[96], line[160];
        tin_format(fld, disp, sizeof(disp), true);
        snprintf(line, sizeof(line), "%s%s: %s   (Enter ok · Esc cancel)",
                 kind, act, disp);
        ui_putln(n, x, y++, w, line, pal->accent, true);
        if (g_ui.prof_input == PL_INPUT_SAVE) {
            char prev[160], line2[192];
            if (g_ui.prof_in_fcurves) {
                fcurve_meta_t tmp;
                fcurve_meta_from_hw(g_ui.hw, &tmp);
                fcurve_summary(&tmp, prev, sizeof(prev));
            } else {
                profile_capture_line(g_ui.hw, prev, sizeof(prev));
            }
            snprintf(line2, sizeof(line2), "will save: %.140s", prev);
            ui_putln(n, x, y++, w, line2, pal->muted, false);
        }
        ui_hline(n, x, y++, w, pal->border);
    } else if (g_ui.prof_del_arm >= 0 && g_ui.prof_del_arm == cur_sel && cur_name[0]) {
        char warn[140];
        snprintf(warn, sizeof(warn),
                 "press d again to delete %s'%.48s' · any other key cancels",
                 g_ui.prof_in_fcurves ? "fan curve " : "", cur_name);
        ui_putln(n, x, y++, w, warn, pal->err, true);
        ui_hline(n, x, y++, w, pal->border);
    } else if (g_ui.prof_upd_arm >= 0 && g_ui.prof_upd_arm == cur_sel && cur_name[0]) {
        char warn[160];
        snprintf(warn, sizeof(warn),
                 "press u again to overwrite %s'%.48s' with live hardware · any other key cancels",
                 g_ui.prof_in_fcurves ? "fan curve " : "", cur_name);
        ui_putln(n, x, y++, w, warn, pal->warn, true);
        ui_hline(n, x, y++, w, pal->border);
    }

    int rem_h = (r->y + r->h) - y;
    if (rem_h < 4)
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
    if (g_ui.prof_in_fcurves) {
        if (g_ui.fc_n == 0 || g_ui.fc_sel >= g_ui.fc_n)
            return;
        ui_spawn_apply(APPLY_FAN_CURVE, "fan curve", g_ui.fcurves[g_ui.fc_sel]);
        return;
    }
    if (g_ui.prof_n == 0 || g_ui.prof_sel >= g_ui.prof_n)
        return;
    ui_spawn_apply(APPLY_SNAPSHOT, "snapshot", g_ui.profs[g_ui.prof_sel]);
}

static void save_current(void)
{
    if (!g_ui.prof_name.buf[0])
        return;
    if (g_ui.prof_in_fcurves) {
        /* If user did not stage un-written curve edits, refresh from live EC */
        if (!g_ui.hw->fan_staged)
            hw_refresh_live(g_ui.hw);
        if (fcurve_export(g_ui.prof_name.buf, g_ui.hw) == 0) {
            char safe[FCURVE_NAME_MAX];
            profile_sanitize_name(g_ui.prof_name.buf, safe, sizeof(safe));
            panel_profiles_reload();
            for (int i = 0; i < g_ui.fc_n; i++)
                if (!strcmp(g_ui.fcurves[i], safe))
                    select_fcurve(i);
        }
        return;
    }
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
    if (g_ui.prof_in_fcurves) {
        if (g_ui.fc_n == 0 || g_ui.fc_sel >= g_ui.fc_n)
            return;
        const char *old = g_ui.fcurves[g_ui.fc_sel];
        if (fcurve_rename(old, g_ui.prof_name.buf) != 0)
            return;
        char safe[FCURVE_NAME_MAX];
        profile_sanitize_name(g_ui.prof_name.buf, safe, sizeof(safe));
        panel_profiles_reload();
        for (int i = 0; i < g_ui.fc_n; i++)
            if (!strcmp(g_ui.fcurves[i], safe))
                select_fcurve(i);
        return;
    }
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
    if (g_ui.prof_in_fcurves) {
        if (g_ui.fc_n == 0 || g_ui.fc_sel >= g_ui.fc_n)
            return;
        if (fcurve_set_note(g_ui.fcurves[g_ui.fc_sel], g_ui.prof_note.buf) == 0)
            panel_profiles_reload();
        return;
    }
    if (g_ui.prof_n == 0 || g_ui.prof_sel >= g_ui.prof_n)
        return;
    if (profile_set_note(g_ui.profs[g_ui.prof_sel], g_ui.prof_note.buf) == 0)
        panel_profiles_reload();
}

static void handle_fcurve_editor_key(uint32_t key)
{
    if (g_ui.prof_val_typing) {
        if (key == NCKEY_ESC) {
            g_ui.prof_val_typing = false;
            return;
        }
        if (key == NCKEY_ENTER || key == '\r' || key == '\n') {
            fc_commit_exact_edit(&g_ui.fc_edit, g_ui.hw, g_ui.fc_edit_sel);
            g_ui.prof_val_typing = false;
            return;
        }
        tin_key(&g_ui.prof_val_in, key);
        return;
    }

    switch (key) {
    case NCKEY_ESC:
        if (g_ui.fc_edit_dirty) {
            sync_fc_edit_from_sel();
            ut_log("fan curve edit discarded");
        }
        g_ui.prof_in_edit = false;
        break;
    case 'j':
    case NCKEY_DOWN:
        if (g_ui.fc_edit_sel + 1 < FCF_COUNT)
            g_ui.fc_edit_sel++;
        break;
    case 'k':
    case NCKEY_UP:
        if (g_ui.fc_edit_sel > 0)
            g_ui.fc_edit_sel--;
        break;
    case 'h':
    case NCKEY_LEFT:
        fc_step_row(&g_ui.fc_edit, g_ui.hw, g_ui.fc_edit_sel, 0, -5);
        break;
    case 'l':
    case NCKEY_RIGHT:
    case ' ':
        fc_step_row(&g_ui.fc_edit, g_ui.hw, g_ui.fc_edit_sel, 0, +5);
        break;
    case '<':
    case '-':
        if (fc_row_pt(g_ui.fc_edit_sel) >= 0)
            fc_step_row(&g_ui.fc_edit, g_ui.hw, g_ui.fc_edit_sel, -1, 0);
        break;
    case '>':
    case '+':
    case '=':
        if (fc_row_pt(g_ui.fc_edit_sel) >= 0)
            fc_step_row(&g_ui.fc_edit, g_ui.hw, g_ui.fc_edit_sel, +1, 0);
        break;
    case 'x':
    case 'X':
    case NCKEY_DEL:
    case NCKEY_BACKSPACE:
    case 0x7f:
        fc_toggle_skip(&g_ui.fc_edit, g_ui.hw, g_ui.fc_edit_sel);
        break;
    case 'e':
    case 'E':
        fc_start_exact_edit(&g_ui.fc_edit, g_ui.hw, g_ui.fc_edit_sel);
        break;
    case 'f':
    case 'F':
        fc_capture_live(&g_ui.fc_edit, g_ui.hw, g_ui.fc_edit_sel);
        break;
    case 'm':
    case 'M':
        fc_cycle_ref(&g_ui.fc_edit, g_ui.fc_edit_sel);
        break;
    case 'o':
    case 'O':
        if (g_ui.fc_n > 0 && g_ui.fc_sel < g_ui.fc_n)
            fc_open_in_graph(&g_ui.fc_edit, g_ui.fcurves[g_ui.fc_sel]);
        break;
    case 'r':
    case 'R':
        sync_fc_edit_from_sel();
        ut_log("fan curve reverted to saved file");
        break;
    case 'w':
    case 'W':
        save_edited_fcurve(false);
        break;
    case NCKEY_ENTER:
    case '\r':
    case '\n':
        save_edited_fcurve(true);
        break;
    default:
        break;
    }
}

static void handle_editor_key(uint32_t key)
{
    if (g_ui.prof_in_fcurves) {
        handle_fcurve_editor_key(key);
        return;
    }

    if (g_ui.prof_in_cores) {
        editor_corefreq_key(key);
        return;
    }

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
    case 'a':
    case 'A':
        if (g_ui.prof_edit_sel == SF_FAN_CPU ||
            g_ui.prof_edit_sel == SF_FAN_GPU)
            apply_snap_fan_both(&g_ui.prof_edit,
                                g_ui.prof_edit_sel == SF_FAN_GPU);
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
    case 'c':
    case 'C':
        if (g_ui.prof_edit_sel == SF_FREQ)
            prof_cores_open();
        else
            ut_log("per-core ceilings: move to the CPU ceiling row, then c");
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

    if (key == 'f' || key == 'F') {
        g_ui.prof_in_fcurves = !g_ui.prof_in_fcurves;
        if (g_ui.prof_in_fcurves)
            sync_fc_edit_from_sel();
        else
            sync_edit_from_sel();
        return;
    }

    if (g_ui.prof_in_fcurves) {
        switch (key) {
        case 'j':
        case NCKEY_DOWN:
            if (g_ui.fc_sel + 1 < g_ui.fc_n)
                select_fcurve(g_ui.fc_sel + 1);
            break;
        case 'k':
        case NCKEY_UP:
            if (g_ui.fc_sel > 0)
                select_fcurve(g_ui.fc_sel - 1);
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
            if (g_ui.fc_n > 0 && g_ui.fc_sel < g_ui.fc_n) {
                sync_fc_edit_from_sel();
                g_ui.prof_in_edit = true;
            }
            break;
        case 'o':
        case 'O':
            if (g_ui.fc_n > 0 && g_ui.fc_sel < g_ui.fc_n &&
                s_fc_ok[g_ui.fc_sel])
                fc_open_in_graph(&s_fc_meta[g_ui.fc_sel],
                                 g_ui.fcurves[g_ui.fc_sel]);
            break;
        case 's':
        case 'S':
            if (!g_ui.hw->fan_staged)
                hw_refresh_live(g_ui.hw);
            tin_set(&g_ui.prof_name, "my-fan");
            g_ui.prof_input = PL_INPUT_SAVE;
            break;
        case 'n':
        case 'N': {
            if (!g_ui.hw->fan_staged)
                hw_refresh_live(g_ui.hw);
            char name[36];
            fcurve_gen_name(name, sizeof(name));
            tin_set(&g_ui.prof_name, name);
            g_ui.prof_input = PL_INPUT_SAVE;
            break;
        }
        case 'u':
        case 'U':
            if (g_ui.fc_n > 0 && g_ui.fc_sel < g_ui.fc_n) {
                if (g_ui.prof_upd_arm == g_ui.fc_sel) {
                    g_ui.prof_upd_arm = -1;
                    char nm[FCURVE_NAME_MAX];
                    snprintf(nm, sizeof(nm), "%s", g_ui.fcurves[g_ui.fc_sel]);
                    hw_refresh_live(g_ui.hw);
                    if (fcurve_export(nm, g_ui.hw) == 0) {
                        panel_profiles_reload();
                        snprintf(g_ui.pw_msg, sizeof(g_ui.pw_msg),
                                 "✓ fan curve '%s' overwritten from live", nm);
                        g_ui.pw_msg_fail = false;
                        g_ui.pw_msg_ms = ut_mono_ms();
                    }
                } else {
                    g_ui.prof_upd_arm = g_ui.fc_sel;
                    ut_log("press u again to overwrite fan curve '%s' from live",
                           g_ui.fcurves[g_ui.fc_sel]);
                }
            }
            break;
        case 'r':
        case 'R':
            if (g_ui.fc_n > 0 && g_ui.fc_sel < g_ui.fc_n) {
                tin_set(&g_ui.prof_name, g_ui.fcurves[g_ui.fc_sel]);
                g_ui.prof_input = PL_INPUT_RENAME;
            }
            break;
        case 'c':
        case 'C':
            if (g_ui.fc_n > 0 && g_ui.fc_sel < g_ui.fc_n) {
                tin_set(&g_ui.prof_note,
                        s_fc_ok[g_ui.fc_sel] ? s_fc_meta[g_ui.fc_sel].note : "");
                g_ui.prof_input = PL_INPUT_NOTE;
            }
            break;
        case 'd':
        case 'D':
            if (g_ui.fc_n > 0 && g_ui.fc_sel < g_ui.fc_n) {
                if (g_ui.prof_del_arm == g_ui.fc_sel) {
                    g_ui.prof_del_arm = -1;
                    fcurve_delete(g_ui.fcurves[g_ui.fc_sel]);
                    panel_profiles_reload();
                } else {
                    g_ui.prof_del_arm = g_ui.fc_sel;
                    ut_log("press d again to delete fan curve '%s' (any other key cancels)",
                           g_ui.fcurves[g_ui.fc_sel]);
                }
            }
            break;
        default:
            break;
        }
        return;
    }

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
    if (g_ui.prof_in_cores)
        panel_profiles_cores_close(true);

    if (id == ACT_PL_HDR_SNAP) {
        g_ui.prof_in_edit = false;
        g_ui.prof_in_fcurves = false;
        sync_edit_from_sel();
        return;
    }
    if (id == ACT_PL_HDR_FC) {
        g_ui.prof_in_edit = false;
        g_ui.prof_in_fcurves = true;
        sync_fc_edit_from_sel();
        return;
    }

    if (id >= ACT_PL_ITEM && id < ACT_PL_ITEM + MAX_PROFILES) {
        int i = id - ACT_PL_ITEM;
        if (i < g_ui.prof_n) {
            bool was_snap = !g_ui.prof_in_fcurves && !g_ui.prof_in_edit;
            g_ui.prof_in_fcurves = false;
            g_ui.prof_in_edit = false;
            if (was_snap && i == g_ui.prof_sel)
                apply_selected();
            else
                select_snapshot(i);
        }
        return;
    }

    if (id >= ACT_PL_FC_ITEM && id < ACT_PL_FC_ITEM + MAX_FCURVES) {
        int i = id - ACT_PL_FC_ITEM;
        if (i < g_ui.fc_n) {
            bool was_fc = g_ui.prof_in_fcurves && !g_ui.prof_in_edit;
            g_ui.prof_in_fcurves = true;
            g_ui.prof_in_edit = false;
            if (was_fc && i == g_ui.fc_sel)
                apply_selected();
            else
                select_fcurve(i);
        }
        return;
    }

    if (id >= ACT_PL_FIELD && id < ACT_PL_FIELD + SF_COUNT) {
        if (g_ui.prof_n <= 0 || g_ui.prof_sel >= g_ui.prof_n)
            return;
        g_ui.prof_in_fcurves = false;
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

    if (id >= ACT_PL_FC_FIELD && id < ACT_PL_FC_FIELD + FCF_COUNT) {
        if (g_ui.fc_n <= 0 || g_ui.fc_sel >= g_ui.fc_n)
            return;
        g_ui.prof_in_fcurves = true;
        int row = id - ACT_PL_FC_FIELD;
        if (!g_ui.prof_in_edit) {
            sync_fc_edit_from_sel();
            g_ui.prof_in_edit = true;
            g_ui.fc_edit_sel = row;
        } else if (g_ui.fc_edit_sel == row) {
            fc_step_row(&g_ui.fc_edit, g_ui.hw, row, 0, +5);
        } else {
            g_ui.fc_edit_sel = row;
        }
        return;
    }

    switch (id) {
    case ACT_PL_BTN_EDIT:
        if (g_ui.prof_in_fcurves) {
            if (g_ui.fc_n > 0 && g_ui.fc_sel < g_ui.fc_n) {
                sync_fc_edit_from_sel();
                g_ui.prof_in_edit = true;
            }
        } else if (g_ui.prof_n > 0 && g_ui.prof_sel < g_ui.prof_n) {
            sync_edit_from_sel();
            g_ui.prof_in_edit = true;
        }
        break;
    case ACT_PL_BTN_SAVE:
        if (g_ui.prof_in_edit) {
            if (g_ui.prof_in_fcurves)
                save_edited_fcurve(false);
            else
                save_edited_snapshot(false);
        }
        break;
    case ACT_PL_BTN_SKIP:
        if (g_ui.prof_in_edit) {
            if (g_ui.prof_in_fcurves)
                fc_toggle_skip(&g_ui.fc_edit, g_ui.hw, g_ui.fc_edit_sel);
            else
                toggle_skip_field(&g_ui.prof_edit, g_ui.hw, g_ui.prof_edit_sel);
        }
        break;
    case ACT_PL_BTN_CURV:
        if (g_ui.prof_in_edit) {
            if (g_ui.prof_in_fcurves)
                fc_capture_live(&g_ui.fc_edit, g_ui.hw, g_ui.fc_edit_sel);
            else
                capture_live_curve(&g_ui.prof_edit, g_ui.hw, g_ui.prof_edit_sel);
        }
        break;
    case ACT_PL_BTN_OPEN:
        if (g_ui.prof_in_fcurves && g_ui.fc_n > 0 && g_ui.fc_sel < g_ui.fc_n) {
            const fcurve_meta_t *fm = g_ui.prof_in_edit
                ? &g_ui.fc_edit : &s_fc_meta[g_ui.fc_sel];
            fc_open_in_graph(fm, g_ui.fcurves[g_ui.fc_sel]);
        }
        break;
    case ACT_PL_BTN_BACK:
        if (g_ui.prof_in_edit) {
            if (g_ui.prof_in_fcurves && g_ui.fc_edit_dirty)
                sync_fc_edit_from_sel();
            else if (!g_ui.prof_in_fcurves && g_ui.prof_edit_dirty)
                sync_edit_from_sel();
            g_ui.prof_in_edit = false;
        }
        break;
    default:
        break;
    }
}
