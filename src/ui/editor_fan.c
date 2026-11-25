#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ui_internal.h"
#include "../control.h"
#include "../util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Big fan curve editor in the workspace (View 2: FANS).
 *   - 8-point curves with strictly increasing °C and monotonic PWM (0..255 / %)
 *   - Points nudged with keyboard (h/l temp, j/k ±5 pwm, J/K ±13 pwm),
 *     typed exactly (e temp, p pwm or %, Enter), or clicked on the graph
 *   - 'w' writes & verifies against live hwmon, 'r' reverts to live EC,
 *     's' saves as a reusable named fan curve (~/.config/ctron/fan_curves/),
 *     'm' opens Presets (OEM factory refs + live EC + saved fan curves). */

enum {
    ACT_FE_CPU = ACT_FE_BASE, /* 100 — forwarded from panel_workspace_act */
    ACT_FE_GPU,
    ACT_FE_ADD,
    ACT_FE_DEL,
    ACT_FE_WRITE,
    ACT_FE_TOGGLE,
    ACT_FE_T_FIELD,
    ACT_FE_P_FIELD,
    ACT_FE_SET,
    ACT_FE_PRESETS_BTN,                    /* opens FAN PRESETS modal */
    ACT_FE_REVERT,
    ACT_FE_SAVE_BTN,
    ACT_FE_PRESET_BASE = ACT_FE_BASE + 15, /* legacy stock/silent/cool/full */
    ACT_FE_NODE_BASE   = ACT_FE_BASE + 30, /* + point index (0..7) */
};

/* Actions for TGT_PANEL_FAN_PRESETS modal */
enum {
    ACT_FPR_TARGET_CPU = 1,
    ACT_FPR_TARGET_GPU = 2,
    ACT_FPR_APPLY      = 3,
    ACT_FPR_CANCEL     = 4,
    ACT_FPR_ITEM_BASE  = 10, /* + item index (0..3 built-ins, 4.. saved) */
};

static rect_t s_graph;
static bool s_typed_fresh; /* first digit after opening e/p replaces seed */

const rect_t *editor_fan_graph(void)
{
    return &s_graph;
}

static fan_curve_t *cur_curve(void)
{
    return g_ui.fe_gpu ? &g_ui.hw->fan_gpu : &g_ui.hw->fan_cpu;
}

/* Any curve mutation stages the table AND enables custom mode on the
 * edited fan so pressing 'w' (Write) activates the curve rather than
 * leaving pwmN_enable on factory auto. */
static void fan_touch_side(bool gpu)
{
    g_ui.hw->fan_staged = true;
    if (gpu)
        g_ui.hw->fan_gpu_on = true;
    else
        g_ui.hw->fan_cpu_on = true;
    fan_curve_t *fc = cur_curve();
    if (fc->n > 0 && g_ui.fe_sel >= 0 && g_ui.fe_sel < fc->n) {
        int st = fc->temp_c[g_ui.fe_sel], sp = fc->pwm[g_ui.fe_sel];
        snprintf(g_ui.pw_msg, sizeof(g_ui.pw_msg),
                 "staged %s pt %d → %d°C, %d%% (%d/255) · w write · r revert · s save",
                 gpu ? "GPU" : "CPU", g_ui.fe_sel + 1, st,
                 fan_pwm_to_pct(sp), sp);
        g_ui.pw_msg_fail = false;
        g_ui.pw_msg_ms = ut_mono_ms();
    }
}

static int px(const fan_curve_t *fc, int i)
{
    int t = ut_clamp_i(fc->temp_c[i], FAN_TMIN, FAN_TMAX);
    if (s_graph.w <= 1)
        return s_graph.x;
    return s_graph.x + (t - FAN_TMIN) * (s_graph.w - 1) / (FAN_TMAX - FAN_TMIN);
}

static int py(const fan_curve_t *fc, int i)
{
    int p = ut_clamp_i(fc->pwm[i], 0, 255);
    if (s_graph.h <= 1)
        return s_graph.y;
    return s_graph.y + s_graph.h - 1 - p * (s_graph.h - 1) / 255;
}

static void cell_to_xy(int mx, int my, int *temp, int *pwm)
{
    int x = ut_clamp_i(mx, 0, s_graph.w - 1);
    int y = ut_clamp_i(my, 0, s_graph.h - 1);
    if (s_graph.w > 1)
        *temp = FAN_TMIN + x * (FAN_TMAX - FAN_TMIN) / (s_graph.w - 1);
    else
        *temp = FAN_TMIN;
    if (s_graph.h > 1)
        *pwm = 255 - y * 255 / (s_graph.h - 1);
    else
        *pwm = 0;
    *temp = ut_clamp_i(*temp, FAN_TMIN, FAN_TMAX);
    *pwm = ut_clamp_i(*pwm, 0, 255);
}

static void fill_xy_bufs(void)
{
    fan_curve_t *fc = cur_curve();
    if (fc->n > 0) {
        if (g_ui.fe_sel >= fc->n)
            g_ui.fe_sel = fc->n - 1;
        if (g_ui.fe_sel < 0)
            g_ui.fe_sel = 0;
        char b[16];
        snprintf(b, sizeof(b), "%d", fc->temp_c[g_ui.fe_sel]);
        tin_set(&g_ui.fe_x, b);
        snprintf(b, sizeof(b), "%d", fc->pwm[g_ui.fe_sel]);
        tin_set(&g_ui.fe_y, b);
    }
}

void editor_fan_sync_bufs(void)
{
    fill_xy_bufs();
}

