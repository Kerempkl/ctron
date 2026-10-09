#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ui_internal.h"
#include "../control.h"
#include "../util.h"

#include <stdio.h>
#include <string.h>

/* Big fan curve editor in the workspace. The graph scales to the whole
 * rectangle; points can be nudged with the keyboard (h/l temp, j/k pwm),
 * typed exactly (t/p fields + Enter) or clicked (select / add / move). */

enum {
    ACT_FE_CPU = ACT_FE_BASE, /* 100 — outside the workspace ACT_WS_* id
                               * space; clicks are forwarded back here
                               * from panel_workspace_act */
    ACT_FE_GPU,
    ACT_FE_ADD,
    ACT_FE_DEL,
    ACT_FE_WRITE,
    ACT_FE_TOGGLE,
    ACT_FE_T_FIELD,
    ACT_FE_P_FIELD,
    ACT_FE_SET,
    ACT_FE_PRESET_BASE = ACT_FE_BASE + 10, /* stock/silent/cool/full */
    ACT_FE_NODE_BASE = ACT_FE_BASE + 30,   /* + point index */
};

static rect_t s_graph;

const rect_t *editor_fan_graph(void)
{
    return &s_graph;
}

static fan_curve_t *cur_curve(void)
{
    return g_ui.fe_gpu ? &g_ui.hw->fan_gpu : &g_ui.hw->fan_cpu;
}

/* any curve mutation marks the in-memory table as newer than the
 * hwmon one, so a live refresh will not clobber it (ctrl_fan_write
 * clears the flag after a verified write) */