static int parse_pwm_str(const char *s)
{
    if (!s || !*s)
        return 0;
    const char *pct = strchr(s, '%');
    if (pct)
        return fan_pct_to_pwm(atoi(s));
    return ut_clamp_i(atoi(s), 0, 255);
}

static void do_write_ec(void)
{
    ui_flash("applying fan curve...");
    int rc = ctrl_fan_write(g_ui.hw);
    fill_xy_bufs();
    if (rc == 0) {
        snprintf(g_ui.pw_msg, sizeof(g_ui.pw_msg),
                 "✓ fan curves written (cpu %s, gpu %s)",
                 g_ui.hw->fan_cpu_on ? "on" : "auto",
                 g_ui.hw->fan_gpu_on ? "on" : "auto");
        g_ui.pw_msg_fail = false;
    } else {
        snprintf(g_ui.pw_msg, sizeof(g_ui.pw_msg),
                 "⚠ fan curve write FAILED (press L for log)");
        g_ui.pw_msg_fail = true;
    }
    g_ui.pw_msg_ms = ut_mono_ms();
}

static void do_revert_ec(void)
{
    g_ui.hw->fan_staged = false;
    g_ui.fe_loaded[0] = '\0';
    hw_refresh_live(g_ui.hw);
    fill_xy_bufs();
    snprintf(g_ui.pw_msg, sizeof(g_ui.pw_msg),
             "✓ fan curves reverted to live EC");
    g_ui.pw_msg_fail = false;
    g_ui.pw_msg_ms = ut_mono_ms();
    ut_log("fan curves reverted to live EC");
}

static void do_open_save_prompt(void)
{
    tin_set(&g_ui.fe_save_name,
            g_ui.fe_loaded[0] ? g_ui.fe_loaded : "my-fan");
    g_ui.fe_input = 3;
}

static void do_commit_save(void)
{
    if (!g_ui.fe_save_name.buf[0]) {
        g_ui.fe_input = 0;
        return;
    }
    char safe[FCURVE_NAME_MAX];
    profile_sanitize_name(g_ui.fe_save_name.buf, safe, sizeof(safe));
    if (fcurve_export(safe, g_ui.hw) == 0) {
        snprintf(g_ui.fe_loaded, sizeof(g_ui.fe_loaded), "%s", safe);
        panel_profiles_reload();
        for (int i = 0; i < g_ui.fc_n; i++) {
            if (!strcmp(g_ui.fcurves[i], safe)) {
                g_ui.fc_sel = i;
                break;
            }
        }
        snprintf(g_ui.pw_msg, sizeof(g_ui.pw_msg),
                 "✓ fan curve '%s' saved (available in 4 Snapshots & m Presets)",
                 safe);
        g_ui.pw_msg_fail = false;
    } else {
        snprintf(g_ui.pw_msg, sizeof(g_ui.pw_msg),
                 "⚠ fan curve '%s': save FAILED", safe);
        g_ui.pw_msg_fail = true;
    }
    g_ui.pw_msg_ms = ut_mono_ms();
    g_ui.fe_input = 0;
}

static void plot_line(struct ncplane *n, int x0, int y0, int x1, int y1, uint64_t ch)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    ncplane_set_channels(n, ch);
    for (;;) {
        if (x0 >= s_graph.x && x0 < s_graph.x + s_graph.w &&
            y0 >= s_graph.y && y0 < s_graph.y + s_graph.h)
            ncplane_putstr_yx(n, y0, x0, "·");
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void editor_fan_draw(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    hw_state_t *hw = g_ui.hw;
    fan_curve_t *fc = cur_curve();

    if (fc->n > 0) {
        if (g_ui.fe_sel >= fc->n)
            g_ui.fe_sel = fc->n - 1;
        if (g_ui.fe_sel < 0)
            g_ui.fe_sel = 0;
        if (!g_ui.fe_x.buf[0] && g_ui.fe_input == 0)
            fill_xy_bufs();
    } else {
        g_ui.fe_sel = -1;
    }

    int x = r->x;
    int w = r->w;
    if (w < 16 || r->h < 8)
        return;

    /* Control row — first 6 buttons preserve exact positions for click tests */
    int cy = r->y;
    bool on = g_ui.fe_gpu ? hw->fan_gpu_on : hw->fan_cpu_on;
    const ui_btndef_t ctl[] = {
        { " CPU ",    !g_ui.fe_gpu, TGT(TGT_PANEL_WORKSPACE, ACT_FE_CPU) },
        { " GPU ",     g_ui.fe_gpu, TGT(TGT_PANEL_WORKSPACE, ACT_FE_GPU) },
        { " + ",       false,       TGT(TGT_PANEL_WORKSPACE, ACT_FE_ADD) },
        { " - ",       false,       TGT(TGT_PANEL_WORKSPACE, ACT_FE_DEL) },
        { " Write ",   hw->fan_staged, TGT(TGT_PANEL_WORKSPACE, ACT_FE_WRITE) },
        { on ? " Custom ON " : " Custom OFF ", on, TGT(TGT_PANEL_WORKSPACE, ACT_FE_TOGGLE) },
        { " Revert ",  false,       TGT(TGT_PANEL_WORKSPACE, ACT_FE_REVERT) },
        { " Save (s) ", g_ui.fe_input == 3, TGT(TGT_PANEL_WORKSPACE, ACT_FE_SAVE_BTN) },
    };
    int next_x = ui_btn_row(n, cy, x, w, ctl, 8);
    {
        char live[88];
        int ltemp = g_ui.fe_gpu ? hw->gpu_temp : hw->cpu_temp;
        int lrpm  = g_ui.fe_gpu ? hw->rpm_gpu : hw->rpm_cpu;
        char ts[16], rs[16], tgt[20] = "", tag[36] = "";
        if (ltemp < 0) snprintf(ts, sizeof(ts), "--");
        else           snprintf(ts, sizeof(ts), "%d°C", ltemp);
        if (lrpm < 0)  snprintf(rs, sizeof(rs), "--");
        else           snprintf(rs, sizeof(rs), "%d rpm", lrpm);
        if (on && ltemp > 0 && fc->n > 0) {
            int ep = fan_eval_pwm(fc, ltemp);
            if (ep >= 0)
                snprintf(tgt, sizeof(tgt), " · %d%%", fan_pwm_to_pct(ep));
        }
        if (g_ui.fe_loaded[0])
            snprintf(tag, sizeof(tag), "[%.20s] · ", g_ui.fe_loaded);
        snprintf(live, sizeof(live), "%slive %s · %s%s", tag, ts, rs, tgt);
        int lw = ut_cells(live);
        if (x + w - lw > next_x + 2)
            ui_putln(n, x + w - lw, cy, lw + 1, live, pal->muted, false);
    }

    /* Graph area */
    s_graph.x = r->x + 5;
    s_graph.y = r->y + 2;
    s_graph.w = r->w - 7;
    s_graph.h = r->h - 7;
    if (s_graph.w < 10)
        s_graph.w = 10;
    if (s_graph.h < 4)
        s_graph.h = 4;

    uint64_t ch_ax = ui_ch(pal->border, false);
    uint64_t ch_ln = ui_ch(on ? pal->accent2 : pal->muted, false);
    uint64_t ch_pt = ui_ch(on ? pal->accent : pal->muted, false);
    uint64_t ch_lb = ui_ch(pal->muted, false);

    /* Axes */
    ncplane_set_channels(n, ch_ax);
    for (int yy = 0; yy < s_graph.h; yy++)
        ncplane_putstr_yx(n, s_graph.y + yy, s_graph.x - 1, "│");
    ncplane_putstr_yx(n, s_graph.y + s_graph.h, s_graph.x - 1, "└");
    for (int xx = 0; xx < s_graph.w; xx++)
        ncplane_putstr(n, "─");

    ncplane_set_channels(n, ch_lb);
    ncplane_putstr_yx(n, s_graph.y, s_graph.x - 5, "100%");
    if (s_graph.h >= 7 && !g_ui.fe_grid)
        ncplane_putstr_yx(n, s_graph.y + s_graph.h / 2, s_graph.x - 5, " 50%");
    ncplane_putstr_yx(n, s_graph.y + s_graph.h - 1, s_graph.x - 5, "  0%");
    ncplane_putstr_yx(n, s_graph.y + s_graph.h, s_graph.x, "20°C");
    {
        char lb[16];
        snprintf(lb, sizeof(lb), "%d°", FAN_TMAX);
        ncplane_putstr_yx(n, s_graph.y + s_graph.h, s_graph.x + s_graph.w - 4, lb);
    }

    /* Value guides ('i') */
    if (g_ui.fe_grid) {
        static const int G_PWM[] = { 64, 128, 192 };
        ncplane_set_channels(n, ch_ax);
        for (int t = 30; t <= 100; t += 10) {
            int gx = s_graph.x + (t - FAN_TMIN) * (s_graph.w - 1) /
                     (FAN_TMAX - FAN_TMIN);
            for (int yy = 0; yy < s_graph.h; yy++)
                ncplane_putstr_yx(n, s_graph.y + yy, gx, "·");
        }
        for (size_t i = 0; i < sizeof(G_PWM) / sizeof(G_PWM[0]); i++) {
            int gy = s_graph.y + s_graph.h - 1 -
                     G_PWM[i] * (s_graph.h - 1) / 255;
            for (int xx = 0; xx < s_graph.w; xx++)
                ncplane_putstr_yx(n, gy, s_graph.x + xx, "·");
        }
        ncplane_set_channels(n, ch_lb);
        char lb[8];
        for (int t = 40; t <= 100; t += 20) {
            int gx = s_graph.x + (t - FAN_TMIN) * (s_graph.w - 1) /
                     (FAN_TMAX - FAN_TMIN);
            snprintf(lb, sizeof(lb), "%d", t);
            ncplane_putstr_yx(n, s_graph.y + s_graph.h, gx, lb);
        }
        for (size_t i = 0; i < sizeof(G_PWM) / sizeof(G_PWM[0]); i++) {
            int gy = s_graph.y + s_graph.h - 1 -
                     G_PWM[i] * (s_graph.h - 1) / 255;
            snprintf(lb, sizeof(lb), "%d", G_PWM[i]);
            ncplane_putstr_yx(n, gy, s_graph.x - 4, lb);
        }
    }

    /* Live temp marker on X-axis */
    {
        int live_t = g_ui.fe_gpu ? hw->gpu_temp : hw->cpu_temp;
        if (live_t > 0) {
            int lx = s_graph.x +
                     (ut_clamp_i(live_t, FAN_TMIN, FAN_TMAX) - FAN_TMIN) *
                     (s_graph.w - 1) / (FAN_TMAX - FAN_TMIN);
            ncplane_set_channels(n, ui_ch(pal->warn, false));
            ncplane_putstr_yx(n, s_graph.y + s_graph.h, lx, "▲");
        }
    }

    /* Polyline + points */
    for (int i = 0; i + 1 < fc->n; i++)
        plot_line(n, px(fc, i), py(fc, i), px(fc, i + 1), py(fc, i + 1), ch_ln);
    ncplane_set_channels(n, ch_pt);
    ncplane_set_styles(n, NCSTYLE_BOLD);
    for (int i = 0; i < fc->n; i++)
        ncplane_putstr_yx(n, py(fc, i), px(fc, i),
                          i == g_ui.fe_sel ? "◆" : "●");
    ncplane_set_styles(n, NCSTYLE_NONE);

    /* Point chips under the graph (keeps "1:40°,80" prefix for smoke regex) */
    int chy = r->y + r->h - 3;
    int stride = w >= 84 ? 20 : 14;
    for (int i = 0; i < fc->n; i++) {
        char chip[28];
        if (w >= 84)
            snprintf(chip, sizeof(chip), " %d:%d°,%d(%d%%) ",
                     i + 1, fc->temp_c[i], fc->pwm[i], fan_pwm_to_pct(fc->pwm[i]));
        else
            snprintf(chip, sizeof(chip), " %d:%d°,%d ",
                     i + 1, fc->temp_c[i], fc->pwm[i]);
        int cx = x + (i % 4) * stride;
        int cyy = chy + i / 4;
        if (cyy < r->y + r->h - 1 && cx + ut_cells(chip) <= x + w) {
            ui_btn(n, cx, cyy, chip, i == g_ui.fe_sel, false,
                   TGT(TGT_PANEL_WORKSPACE, ACT_FE_NODE_BASE + i));
        }
    }

    /* Bottom row: inline save prompt OR X/Y input + selected point summary */
    int iy = r->y + r->h - 1;
    if (g_ui.fe_input == 3) {
        char disp[72], line[160];
        tin_format(&g_ui.fe_save_name, disp, sizeof(disp), true);
        snprintf(line, sizeof(line),
                 "save fan curve (CPU+GPU): %s   (Enter save · Esc cancel)",
                 disp);
        ui_putln(n, x, iy, w, line, pal->accent, true);
        return;
    }

    char xd[12], yd[12], tb[28], pb[28];
    tin_format(&g_ui.fe_x, xd, sizeof(xd), g_ui.fe_input == 1);
    tin_format(&g_ui.fe_y, yd, sizeof(yd), g_ui.fe_input == 2);
    snprintf(tb, sizeof(tb), " T[%s°] ", xd);
    snprintf(pb, sizeof(pb), " P[%s] ", yd);
    const ui_btndef_t xy[] = {
        { tb,    g_ui.fe_input == 1, TGT(TGT_PANEL_WORKSPACE, ACT_FE_T_FIELD) },
        { pb,    g_ui.fe_input == 2, TGT(TGT_PANEL_WORKSPACE, ACT_FE_P_FIELD) },
        { " Set ", false,            TGT(TGT_PANEL_WORKSPACE, ACT_FE_SET) },
        { " Presets (m) ", g_ui.fe_presets_overlay, TGT(TGT_PANEL_WORKSPACE, ACT_FE_PRESETS_BTN) },
    };
    int end_x = ui_btn_row(n, iy, x, w, xy, 4);
    if (fc->n > 0 && g_ui.fe_sel >= 0 && g_ui.fe_sel < fc->n) {
        char info[64];
        int st = fc->temp_c[g_ui.fe_sel], sp = fc->pwm[g_ui.fe_sel];
        snprintf(info, sizeof(info), "Pt %d/%d: %d°C → %d%% (%d/255)%s",
                 g_ui.fe_sel + 1, fc->n, st, fan_pwm_to_pct(sp), sp,
                 on ? "" : " · factory auto");
        int iw = ut_cells(info);
        if (x + w - iw > end_x + 2)
            ui_putln(n, x + w - iw, iy, iw + 1, info,
                     on ? pal->text : pal->warn, false);
    }
}

/* ---- input -------------------------------------------------------------- */

static void apply_xy(void)
{
    if (g_ui.fe_sel < 0)
        return;
    int t = atoi(g_ui.fe_x.buf);
    int p = parse_pwm_str(g_ui.fe_y.buf);
    g_ui.fe_sel = fan_set_point(cur_curve(), g_ui.fe_sel, t, p);
    fill_xy_bufs();
    g_ui.fe_input = 0;
    s_typed_fresh = false;
    fan_touch_side(g_ui.fe_gpu != 0);
    ut_log("fan pt %d set (%d°C, %d%%)",
           g_ui.fe_sel + 1, cur_curve()->temp_c[g_ui.fe_sel],
           fan_pwm_to_pct(cur_curve()->pwm[g_ui.fe_sel]));
}

static void open_xy_field(int which)
{
    fill_xy_bufs();
    g_ui.fe_input = which;
    s_typed_fresh = true;
}

void editor_fan_key(uint32_t key)
{
    fan_curve_t *fc = cur_curve();

    if (g_ui.fe_input == 3) {
        if (key == NCKEY_ESC) {
            g_ui.fe_input = 0;
            return;
        }
        if (key == NCKEY_ENTER || key == '\r' || key == '\n') {
            do_commit_save();
            return;
        }
        tin_key(&g_ui.fe_save_name, key);
        return;
    }

    /* Typing fields swallow keys */
    if (g_ui.fe_input == 1 || g_ui.fe_input == 2) {
        tinput_t *t = g_ui.fe_input == 1 ? &g_ui.fe_x : &g_ui.fe_y;
        if (key == NCKEY_ESC) {
            g_ui.fe_input = 0;
            s_typed_fresh = false;
            fill_xy_bufs();
            return;
        }
        if (key == NCKEY_ENTER || key == '\r' || key == '\n') {
            apply_xy();
            return;
        }
        if (key == '\t') {
            g_ui.fe_input = g_ui.fe_input == 1 ? 2 : 1;
            s_typed_fresh = true;
            return;
        }
        if ((key >= '0' && key <= '9') ||
            (g_ui.fe_input == 2 && key == '%')) {
            if (s_typed_fresh) {
                tin_clear(t);
                s_typed_fresh = false;
            }
            tin_key(t, key);
            return;
        }
        if (tin_is_edit_key(key)) {
            s_typed_fresh = false;
            tin_key(t, key);
            return;
        }
        g_ui.fe_input = 0;
        s_typed_fresh = false;
        fill_xy_bufs();
        return;
    }

    switch (key) {
    case 'c':
    case 'C':
        g_ui.fe_gpu = 0;
        fill_xy_bufs();
        break;
    case 'g':
    case 'G':
        g_ui.fe_gpu = 1;
        fill_xy_bufs();
        break;
    case 'i':
    case 'I':
        g_ui.fe_grid = !g_ui.fe_grid;
        break;
    case 'm':
    case 'M':
        editor_fan_presets_open();
        break;
    case 's':
    case 'S':
        do_open_save_prompt();
        break;
    case 'r':
    case 'R':
        do_revert_ec();
        break;

    case '+':
    case 'a':
    case 'A':
        /* Auto-place new point in the widest temperature gap with
         * linearly interpolated PWM (never duplicates selected point). */
        if (fc->n < FAN_POINTS) {
            g_ui.fe_sel = fan_add_point(fc, -1, -1);
            fill_xy_bufs();
            fan_touch_side(g_ui.fe_gpu != 0);
        } else {
            ut_log("fan curve already has all %d hardware points", FAN_POINTS);
        }
        break;
    case '-':
    case 'x':
    case 'X':
        if (fc->n > 2) {
            g_ui.fe_sel = fan_del_point(fc, g_ui.fe_sel);
            fill_xy_bufs();
            fan_touch_side(g_ui.fe_gpu != 0);
        } else {
            ut_log("fan curve requires at least 2 points");
        }
        break;
    case 'w':
    case 'W':
        do_write_ec();
        break;
    case 'o':
    case 'O':
        if (g_ui.fe_gpu)
            ctrl_fan_set_enabled(g_ui.hw, g_ui.hw->fan_cpu_on, !g_ui.hw->fan_gpu_on);
        else
            ctrl_fan_set_enabled(g_ui.hw, !g_ui.hw->fan_cpu_on, g_ui.hw->fan_gpu_on);
        break;
    case 'e':
    case 'E':
        open_xy_field(1);
        break;
    case 'p':
    case 'P':
        open_xy_field(2);
        break;
    case 'h':
    case NCKEY_LEFT:
        if (g_ui.fe_sel >= 0) {
            g_ui.fe_sel = fan_nudge_point(fc, g_ui.fe_sel, -1, 0);
            fill_xy_bufs();
            fan_touch_side(g_ui.fe_gpu != 0);
        }
        break;
    case 'l':
    case NCKEY_RIGHT:
        if (g_ui.fe_sel >= 0) {
            g_ui.fe_sel = fan_nudge_point(fc, g_ui.fe_sel, 1, 0);
            fill_xy_bufs();
            fan_touch_side(g_ui.fe_gpu != 0);
        }
        break;
    case 'j':
    case NCKEY_DOWN:
        if (g_ui.fe_sel >= 0) {
            g_ui.fe_sel = fan_nudge_point(fc, g_ui.fe_sel, 0, -5);
            fill_xy_bufs();
            fan_touch_side(g_ui.fe_gpu != 0);
        }
        break;
    case 'J':
        if (g_ui.fe_sel >= 0) {
            g_ui.fe_sel = fan_nudge_point(fc, g_ui.fe_sel, 0, -13);
            fill_xy_bufs();
            fan_touch_side(g_ui.fe_gpu != 0);
        }
        break;
    case 'k':
    case NCKEY_UP:
        if (g_ui.fe_sel >= 0) {
            g_ui.fe_sel = fan_nudge_point(fc, g_ui.fe_sel, 0, 5);
            fill_xy_bufs();
            fan_touch_side(g_ui.fe_gpu != 0);
        }
        break;
    case 'K':
        if (g_ui.fe_sel >= 0) {
            g_ui.fe_sel = fan_nudge_point(fc, g_ui.fe_sel, 0, 13);
            fill_xy_bufs();
            fan_touch_side(g_ui.fe_gpu != 0);
        }
        break;
    case NCKEY_ENTER:
    case '\r':
    case '\n':
    case ' ':
        apply_xy();
        break;
    case '<':
        if (g_ui.fe_sel > 0)
            g_ui.fe_sel--;
        fill_xy_bufs();
        break;
    case '>':
        if (g_ui.fe_sel + 1 < fc->n)
            g_ui.fe_sel++;
        fill_xy_bufs();
        break;
    default:
        break;
    }
}

void editor_fan_act(int id, int mx, int my)
{
    fan_curve_t *fc = cur_curve();

    if (id >= ACT_FE_BASE && id < ACT_FE_END) {
        if (id != ACT_FE_T_FIELD && id != ACT_FE_P_FIELD &&
            id != ACT_FE_SET && id != ACT_FE_SAVE_BTN)
            g_ui.fe_input = 0;
        switch (id) {
        case ACT_FE_CPU:
            g_ui.fe_gpu = 0;
            fill_xy_bufs();
            return;
        case ACT_FE_GPU:
            g_ui.fe_gpu = 1;
            fill_xy_bufs();
            return;
        case ACT_FE_ADD:
            if (fc->n < FAN_POINTS) {
                g_ui.fe_sel = fan_add_point(fc, -1, -1);
                fill_xy_bufs();
                fan_touch_side(g_ui.fe_gpu != 0);
            }
            return;
        case ACT_FE_DEL:
            if (fc->n > 2) {
                g_ui.fe_sel = fan_del_point(fc, g_ui.fe_sel);
                fill_xy_bufs();
                fan_touch_side(g_ui.fe_gpu != 0);
            }
            return;
        case ACT_FE_WRITE:
            do_write_ec();
            return;
        case ACT_FE_REVERT:
            do_revert_ec();
            return;
        case ACT_FE_SAVE_BTN:
            do_open_save_prompt();
            return;
        case ACT_FE_TOGGLE:
            if (g_ui.fe_gpu)
                ctrl_fan_set_enabled(g_ui.hw, g_ui.hw->fan_cpu_on, !g_ui.hw->fan_gpu_on);
            else
                ctrl_fan_set_enabled(g_ui.hw, !g_ui.hw->fan_cpu_on, g_ui.hw->fan_gpu_on);
            return;
        case ACT_FE_T_FIELD:
            open_xy_field(1);
            return;
        case ACT_FE_P_FIELD:
            open_xy_field(2);
            return;
        case ACT_FE_SET:
            apply_xy();
            return;
        case ACT_FE_PRESETS_BTN:
            editor_fan_presets_open();
            return;
        default:
            if (id >= ACT_FE_PRESET_BASE && id < ACT_FE_PRESET_BASE + 4) {
                ui_flash("applying fan preset...");
                ctrl_fan_preset(g_ui.hw, id - ACT_FE_PRESET_BASE);
                fill_xy_bufs();
                return;
            }
            if (id >= ACT_FE_NODE_BASE && id < ACT_FE_NODE_BASE + FAN_POINTS) {
                int i = id - ACT_FE_NODE_BASE;
                if (i < fc->n) {
                    g_ui.fe_sel = i;
                    fill_xy_bufs();
                }
                return;
            }
            return;
        }
    }

    /* Graph click: nearest point within 2 cells? */
    int near = -1, nd = 99;
    for (int i = 0; i < fc->n; i++) {
        int dx = px(fc, i) - s_graph.x - mx;
        int dy = py(fc, i) - s_graph.y - my;
        int d = dx * dx + dy * dy;
        if (d < nd) {
            nd = d;
            near = i;
        }
    }

    int t, p;
    cell_to_xy(mx, my, &t, &p);
    if (near >= 0 && nd <= 4) {
        if (g_ui.fe_sel == near) {
            g_ui.fe_sel = fan_set_point(fc, near, t, p);
            fan_touch_side(g_ui.fe_gpu != 0);
        } else {
            g_ui.fe_sel = near;
        }
    } else if (fc->n < FAN_POINTS) {
        g_ui.fe_sel = fan_add_point(fc, t, p);
        fan_touch_side(g_ui.fe_gpu != 0);
    } else {
        g_ui.fe_sel = fan_set_point(fc, g_ui.fe_sel, t, p);
        fan_touch_side(g_ui.fe_gpu != 0);
    }
    fill_xy_bufs();
}

/* ---- fan presets & saved fan curves modal ('m') ------------------------- */

static const char *const s_preset_labels[4] = {
    "1: Quiet (Factory Ref)",
    "2: Balanced (Factory Ref)",
    "3: Performance (Factory Ref)",
    "4: Active (Live EC)",
};

static int s_pr_cached_sel = -1;
static fan_curve_t s_pr_cpu, s_pr_gpu;
static fan_preset_info_t s_pr_info;

static int pr_total_count(void)
{
    return 4 + (g_ui.fc_n > 0 ? g_ui.fc_n : 0);
}

void editor_fan_presets_open(void)
{
    panel_profiles_reload();
    g_ui.fe_presets_overlay = true;
    s_pr_cached_sel = -1;
    int tot = pr_total_count();
    if (g_ui.fe_pr_sel < 0 || g_ui.fe_pr_sel >= tot)
        g_ui.fe_pr_sel = 0;
    g_ui.fe_pr_top = 0;
    if (!g_ui.fe_pr_target_cpu && !g_ui.fe_pr_target_gpu) {
        g_ui.fe_pr_target_cpu = true;
        g_ui.fe_pr_target_gpu = true;
    }
}

static void apply_selected_preset(void)
{
    if (!g_ui.fe_pr_target_cpu && !g_ui.fe_pr_target_gpu)
        return;

    int sel = g_ui.fe_pr_sel;
    if (sel >= 4 && sel - 4 < g_ui.fc_n) {
        int idx = sel - 4;
        const fcurve_meta_t *fm = panel_profiles_fcurve_meta(idx);
        if (!fm)
            return;
        if (g_ui.fe_pr_target_cpu) {
            if (fm->has_cpu)
                g_ui.hw->fan_cpu = fm->cpu;
            g_ui.hw->fan_cpu_on = (fm->cpu_on != 0);
        }
        if (g_ui.fe_pr_target_gpu) {
            if (fm->has_gpu)
                g_ui.hw->fan_gpu = fm->gpu;
            g_ui.hw->fan_gpu_on = (fm->gpu_on != 0);
        }
        snprintf(g_ui.fe_loaded, sizeof(g_ui.fe_loaded), "%s", g_ui.fcurves[idx]);
        g_ui.hw->fan_staged = true;
        fill_xy_bufs();
        g_ui.fe_presets_overlay = false;
        g_ui.pw_msg_ms = 0;
        ut_log("saved fan curve '%s' loaded into editor", g_ui.fcurves[idx]);
        return;
    }

    fan_curve_t c_cpu = {0}, c_gpu = {0};
    fan_preset_get(g_ui.hw, (fan_preset_t)sel,
                   g_ui.fe_pr_target_cpu ? &c_cpu : NULL,
                   g_ui.fe_pr_target_gpu ? &c_gpu : NULL, NULL);

    if (g_ui.fe_pr_target_cpu && c_cpu.n > 0) {
        g_ui.hw->fan_cpu = c_cpu;
        g_ui.hw->fan_cpu_on = true;
    }
    if (g_ui.fe_pr_target_gpu && c_gpu.n > 0) {
        g_ui.hw->fan_gpu = c_gpu;
        g_ui.hw->fan_gpu_on = true;
    }
    g_ui.fe_loaded[0] = '\0';
    g_ui.hw->fan_staged = true;
    fill_xy_bufs();
    g_ui.fe_presets_overlay = false;
    g_ui.pw_msg_ms = 0;
    ut_log("fan preset '%s' loaded into editor",
           fan_preset_name((fan_preset_t)sel));
}

static void fmt_curve_summary(const fan_curve_t *fc, char *out, size_t n)
{
    if (!fc || fc->n <= 0) {
        snprintf(out, n, "--");
        return;
    }
    size_t pos = 0;
    out[0] = '\0';
    for (int i = 0; i < fc->n && i < FAN_POINTS; i++) {
        int w = snprintf(out + pos, n - pos, "%s%d°:%d%%",
                         i ? " " : "", fc->temp_c[i], fan_pwm_to_pct(fc->pwm[i]));
        if (w <= 0 || pos + (size_t)w >= n)
            break;
        pos += (size_t)w;
    }
}

static const char *preset_src_str(fan_preset_src_t s)
{
    switch (s) {
    case FAN_PSRC_RON:     return "asusd ron";
    case FAN_PSRC_BUILTIN: return "OEM factory reference";
    case FAN_PSRC_HWMON:   return "live EC curve";
    case FAN_PSRC_STOCK:   return "init snapshot (hwmon unreadable)";
    default:               return "generic default (no data)";
    }
}

void editor_fan_presets_draw(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    ui_window(n, r, "FAN PRESETS & SAVED CURVES");

    int x = r->x + 2;
    int w = r->w - 4;
    int last = r->y + r->h - 2;
    if (w < 16 || r->h < 10)
        return;

    ui_putln(n, x, r->y + 1, w,
             "j/k or 1..4 select · Enter load · c CPU · g GPU · Esc close",
             pal->muted, false);

    ui_putln(n, x, r->y + 2, 11, "Apply to:  ", pal->text, true);
    ui_btn(n, x + 11, r->y + 2,
           g_ui.fe_pr_target_cpu ? "[✓ CPU]" : "[  CPU]",
           g_ui.fe_pr_target_cpu, false,
           TGT(TGT_PANEL_FAN_PRESETS, ACT_FPR_TARGET_CPU));
    ui_btn(n, x + 21, r->y + 2,
           g_ui.fe_pr_target_gpu ? "[✓ GPU]" : "[  GPU]",
           g_ui.fe_pr_target_gpu, false,
           TGT(TGT_PANEL_FAN_PRESETS, ACT_FPR_TARGET_GPU));

    int tot = pr_total_count();
    if (g_ui.fe_pr_sel >= tot)
        g_ui.fe_pr_sel = tot - 1;
    if (g_ui.fe_pr_sel < 0)
        g_ui.fe_pr_sel = 0;

    int list_y0 = r->y + 4;
    int prev_y  = last - 4;
    int list_rows = prev_y - list_y0 - 1;
    if (list_rows < 2)
        list_rows = 2;

    if (tot <= list_rows) {
        g_ui.fe_pr_top = 0;
    } else {
        if (g_ui.fe_pr_top > g_ui.fe_pr_sel)
            g_ui.fe_pr_top = g_ui.fe_pr_sel;
        if (g_ui.fe_pr_sel >= g_ui.fe_pr_top + list_rows)
            g_ui.fe_pr_top = g_ui.fe_pr_sel - list_rows + 1;
        if (g_ui.fe_pr_top + list_rows > tot)
            g_ui.fe_pr_top = tot - list_rows;
        if (g_ui.fe_pr_top < 0)
            g_ui.fe_pr_top = 0;
    }

    for (int i = g_ui.fe_pr_top, shown = 0;
         i < tot && shown < list_rows; i++, shown++) {
        int pyy = list_y0 + shown;
        if (pyy > last)
            break;
        bool sel = (i == g_ui.fe_pr_sel);
        char line[96];
        if (i < 4)
            snprintf(line, sizeof(line), "%s %s",
                     sel ? "▸" : " ", s_preset_labels[i]);
        else
            snprintf(line, sizeof(line), "%s Saved: %s",
                     sel ? "▸" : " ", g_ui.fcurves[i - 4]);
        ui_putln(n, x, pyy, w, line, sel ? pal->accent : pal->text, sel);
        tgt_register(x, pyy, w, 1, TGT(TGT_PANEL_FAN_PRESETS, ACT_FPR_ITEM_BASE + i));
    }

    const fan_curve_t *prev_cpu = NULL, *prev_gpu = NULL;
    char src[112] = "";
    if (g_ui.fe_pr_sel >= 4 && g_ui.fe_pr_sel - 4 < g_ui.fc_n) {
        const fcurve_meta_t *fm = panel_profiles_fcurve_meta(g_ui.fe_pr_sel - 4);
        if (fm) {
            prev_cpu = fm->has_cpu ? &fm->cpu : NULL;
            prev_gpu = fm->has_gpu ? &fm->gpu : NULL;
            snprintf(src, sizeof(src), "Saved curve '%s'%s%s",
                     g_ui.fcurves[g_ui.fe_pr_sel - 4],
                     fm->note[0] ? " · " : "", fm->note);
        }
    } else {
        if (s_pr_cached_sel != g_ui.fe_pr_sel) {
            memset(&s_pr_cpu, 0, sizeof(s_pr_cpu));
            memset(&s_pr_gpu, 0, sizeof(s_pr_gpu));
            memset(&s_pr_info, 0, sizeof(s_pr_info));
            fan_preset_get(g_ui.hw, (fan_preset_t)g_ui.fe_pr_sel,
                           &s_pr_cpu, &s_pr_gpu, &s_pr_info);
            s_pr_cached_sel = g_ui.fe_pr_sel;
        }
        prev_cpu = &s_pr_cpu;
        prev_gpu = &s_pr_gpu;
        snprintf(src, sizeof(src), "Source: %s", preset_src_str(s_pr_info.src));
    }

    if (prev_y <= last - 2) {
        ui_hline(n, x, prev_y - 1, w, pal->border);
        ui_putln(n, x, prev_y, w, src, pal->muted, true);
    }

    char c_sum[128], g_sum[128], c_line[160], g_line[160];
    fmt_curve_summary(prev_cpu, c_sum, sizeof(c_sum));
    fmt_curve_summary(prev_gpu, g_sum, sizeof(g_sum));

    if (g_ui.fe_pr_target_cpu)
        snprintf(c_line, sizeof(c_line), "CPU: %s", c_sum);
    else
        snprintf(c_line, sizeof(c_line), "CPU: (not targeted)");
    if (prev_y + 1 <= last - 1)
        ui_putln(n, x, prev_y + 1, w, c_line,
                 g_ui.fe_pr_target_cpu ? pal->text : pal->muted, false);

    if (g_ui.fe_pr_target_gpu)
        snprintf(g_line, sizeof(g_line), "GPU: %s", g_sum);
    else
        snprintf(g_line, sizeof(g_line), "GPU: (not targeted)");
    if (prev_y + 2 <= last - 1)
        ui_putln(n, x, prev_y + 2, w, g_line,
                 g_ui.fe_pr_target_gpu ? pal->text : pal->muted, false);

    int by = last;
    bool can_apply = g_ui.fe_pr_target_cpu || g_ui.fe_pr_target_gpu;
    ui_btn(n, x, by, " Load into editor (Enter) ", can_apply, false,
           TGT(TGT_PANEL_FAN_PRESETS, ACT_FPR_APPLY));
    ui_btn(n, x + 27, by, " Cancel (Esc) ", false, false,
           TGT(TGT_PANEL_FAN_PRESETS, ACT_FPR_CANCEL));
}

void editor_fan_presets_key(uint32_t key)
{
    int tot = pr_total_count();
    switch (key) {
    case NCKEY_ESC:
    case 'q':
    case 'Q':
    case 'm':
    case 'M':
        g_ui.fe_presets_overlay = false;
        return;
    case '1': g_ui.fe_pr_sel = 0; return;
    case '2': g_ui.fe_pr_sel = 1; return;
    case '3': g_ui.fe_pr_sel = 2; return;
    case '4': g_ui.fe_pr_sel = 3; return;
    case 'j':
    case NCKEY_DOWN:
        g_ui.fe_pr_sel = (g_ui.fe_pr_sel + 1) % tot;
        return;
    case 'k':
    case NCKEY_UP:
        g_ui.fe_pr_sel = (g_ui.fe_pr_sel + tot - 1) % tot;
        return;
    case 'c':
    case 'C':
        g_ui.fe_pr_target_cpu = !g_ui.fe_pr_target_cpu;
        return;
    case 'g':
    case 'G':
        g_ui.fe_pr_target_gpu = !g_ui.fe_pr_target_gpu;
        return;
    case NCKEY_ENTER:
    case '\r':
    case '\n':
    case ' ':
        apply_selected_preset();
        return;
    default:
        return;
    }
}

void editor_fan_presets_act(int id)
{
    int tot = pr_total_count();
    switch (id) {
    case ACT_FPR_TARGET_CPU:
        g_ui.fe_pr_target_cpu = !g_ui.fe_pr_target_cpu;
        return;
    case ACT_FPR_TARGET_GPU:
        g_ui.fe_pr_target_gpu = !g_ui.fe_pr_target_gpu;
        return;
    case ACT_FPR_APPLY:
        apply_selected_preset();
        return;
    case ACT_FPR_CANCEL:
        g_ui.fe_presets_overlay = false;
        return;
    default:
        if (id >= ACT_FPR_ITEM_BASE && id < ACT_FPR_ITEM_BASE + tot) {
            int sel = id - ACT_FPR_ITEM_BASE;
            if (g_ui.fe_pr_sel == sel)
                apply_selected_preset();
            else
                g_ui.fe_pr_sel = sel;
            return;
        }
        break;
    }
}