static void fan_touch(void)
{
    g_ui.hw->fan_staged = true;
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
    if (fc->n > 0 && g_ui.fe_sel >= 0 && g_ui.fe_sel < fc->n) {
        char b[16];
        snprintf(b, sizeof(b), "%d", fc->temp_c[g_ui.fe_sel]);
        tin_set(&g_ui.fe_x, b);
        snprintf(b, sizeof(b), "%d", fc->pwm[g_ui.fe_sel]);
        tin_set(&g_ui.fe_y, b);
    }
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
    } else {
        g_ui.fe_sel = -1;
    }

    int x = r->x + 2;
    int w = r->w - 4;

    /* control row */
    int cy = r->y + 1;
    bool on = g_ui.fe_gpu ? hw->fan_gpu_on : hw->fan_cpu_on;
    const ui_btndef_t ctl[] = {
        { "CPU",    !g_ui.fe_gpu, TGT(TGT_PANEL_WORKSPACE, ACT_FE_CPU) },
        { "GPU",     g_ui.fe_gpu, TGT(TGT_PANEL_WORKSPACE, ACT_FE_GPU) },
        { "+",       false,       TGT(TGT_PANEL_WORKSPACE, ACT_FE_ADD) },
        { "-",       false,       TGT(TGT_PANEL_WORKSPACE, ACT_FE_DEL) },
        { " Write ", false,       TGT(TGT_PANEL_WORKSPACE, ACT_FE_WRITE) },
        { on ? " ON " : " OFF ", on, TGT(TGT_PANEL_WORKSPACE, ACT_FE_TOGGLE) },
    };
    ui_btn_row(n, cy, x, w, ctl, 6);

    /* graph area */
    s_graph.x = r->x + 6;
    s_graph.y = r->y + 3;
    s_graph.w = r->w - 9;
    s_graph.h = r->h - 9;
    if (s_graph.w < 10)
        s_graph.w = 10;
    if (s_graph.h < 4)
        s_graph.h = 4;

    uint64_t ch_ax = 0, ch_ln = 0, ch_pt = 0, ch_lb = 0;
    ncchannels_set_fg_rgb(&ch_ax, pal->border);
    ncchannels_set_fg_rgb(&ch_ln, pal->accent2);
    ncchannels_set_fg_rgb(&ch_pt, pal->accent);
    ncchannels_set_fg_rgb(&ch_lb, pal->muted);

    /* axes + clear */
    ncplane_set_channels(n, ch_ax);
    for (int yy = 0; yy < s_graph.h; yy++) {
        ncplane_putstr_yx(n, s_graph.y + yy, s_graph.x - 1, "│");
        ncplane_cursor_move_yx(n, s_graph.y + yy, s_graph.x);
        for (int xx = 0; xx < s_graph.w; xx++)
            ncplane_putchar(n, ' ');
    }
    ncplane_putstr_yx(n, s_graph.y + s_graph.h, s_graph.x - 1, "└");
    for (int xx = 0; xx < s_graph.w; xx++)
        ncplane_putstr(n, "─");

    ncplane_set_channels(n, ch_lb);
    ncplane_putstr_yx(n, s_graph.y, s_graph.x - 5, "255");
    ncplane_putstr_yx(n, s_graph.y + s_graph.h - 1, s_graph.x - 5, "  0");
    ncplane_putstr_yx(n, s_graph.y + s_graph.h, s_graph.x, "20°C");
    {
        char lb[16];
        snprintf(lb, sizeof(lb), "%d", FAN_TMAX);
        ncplane_putstr_yx(n, s_graph.y + s_graph.h, s_graph.x + s_graph.w - 4, lb);
    }

    /* value guides ('i'): dotted 10 °C verticals + 25 % horizontals
     * with axis labels, so "what °C maps to what PWM" is readable
     * off the graph instead of guessed from the point chips */
    if (g_ui.fe_grid) {
        static const int G_PWM[] = { 64, 128, 192 }; /* ~25/50/75 % */
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
            ncplane_putstr_yx(n, gy, s_graph.x - 3, lb);
        }
    }

    /* live temp marker on the axis */
    if (hw->cpu_temp > 0 && !g_ui.fe_gpu) {
        int lx = s_graph.x + ut_clamp_i(hw->cpu_temp, FAN_TMIN, FAN_TMAX) *
                 (s_graph.w - 1) / (FAN_TMAX - FAN_TMIN);
        ncplane_set_channels(n, ch_pt);
        ncplane_putstr_yx(n, s_graph.y + s_graph.h, lx, "▲");
    }

    /* polyline + points */
    for (int i = 0; i + 1 < fc->n; i++)
        plot_line(n, px(fc, i), py(fc, i), px(fc, i + 1), py(fc, i + 1), ch_ln);
    ncplane_set_channels(n, ch_pt);
    ncplane_set_styles(n, NCSTYLE_BOLD);
    for (int i = 0; i < fc->n; i++)
        ncplane_putstr_yx(n, py(fc, i), px(fc, i),
                          i == g_ui.fe_sel ? "◆" : "●");
    ncplane_set_styles(n, NCSTYLE_NONE);

    /* point chips under the graph */
    int chy = r->y + r->h - 4;
    for (int i = 0; i < fc->n; i++) {
        char chip[20];
        snprintf(chip, sizeof(chip), "%d:%d,%d", i + 1, fc->temp_c[i], fc->pwm[i]);
        int cx = x + (i % 4) * 13;
        int cyy = chy + i / 4;
        if (cyy < r->y + r->h - 2) {
            ui_btn(n, cx, cyy, chip, i == g_ui.fe_sel, false,
                   TGT(TGT_PANEL_WORKSPACE, ACT_FE_NODE_BASE + i));
        }
    }

    /* X/Y input row */
    int iy = r->y + r->h - 2;
    char tb[24], pb[24];
    snprintf(tb, sizeof(tb), "T[%.6s%s]", g_ui.fe_x.buf, g_ui.fe_input == 1 ? "_" : "");
    snprintf(pb, sizeof(pb), "P[%.6s%s]", g_ui.fe_y.buf, g_ui.fe_input == 2 ? "_" : "");
    const ui_btndef_t xy[] = {
        { tb,    g_ui.fe_input == 1, TGT(TGT_PANEL_WORKSPACE, ACT_FE_T_FIELD) },
        { pb,    g_ui.fe_input == 2, TGT(TGT_PANEL_WORKSPACE, ACT_FE_P_FIELD) },
        { "Set", false,              TGT(TGT_PANEL_WORKSPACE, ACT_FE_SET) },
        { "Stk",  false,             TGT(TGT_PANEL_WORKSPACE, ACT_FE_PRESET_BASE + 0) },
        { "Sil",  false,             TGT(TGT_PANEL_WORKSPACE, ACT_FE_PRESET_BASE + 1) },
        { "Col",  false,             TGT(TGT_PANEL_WORKSPACE, ACT_FE_PRESET_BASE + 2) },
        { "Ful",  false,             TGT(TGT_PANEL_WORKSPACE, ACT_FE_PRESET_BASE + 3) },
    };
    ui_btn_row(n, iy, x, w, xy, 7);
}

/* ---- input -------------------------------------------------------------- */

static void apply_xy(void)
{
    if (g_ui.fe_sel < 0)
        return;
    int t = atoi(g_ui.fe_x.buf);
    int p = atoi(g_ui.fe_y.buf);
    g_ui.fe_sel = fan_set_point(cur_curve(), g_ui.fe_sel, t, p);
    fill_xy_bufs();
    g_ui.fe_input = 0;
    fan_touch();
    ut_log("fan pt %d set", g_ui.fe_sel + 1);
}

void editor_fan_key(uint32_t key)
{
    fan_curve_t *fc = cur_curve();

    /* typing fields swallow keys */
    if (g_ui.fe_input == 1 || g_ui.fe_input == 2) {
        tinput_t *t = g_ui.fe_input == 1 ? &g_ui.fe_x : &g_ui.fe_y;
        if (key == NCKEY_ESC) {
            g_ui.fe_input = 0;
            return;
        }
        if (key == NCKEY_ENTER || key == '\r' || key == '\n') {
            apply_xy();
            return;
        }
        if (key == '\t') {
            g_ui.fe_input = g_ui.fe_input == 1 ? 2 : 1;
            return;
        }
        if (key >= '0' && key <= '9') {
            tin_key(t, key);
            return;
        }
        if (key == NCKEY_BACKSPACE || key == 127 || key == 8) {
            tin_key(t, key);
            return;
        }
        g_ui.fe_input = 0; /* any navigation key drops the field */
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
    case '+':
    case 'a':
    case 'A': {
        int t = g_ui.fe_x.buf[0] ? atoi(g_ui.fe_x.buf) : -1;
        int p = g_ui.fe_y.buf[0] ? atoi(g_ui.fe_y.buf) : -1;
        g_ui.fe_sel = fan_add_point(fc, t, p);
        fill_xy_bufs();
        g_ui.fe_input = 1;
        fan_touch();
        break;
    }
    case '-':
    case 'x':
    case 'X':
        g_ui.fe_sel = fan_del_point(fc, g_ui.fe_sel);
        fill_xy_bufs();
        fan_touch();
        break;
    case 'w':
    case 'W':
        ui_flash("applying fan curve...");
        ctrl_fan_write(g_ui.hw);
        break;
    case 'o':
    case 'O':
        if (g_ui.fe_gpu)
            ctrl_fan_set_enabled(g_ui.hw, g_ui.hw->fan_cpu_on, !g_ui.hw->fan_gpu_on);
        else
            ctrl_fan_set_enabled(g_ui.hw, !g_ui.hw->fan_cpu_on, g_ui.hw->fan_gpu_on);
        break;
    case 't':
    case 'T':
        g_ui.fe_input = 1;
        tin_clear(&g_ui.fe_x);
        break;
    /* 'P' is deliberately not bound here: dispatch gives it to the
     * POWER view hotkey; lowercase p types the pwm field */
    case 'p':
        g_ui.fe_input = 2;
        tin_clear(&g_ui.fe_y);
        break;
    case 'h':
    case NCKEY_LEFT:
        if (g_ui.fe_sel >= 0) {
            g_ui.fe_sel = fan_nudge_point(fc, g_ui.fe_sel, -1, 0);
            fill_xy_bufs();
            fan_touch();
        }
        break;
    case 'l':
    case NCKEY_RIGHT:
        if (g_ui.fe_sel >= 0) {
            g_ui.fe_sel = fan_nudge_point(fc, g_ui.fe_sel, 1, 0);
            fill_xy_bufs();
            fan_touch();
        }
        break;
    case 'j':
    case NCKEY_DOWN:
        if (g_ui.fe_sel >= 0) {
            g_ui.fe_sel = fan_nudge_point(fc, g_ui.fe_sel, 0, -5);
            fill_xy_bufs();
            fan_touch();
        }
        break;
    case 'k':
    case NCKEY_UP:
        if (g_ui.fe_sel >= 0) {
            g_ui.fe_sel = fan_nudge_point(fc, g_ui.fe_sel, 0, 5);
            fill_xy_bufs();
            fan_touch();
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

    /* button clicks carry an ACT_FE_* id; id == 0 is a graph click */
    if (id >= ACT_FE_BASE && id < ACT_FE_END) {
        switch (id) {
        case ACT_FE_CPU:
            g_ui.fe_gpu = 0;
            fill_xy_bufs();
            return;
        case ACT_FE_GPU:
            g_ui.fe_gpu = 1;
            fill_xy_bufs();
            return;
        case ACT_FE_ADD: {
            int t = g_ui.fe_x.buf[0] ? atoi(g_ui.fe_x.buf) : -1;
            int p = g_ui.fe_y.buf[0] ? atoi(g_ui.fe_y.buf) : -1;
            g_ui.fe_sel = fan_add_point(fc, t, p);
            fill_xy_bufs();
            g_ui.fe_input = 1;
            fan_touch();
            return;
        }
        case ACT_FE_DEL:
            g_ui.fe_sel = fan_del_point(fc, g_ui.fe_sel);
            fill_xy_bufs();
            fan_touch();
            return;
        case ACT_FE_WRITE:
            ui_flash("applying fan curve...");
            ctrl_fan_write(g_ui.hw);
            return;
        case ACT_FE_TOGGLE:
            if (g_ui.fe_gpu)
                ctrl_fan_set_enabled(g_ui.hw, g_ui.hw->fan_cpu_on, !g_ui.hw->fan_gpu_on);
            else
                ctrl_fan_set_enabled(g_ui.hw, !g_ui.hw->fan_cpu_on, g_ui.hw->fan_gpu_on);
            return;
        case ACT_FE_T_FIELD:
            g_ui.fe_input = 1;
            tin_clear(&g_ui.fe_x);
            return;
        case ACT_FE_P_FIELD:
            g_ui.fe_input = 2;
            tin_clear(&g_ui.fe_y);
            return;
        case ACT_FE_SET:
            apply_xy();
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

    /* graph click: nearest point within 2 cells? */
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
            /* second click on the selected point moves it */
            g_ui.fe_sel = fan_set_point(fc, near, t, p);
            fan_touch();
        } else {
            g_ui.fe_sel = near;
        }
    } else if (fc->n < FAN_POINTS) {
        g_ui.fe_sel = fan_add_point(fc, t, p);
        fan_touch();
    } else {
        g_ui.fe_sel = fan_set_point(fc, g_ui.fe_sel, t, p);
        fan_touch();
    }
    fill_xy_bufs();
}
