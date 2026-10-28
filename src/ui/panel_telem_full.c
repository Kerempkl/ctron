#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ui_internal.h"
#include "../hw.h"
#include "../telem.h"
#include "../util.h"

#include <stdio.h>
#include <string.h>

/* Fullscreen TELEMETRY ('t'): read-only rolling view over the telem
 * rings. Sections are a REGISTRY — adding, removing or moving one is
 * an edit to SECTIONS[] below; nothing else in the tree changes. The
 * screen never writes hardware; sampling happens in telem_poll(),
 * which ui.c calls after every fast refresh.
 *
 * Two modes: the section grid (j/k cursor, enter/1-7 opens) and one
 * section's detail page — full-width multi-row charts over the same
 * rings plus info lines from the live hw snapshot. */

typedef void (*telem_sec_fn)(struct ncplane *n, const rect_t *r);

typedef struct {
    const char *title;
    telem_sec_fn draw;    /* grid box */
    telem_sec_fn detail;  /* full page inside the overlay */
} telem_section_t;

#define TELEM_SEC_N ((int)(sizeof SECTIONS / sizeof SECTIONS[0]))

/* ---- small format helpers ------------------------------------------- */

static void fmt_int(char *out, size_t n, const char *unit, int v)
{
    if (v < 0)
        snprintf(out, n, "--");
    else
        snprintf(out, n, "%d%s", v, unit);
}

static void fmt_bs(char *out, size_t n, double bs)
{
    if (bs < 0) {
        snprintf(out, n, "--");
    } else if (bs >= 1024.0 * 1024.0) {
        snprintf(out, n, "%.1f MB/s", bs / (1024.0 * 1024.0));
    } else if (bs >= 1024.0) {
        snprintf(out, n, "%.0f kB/s", bs / 1024.0);
    } else {
        snprintf(out, n, "%.0f B/s", bs);
    }
}

/* plain text, not muted: stats/info lines carry real numbers and the
 * muted grey was unreadable on dark themes (user report) */
static void info_line(struct ncplane *n, int x, int y, int w, const char *s)
{
    ui_putln(n, x, y, w, s, ui_palette(g_prefs.theme)->text, false);
}

/* filled/empty block bar (█/░), cell-safe */
static void bar(char *out, size_t n, int used, int total, int cells)
{
    double f = total > 0 ? (double)used / (double)total : 0;
    int fill = (int)(f * cells + 0.5);
    if (fill > cells)
        fill = cells;
    size_t w = 0;
    for (int i = 0; i < cells && w + 4 <= n - 1; i++) {
        out[w++] = (char)0xE2;
        out[w++] = (char)0x96;
        out[w++] = (char)(i < fill ? 0x88 : 0x91);
    }
    out[w] = '\0';
}

/* ---- detail-page charts ------------------------------------------------ */

typedef enum { U_TEMP, U_MHZ, U_W, U_PCT, U_LOAD, U_MBPS, U_MB } unit_t;

static void fmt_val(char *out, size_t n, unit_t u, double v)
{
    if (v < 0) {
        snprintf(out, n, "--");
    } else {
        switch (u) {
        case U_TEMP: snprintf(out, n, "%.1f °C", v); break;
        case U_MHZ:  snprintf(out, n, "%.0f MHz", v); break;
        case U_W:    snprintf(out, n, "%.2f W", v / 1000.0); break;
        case U_PCT:  snprintf(out, n, "%.0f %%", v); break;
        case U_LOAD: snprintf(out, n, "%.2f", v); break;
        case U_MBPS: fmt_bs(out, n, v); break;
        case U_MB:   snprintf(out, n, "%.0f MB", v); break;
        }
    }
}

/* solid block area chart (btop-style) with a WASH fill: the area
 * under the curve is a solid background color wash (no glyphs — the
 * fill has zero texture), and the curve itself is an eighth-block
 * edge (▁▂▃▄▅▆▇█) painted bright over the same wash so it rises
 * smoothly out of the area. rows*8 vertical levels; each column
 * keeps its window slice's PEAK so a spike between columns still
 * shows; the newest sample carries a bright dot marker. */
#define CHART_COLS_MAX 256
#define CHART_ROWS_MAX 12

static double s_rng_mn[TS_COUNT], s_rng_mx[TS_COUNT];
static bool s_rng_ok[TS_COUNT];

static double dmax3(double a, double b, double c)
{
    return a > b ? (a > c ? a : c) : (b > c ? b : c);
}

/* Fixed series ranges (0-100) pass through untouched — they never
 * rescale, so none of this applies. AUTO windows get: a minimum span
 * of 25% of the larger magnitude (a near-flat series must LOOK flat,
 * not explode into full-height noise), 5% headroom pad, and an
 * exponential follow that hides the rescale jump every time the 60 s
 * window slides a sample out. */
static void chart_scale(int sidx, int count, double *mnp, double *mxp)
{
    double mn, mx;
    telem_range(sidx, &mn, &mx);
    if (mn != TELEM_AUTO || mx != TELEM_AUTO) {
        *mnp = mn;
        *mxp = mx;
        return;
    }
    if (count <= 0) {
        *mnp = *mxp = -1;
        return;
    }

    telem_minmax(sidx, &mn, &mx);
    double span = mx > mn ? mx - mn : 0.0;
    double mag = mn < 0 ? -mn : mn; /* magnitudes here are >= 0 */
    double minspan = 0.25 * dmax3(mx, mag, 1.0);
    if (span < minspan) {
        double mid = (mx + mn) / 2.0;
        mn = mid - minspan / 2.0;
        mx = mid + minspan / 2.0;
    } else {
        double pad = 0.05 * span;
        mn -= pad;
        mx += pad;
    }

    if (!s_rng_ok[sidx]) {
        s_rng_mn[sidx] = mn;
        s_rng_mx[sidx] = mx;
        s_rng_ok[sidx] = true;
    } else {
        s_rng_mn[sidx] += 0.2 * (mn - s_rng_mn[sidx]);
        s_rng_mx[sidx] += 0.2 * (mx - s_rng_mx[sidx]);
        if (s_rng_mx[sidx] <= s_rng_mn[sidx]) {
            s_rng_mn[sidx] = mn;
            s_rng_mx[sidx] = mx;
        }
    }
    *mnp = s_rng_mn[sidx];
    *mxp = s_rng_mx[sidx];
}

/* write a run with BOTH colors: the wash needs a bg, and ui_putln
 * only takes a fg (every other writer resets channels itself, so
 * this can never leak) */
static void put_cells(struct ncplane *n, int x, int y, const char *s,
                      uint32_t fg, uint32_t bg)
{
    ncplane_cursor_move_yx(n, y, x);
    uint64_t ch = 0;
    ncchannels_set_fg_rgb(&ch, fg);
    ncchannels_set_bg_rgb(&ch, bg);
    ncplane_set_channels(n, ch);
    ncplane_putstr(n, s);
}

/* bottom-based row of a column's top cell, from its height in eighths */
static int col_top(int h8)
{
    return h8 % 8 > 0 ? h8 / 8 : h8 / 8 - 1;
}

static void chart(struct ncplane *n, int x, int y, int w, int rows,
                  int sidx, double mn, double mx)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int count = telem_count(sidx);
    if (count < 1 || rows < 1 || !(mx > mn))
        return;
    if (rows > CHART_ROWS_MAX)
        rows = CHART_ROWS_MAX;
    int cols = w - 2;
    if (cols < 4)
        cols = 4;
    if (cols > CHART_COLS_MAX)
        cols = CHART_COLS_MAX;

    int lv8[CHART_COLS_MAX] = {0}; /* column height in eighths */
    int last = -1;                 /* newest column with a sample */
    for (int c = 0; c < cols; c++) {
        int lo = (int)((long long)c * count / cols);
        int hi = (int)((long long)(c + 1) * count / cols);
        if (hi <= lo)
            hi = lo + 1;
        double peak = -1, v;
        for (int k = lo; k < hi && k < count; k++) {
            if (telem_at(sidx, count - 1 - k, &v) && v > peak)
                peak = v;
        }
        if (peak < 0)
            continue; /* unknown tick: empty column */
        double f = (peak - mn) / (mx - mn);
        if (f < 0)
            f = 0;
        if (f > 1)
            f = 1;
        int h = (int)(f * (rows * 8) + 0.5);
        if (h > rows * 8)
            h = rows * 8;
        lv8[c] = h;
        last = c;
    }

    /* wash = accent halved into the theme bg: a quiet tint that reads
     * as "the area" without any glyph texture */
    uint32_t wash = (((pal->accent >> 1) & 0x7f7f7f) +
                     ((pal->bg >> 1) & 0x7f7f7f)) & 0xffffff;

    char buf[CHART_COLS_MAX * 3 + 8];
    for (int rr = 0; rr < rows; rr++) {
        int br = rows - 1 - rr; /* bottom-based row index */
        int c = 0;
        while (c < cols) {
            while (c < cols && (lv8[c] <= 0 || br > col_top(lv8[c])))
                c++; /* skip columns above the curve on this row */
            if (c >= cols)
                break;
            size_t wl = 0;
            int start = c;
            while (c < cols && lv8[c] > 0 && br <= col_top(lv8[c]) &&
                   wl + 4 <= sizeof buf - 1) {
                int rem = lv8[c] % 8;
                if (br == col_top(lv8[c])) { /* the curve itself */
                    buf[wl++] = (char)0xE2;
                    buf[wl++] = (char)0x96;
                    buf[wl++] = rem > 0 ? (char)(0x81 + rem - 1)
                                        : (char)0x88;
                } else { /* inside the area: plain wash */
                    buf[wl++] = ' ';
                }
                c++;
            }
            buf[wl] = '\0';
            put_cells(n, x + 1 + start, y + rr, buf, pal->accent, wash);
        }
    }

    /* newest sample: a bright dot so the current value pops */
    if (last >= 0) {
        char dot[4] = {(char)0xE2, (char)0x97, (char)0x8F, 0}; /* ● */
        put_cells(n, x + 1 + last, y + (rows - 1) - col_top(lv8[last]),
                  dot, pal->text, wash);
    }
}

typedef struct {
    const char *label;
    unit_t u;
    int sidx;
} chartdef_t;

/* the series each section charts — one list per section, shared by
 * its grid box and its detail page so the two never drift apart */
static const chartdef_t CD_CPU[] = {
    { "temp", U_TEMP, TS_CPU_TEMP },
    { "clock", U_MHZ, TS_CPU_MHZ },
    { "load", U_LOAD, TS_CPU_LOAD },
};
static const chartdef_t CD_GPU[] = {
    { "temp", U_TEMP, TS_GPU_TEMP },
    { "power", U_W, TS_GPU_MW },
    { "clock", U_MHZ, TS_GPU_MHZ },
    { "util", U_PCT, TS_GPU_UTIL },
};
static const chartdef_t CD_IGPU[] = {
    { "temp", U_TEMP, TS_IGPU_TEMP },
    /* amdgpu power1_input is the SoC PPT: the WHOLE APU package
     * (CPU cores + iGPU + fabric), not the iGPU rail alone */
    { "APU PPT", U_W, TS_IGPU_MW },
    { "clock", U_MHZ, TS_IGPU_MHZ },
};
static const chartdef_t CD_RAM[] = {
    { "used", U_MB, TS_MEM_USED },
};
static const chartdef_t CD_DISK[] = {
    { "read", U_MBPS, TS_DISK_RD },
    { "write", U_MBPS, TS_DISK_WR },
};
static const chartdef_t CD_NET[] = {
    { "rx", U_MBPS, TS_NET_RX },
    { "tx", U_MBPS, TS_NET_TX },
};
static const chartdef_t CD_POWER[] = {
    { "draw", U_W, TS_BAT_MW },
    { "charge", U_PCT, TS_BAT_PCT },
    { "usb-c", U_W, TS_PD_MW },
};
#define CD_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static int utf_cells(const char *s)
{
    int c = 0;
    for (const char *p = s; *p; p++)
        if ((*p & 0xC0) != 0x80)
            c++;
    return c;
}

/* one labelled block: value row, `rows` chart rows with a y-axis of
 * the actual chart scale (top = max, bottom = min) so every graph
 * says what its height means on its own, optional stats row */
static void chart_block(struct ncplane *n, int x, int y, int w, int rows,
                        const chartdef_t *cd, bool stats)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    char v[40], line[160];
    double last = telem_last(cd->sidx);
    fmt_val(v, sizeof v, cd->u, last);
    snprintf(line, sizeof line, " %-10s %s", cd->label, v);
    ui_putln(n, x, y, w, line, pal->text, false);

    int count = telem_count(cd->sidx);
    double mn, mx;
    chart_scale(cd->sidx, count, &mn, &mx);
    if (count < 1 || !(mx > mn))
        return;

    char bt[40], bb[40], pad[48];
    fmt_val(bt, sizeof bt, cd->u, mx);
    fmt_val(bb, sizeof bb, cd->u, mn);
    int ax = utf_cells(bt);
    if (utf_cells(bb) > ax)
        ax = utf_cells(bb);
    int cw = w - ax - 3; /* axis column + gap + right margin */
    if (rows >= 2 && cw >= 8) {
        snprintf(pad, sizeof pad, "%*s%s", ax - utf_cells(bt), "", bt);
        ui_putln(n, x + 1, y + 1, ax, pad, pal->text, false);
        snprintf(pad, sizeof pad, "%*s%s", ax - utf_cells(bb), "", bb);
        ui_putln(n, x + 1, y + rows, ax, pad, pal->text, false);
        chart(n, x + ax + 2, y, cw, rows, cd->sidx, mn, mx);
    } else {
        chart(n, x, y, w, rows, cd->sidx, mn, mx);
    }

    if (!stats)
        return;
    double wmn, wmx;
    telem_minmax(cd->sidx, &wmn, &wmx);
    char a[40], b[40], c2[40];
    fmt_val(a, sizeof a, cd->u, wmn);
    fmt_val(b, sizeof b, cd->u, wmx);
    fmt_val(c2, sizeof c2, cd->u, last);
    snprintf(line, sizeof line, " min %s · max %s · last %s", a, b, c2);
    info_line(n, x, y + 1 + rows, w, line);
}

/* stacked full-width chart blocks with trailing info lines, shared
 * by the detail pages and the grid boxes: detail passes stats=true
 * (min/max/last per chart, min height 2, tall blocks); the grid
 * passes stats=false with min height 1 and a low cap. Charts shrink
 * first and then drop (newest-last) on short terminals; a box that
 * fits nothing shows the info lines alone. */
static void stack_charts(struct ncplane *n, const rect_t *r,
                         const chartdef_t *cd, int ncd, bool stats,
                         int minper, int maxper,
                         const char *const *info, int ninfo)
{
    int x = r->x + 2, w = r->w - 4;
    int avail = r->h - 2;
    if (avail < 1)
        return;
    int over = stats ? 2 : 1; /* label row + optional stats row */
    int nch = ncd;
    while (nch > 0 && (avail - ninfo) / nch - over < minper)
        nch--;
    if (nch <= 0) {
        for (int i = 0; i < ninfo && i < avail; i++)
            info_line(n, x, r->y + 1 + i, w, info[i]);
        return;
    }
    int per = (avail - ninfo) / nch - over;
    if (per > maxper)
        per = maxper;
    int y = r->y + 1;
    for (int i = 0; i < nch; i++) {
        chart_block(n, x, y, w, per, &cd[i], stats);
        y += per + over;
    }
    for (int i = 0; i < ninfo; i++)
        info_line(n, x, y + i, w, info[i]);
}

/* grid-box content: one "label  value  sparkline" row per series,
 * the info line, and — when the box has room — a small wash chart of
 * the section's PRIMARY series (cd[0]) so every box also carries one
 * real graph without getting busy. */
static void grid_rows(struct ncplane *n, const rect_t *r,
                      const chartdef_t *cd, int ncd,
                      const char *const *info, int ninfo)
{
    int x = r->x + 2, w = r->w - 4;
    int avail = r->h - 2;
    if (avail < 1)
        return;
    const palette_t *pal = ui_palette(g_prefs.theme);
    int y = r->y + 1;
    for (int i = 0; i < ncd && i < avail; i++, y++) {
        char cur[40], spark[600], line[700];
        int cells = w - 26;
        if (cells < 6)
            cells = 6;
        if ((size_t)cells * 3 + 1 > sizeof spark)
            cells = (int)(sizeof spark - 1) / 3;
        telem_spark(cd[i].sidx, spark, sizeof spark, cells);
        fmt_val(cur, sizeof cur, cd[i].u, telem_last(cd[i].sidx));
        snprintf(line, sizeof line, " %-8s %-14s %s", cd[i].label, cur, spark);
        ui_putln(n, x, y, w, line, pal->text, false);
    }
    int yinfo = y;
    for (int i = 0; i < ninfo && i < avail - ncd; i++)
        info_line(n, x, yinfo + i, w, info[i]);
    int left = avail - ncd - ninfo;
    if (left >= 3) {
        double mn, mx;
        int cnt = telem_count(cd[0].sidx);
        chart_scale(cd[0].sidx, cnt, &mn, &mx);
        if (cnt > 0 && mx > mn)
            chart(n, x - 1, yinfo + ninfo, w + 2, left, cd[0].sidx, mn, mx);
    }
}

/* ---- sections (each draws inside its own boxed rect) ------------------
 * Grid boxes chart the SAME series as their detail pages (shared
 * CD_* lists), compact: label + chart, no stats row. On short boxes
 * charts shrink to 1 row and then drop; the info line stays. */

/* " · Tccd 85/88 °C" / " · Tccd 85 °C" / "" — both unknown says
 * nothing (Tctl is already on screen) */
static void fmt_tccd(char *out, size_t n, const hw_state_t *hw)
{
    if (hw->cpu_temp_ccd1 > 0 && hw->cpu_temp_ccd2 > 0)
        snprintf(out, n, " · Tccd %d/%d °C",
                 hw->cpu_temp_ccd1, hw->cpu_temp_ccd2);
    else if (hw->cpu_temp_ccd1 > 0)
        snprintf(out, n, " · Tccd %d °C", hw->cpu_temp_ccd1);
    else
        out[0] = '\0';
}

static void sec_cpu(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char line[192];
    char cc[40];
    fmt_tccd(cc, sizeof cc, hw);
    snprintf(line, sizeof line,
             " fan %d/%d rpm · %d cores · EPP %s%s",
             hw->rpm_cpu, hw->rpm_gpu, hw->cpu_n, hw_epp_name(hw->epp), cc);
    const char *info[1] = { line };
    grid_rows(n, r, CD_CPU, CD_COUNT(CD_CPU), info, 1);
}

static void sec_gpu(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char line[160];
    if (hw->gpu_vram_total_mb > 0)
        snprintf(line, sizeof line, " util %d%% · pstate %s · VRAM %d/%d MB",
                 hw->gpu_util,
                 hw->gpu_pstate[0] ? hw->gpu_pstate : "--",
                 hw->gpu_vram_used_mb, hw->gpu_vram_total_mb);
    else
        snprintf(line, sizeof line, " util %d%%", hw->gpu_util);
    const char *info[1] = { line };
    grid_rows(n, r, CD_GPU, CD_COUNT(CD_GPU), info, 1);
}

static void sec_igpu(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char vg[24], vn[24], line[96];
    fmt_int(vg, sizeof vg, " mV", hw->igpu_vddgfx_mv);
    fmt_int(vn, sizeof vn, " mV", hw->igpu_vddnb_mv);
    snprintf(line, sizeof line, " vddgfx %s · vddnb %s", vg, vn);
    const char *info[1] = { line };
    grid_rows(n, r, CD_IGPU, CD_COUNT(CD_IGPU), info, 1);
}

static void sec_ram(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char line[192];
    /* kernel max_alarm flag: "modules 57/66 °C !>55" — a module is
     * over its spd5118 limit */
    char al[24] = "";
    if (hw->ram_alarm) {
        if (hw->ram_thresh_max > 0)
            snprintf(al, sizeof al, " !>%d", hw->ram_thresh_max);
        else
            snprintf(al, sizeof al, " !");
    }
    if (hw->ram_temp_n == 2)
        snprintf(line, sizeof line, " modules %d/%d °C%s · swap %d/%d MB",
                 hw->ram_temp[0], hw->ram_temp[1], al,
                 hw->swap_used_mb, hw->swap_total_mb);
    else if (hw->ram_temp_n == 1)
        snprintf(line, sizeof line, " module %d °C%s · swap %d/%d MB",
                 hw->ram_temp[0], al, hw->swap_used_mb, hw->swap_total_mb);
    else
        snprintf(line, sizeof line, " swap %d/%d MB",
                 hw->swap_used_mb, hw->swap_total_mb);
    const char *info[1] = { line };
    grid_rows(n, r, CD_RAM, CD_COUNT(CD_RAM), info, 1);
}

static void sec_disk(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char line[200];
    if (hw->fs_total_mb > 0) {
        char b[120];
        int cells = r->w - 40;
        if (cells < 8)
            cells = 8;
        if ((size_t)cells * 3 + 1 > sizeof b)
            cells = (int)(sizeof b - 1) / 3;
        bar(b, sizeof b, hw->fs_used_mb, hw->fs_total_mb, cells);
        int pct = hw->fs_total_mb > 0
                      ? hw->fs_used_mb * 100 / hw->fs_total_mb : 0;
        if (hw->nvme_temp_n == 2)
            snprintf(line, sizeof line, " / %d%% %s · NVMe %d/%d °C",
                     pct, b, hw->nvme_temp[0], hw->nvme_temp[1]);
        else if (hw->nvme_temp_n == 1)
            snprintf(line, sizeof line, " / %d%% %s · NVMe %d °C",
                     pct, b, hw->nvme_temp[0]);
        else
            snprintf(line, sizeof line, " / %d%% %s", pct, b);
    } else {
        snprintf(line, sizeof line, " / --");
    }
    const char *info[1] = { line };
    grid_rows(n, r, CD_DISK, CD_COUNT(CD_DISK), info, 1);
}

static void sec_net(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char line[160];
    size_t w = 0;
    int len = snprintf(line, sizeof line, " ");
    if (len > 0)
        w = (size_t)len;
    for (int i = 0; i < hw->net_n && w < sizeof line; i++) {
        len = snprintf(line + w, sizeof line - w, "%s%s %s",
                       i ? " · " : "", hw->net_if[i],
                       hw->net_up[i] ? "up" : "down");
        if (len < 0 || (size_t)len >= sizeof line - w)
            break;
        w += (size_t)len;
    }
    if (hw->net_n == 0)
        snprintf(line, sizeof line, " no counted interfaces");
    const char *info[1] = { line };
    grid_rows(n, r, CD_NET, CD_COUNT(CD_NET), info, 1);
}

static void sec_power(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char line[176];
    int len = snprintf(line, sizeof line, " %s · %s · charge limit %d%%",
                       hw->bat_status, hw->ac_online ? "on AC" : "on battery",
                       hw->bat_limit);
    if (len > 0 && (size_t)len < sizeof line && hw->pd_in_mw > 0)
        snprintf(line + len, sizeof line - len, " · USB-C in %.1f W",
                 hw->pd_in_mw / 1000.0);
    const char *info[1] = { line };
    grid_rows(n, r, CD_POWER, hw->pd_n > 0 ? CD_COUNT(CD_POWER) : 2, info, 1);
}

/* ---- section detail pages (full page inside the overlay) ------------- */

static void detail_cpu(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char l0[128], l1[192], l2[192];
    char cc[40];
    fmt_tccd(cc, sizeof cc, hw);

    snprintf(l0, sizeof l0, " %d threads · %d cores · %d CCD · EPP %s · boost %s",
             hw->cpu_n, hw->core_n, hw->ccd_n, hw_epp_name(hw->epp),
             hw->cpu_boost ? "on" : "off");
    if (hw->ppt_spl > 0 || hw->ppt_sppt > 0 || hw->ppt_fppt > 0)
        snprintf(l1, sizeof l1, " PPT %d / %d / %d W · fans %d / %d rpm%s",
                 hw->ppt_spl, hw->ppt_sppt, hw->ppt_fppt,
                 hw->rpm_cpu, hw->rpm_gpu, cc);
    else
        snprintf(l1, sizeof l1, " PPT -- · fans %d / %d rpm%s",
                 hw->rpm_cpu, hw->rpm_gpu, cc);

    /* per-core ceilings: one 8-level block per thread, scaled to the
     * hardware max (a pinned core reads its ceiling, not live clock) */
    char blk[HW_CPU_MAX * 3 + 8];
    size_t wb = 0;
    blk[wb++] = ' ';
    for (int i = 0; i < hw->cpu_n && wb + 4 < sizeof blk; i++) {
        int mhz = hw->cpu_mhz_core[hw->cpu_ids[i]];
        if (mhz <= 0 || hw->cpu_mhz_max <= 0) {
            blk[wb++] = '.';
            continue;
        }
        double f = (double)mhz / (double)hw->cpu_mhz_max;
        if (f > 1)
            f = 1;
        blk[wb++] = (char)0xE2;
        blk[wb++] = (char)0x96;
        blk[wb++] = (char)(0x81 + (int)(f * 7.999));
    }
    blk[wb] = '\0';
    snprintf(l2, sizeof l2, "%s  per-core ceilings (max %d MHz)",
             blk, hw->cpu_mhz_max);

    const char *info[3] = { l0, l1, l2 };
    stack_charts(n, r, CD_CPU, CD_COUNT(CD_CPU), true, 2, CHART_ROWS_MAX,
                 info, 3);
}

static void detail_gpu(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char l0[160], l1[128], l2[128];
    if (hw->gpu_vram_total_mb > 0)
        snprintf(l0, sizeof l0, " VRAM %d / %d MB · clock max %d MHz",
                 hw->gpu_vram_used_mb, hw->gpu_vram_total_mb,
                 hw->gpu_mhz_max);
    else
        snprintf(l0, sizeof l0, " clock max %d MHz", hw->gpu_mhz_max);
    if (hw->gpu_clock_lock > 0)
        snprintf(l0 + strlen(l0), sizeof(l0) - strlen(l0),
                 " · locked %d MHz (this session)", hw->gpu_clock_lock);
    snprintf(l1, sizeof l1, " NV dynamic boost %s · temp target %s",
             hw->nv_boost > 0 ? "on" : "--",
             hw->nv_temp >= 70 ? "on" : "--");
    /* pstate is the driver's own performance level (P0 top .. P12
     * idle); throttle names come straight from the query, 0 = none */
    if (hw->gpu_pstate[0]) {
        char th[96];
        hw_gpu_throttle_str(hw->gpu_throttle, th, sizeof th);
        snprintf(l2, sizeof l2, " pstate %s · throttle: %s",
                 hw->gpu_pstate, th);
        const char *info[3] = { l0, l1, l2 };
        stack_charts(n, r, CD_GPU, CD_COUNT(CD_GPU), true, 2, CHART_ROWS_MAX,
                     info, 3);
        return;
    }
    const char *info[2] = { l0, l1 };
    stack_charts(n, r, CD_GPU, CD_COUNT(CD_GPU), true, 2, CHART_ROWS_MAX,
                 info, 2);
}

static void detail_igpu(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char vg[24], vn[24], l0[96], l1[128], l2[96];
    fmt_int(vg, sizeof vg, " mV", hw->igpu_vddgfx_mv);
    fmt_int(vn, sizeof vn, " mV", hw->igpu_vddnb_mv);
    snprintf(l0, sizeof l0, " vddgfx %s · vddnb %s", vg, vn);
    /* busy/DPM from the DRM device; gfx mW from gpu_metrics — the
     * only live slice of the package power this silicon reports */
    if (hw->igpu_busy >= 0)
        snprintf(l1, sizeof l1, " busy %d%%", hw->igpu_busy);
    else
        snprintf(l1, sizeof l1, " busy --");
    int blen = (int)strlen(l1);
    if (hw->igpu_dpm >= 0 && (size_t)blen < sizeof l1)
        snprintf(l1 + blen, sizeof l1 - blen, " · sclk DPM %d/%d @ %d MHz",
                 hw->igpu_dpm + 1, hw->igpu_dpm_n, hw->igpu_dpm_mhz);
    if (hw->igpu_gfx_mw >= 0)
        snprintf(l2, sizeof l2,
                 " APU PPT = whole-package (CPU+iGPU) · gfx rail %.1f W",
                 hw->igpu_gfx_mw / 1000.0);
    else
        snprintf(l2, sizeof l2, " APU PPT = whole-package power (CPU+iGPU)");
    const char *info[3] = { l0, l1, l2 };
    stack_charts(n, r, CD_IGPU, CD_COUNT(CD_IGPU), true, 2, CHART_ROWS_MAX,
                 info, 3);
}

static void detail_ram(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char l0[128], l1[96], l2[128];
    if (hw->mem_total_mb > 0)
        snprintf(l0, sizeof l0, " %d / %d MB · swap %d / %d MB",
                 hw->mem_used_mb, hw->mem_total_mb,
                 hw->swap_used_mb, hw->swap_total_mb);
    else
        snprintf(l0, sizeof l0, " swap %d / %d MB",
                 hw->swap_used_mb, hw->swap_total_mb);
    if (hw->ram_temp_n == 2)
        snprintf(l1, sizeof l1, " module temps %d / %d °C",
                 hw->ram_temp[0], hw->ram_temp[1]);
    else if (hw->ram_temp_n == 1)
        snprintf(l1, sizeof l1, " module temp %d °C", hw->ram_temp[0]);
    else {
        snprintf(l1, sizeof l1, " no spd5118 temp sensors");
        const char *info2[2] = { l0, l1 };
        stack_charts(n, r, CD_RAM, CD_COUNT(CD_RAM), true, 2, CHART_ROWS_MAX,
                     info2, 2);
        return;
    }
    /* spd5118 limits + which modules are flagged over max right now */
    char lim[96], mod[32] = "";
    if (hw->ram_thresh_max > 0 && hw->ram_thresh_crit > 0)
        snprintf(lim, sizeof lim, " limits max %d · crit %d °C",
                 hw->ram_thresh_max, hw->ram_thresh_crit);
    else
        snprintf(lim, sizeof lim, " limits --");
    if (hw->ram_alarm) {
        char *p = mod;
        size_t left = sizeof mod;
        int n = 0;
        for (int i = 0; i < hw->ram_temp_n; i++) {
            if (!(hw->ram_alarm & (1 << i)))
                continue;
            int w = snprintf(p, left, "%s%d", n ? "," : "", i + 1);
            if (w < 0 || (size_t)w >= left)
                break;
            p += w;
            left -= (size_t)w;
            n++;
        }
    }
    snprintf(l2, sizeof l2, "%s%s%s", lim,
             hw->ram_alarm ? " · over max: module " : "",
             hw->ram_alarm ? mod : "");
    const char *info[3] = { l0, l1, l2 };
    stack_charts(n, r, CD_RAM, CD_COUNT(CD_RAM), true, 2, CHART_ROWS_MAX,
                 info, 3);
}

static void detail_disk(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char l0[128], l1[200];
    if (hw->nvme_temp_n == 2)
        snprintf(l0, sizeof l0, " NVMe %d / %d °C · hottest %d / %d °C",
                 hw->nvme_temp[0], hw->nvme_temp[1],
                 hw->nvme_sensor_max[0] > 0 ? hw->nvme_sensor_max[0]
                                            : hw->nvme_temp[0],
                 hw->nvme_sensor_max[1] > 0 ? hw->nvme_sensor_max[1]
                                            : hw->nvme_temp[1]);
    else if (hw->nvme_temp_n == 1)
        snprintf(l0, sizeof l0, " NVMe %d °C · hottest %d °C",
                 hw->nvme_temp[0],
                 hw->nvme_sensor_max[0] > 0 ? hw->nvme_sensor_max[0]
                                            : hw->nvme_temp[0]);
    else
        snprintf(l0, sizeof l0, " no NVMe temp sensors");
    if (hw->fs_total_mb > 0) {
        char b[120];
        int cells = r->w - 48;
        if (cells < 8)
            cells = 8;
        if ((size_t)cells * 3 + 1 > sizeof b)
            cells = (int)(sizeof b - 1) / 3;
        bar(b, sizeof b, hw->fs_used_mb, hw->fs_total_mb, cells);
        snprintf(l1, sizeof l1, " / %d%% %s",
                 hw->fs_total_mb > 0
                     ? hw->fs_used_mb * 100 / hw->fs_total_mb : 0, b);
    } else {
        snprintf(l1, sizeof l1, " / --");
    }
    const char *info[2] = { l0, l1 };
    stack_charts(n, r, CD_DISK, CD_COUNT(CD_DISK), true, 2, CHART_ROWS_MAX,
                 info, 2);
}

static void detail_net(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char l[HW_NET_IF_MAX + 1][96];
    const char *info[HW_NET_IF_MAX + 1];
    int ni = 0;
    for (int i = 0; i < hw->net_n && ni < HW_NET_IF_MAX; i++) {
        char rb[20], tb[20];
        hw_fmt_bytes(rb, sizeof rb, hw->net_rx[i]);
        hw_fmt_bytes(tb, sizeof tb, hw->net_tx[i]);
        snprintf(l[ni], sizeof l[ni], " %s %s: rx %s · tx %s",
                 hw->net_if[i], hw->net_up[i] ? "up" : "down", rb, tb);
        info[ni] = l[ni];
        ni++;
    }
    if (ni == 0)
        snprintf(l[ni], sizeof l[ni],
                 " no counted interfaces (lo/veth/docker excluded)");
    else {
        char rb[20], tb[20];
        hw_fmt_bytes(rb, sizeof rb, hw->net_rx_tot);
        hw_fmt_bytes(tb, sizeof tb, hw->net_tx_tot);
        snprintf(l[ni], sizeof l[ni], " total rx %s · tx %s", rb, tb);
    }
    info[ni] = l[ni];
    ni++;
    stack_charts(n, r, CD_NET, CD_COUNT(CD_NET), true, 2, CHART_ROWS_MAX,
                 info, ni);
}

static void detail_power(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char l0[176], l1[128], l2[96];
    int len = snprintf(l0, sizeof l0, " %s · %s · charge limit %d%%",
                       hw->bat_status, hw->ac_online ? "on AC" : "on battery",
                       hw->bat_limit);
    if (len > 0 && (size_t)len < sizeof l0 && hw->pd_in_mw > 0)
        snprintf(l0 + len, sizeof l0 - len, " · USB-C in %.1f W",
                 hw->pd_in_mw / 1000.0);
    char hp[40];
    if (hw->bat_health >= 0)
        snprintf(hp, sizeof hp, "%d%% of design", hw->bat_health);
    else
        snprintf(hp, sizeof hp, "--");
    if (hw->bat_v_mv > 0 && hw->bat_a_ma > 0)
        snprintf(l1, sizeof l1, " battery %d.%02d V · %d.%02d A · health %s",
                 hw->bat_v_mv / 1000, (hw->bat_v_mv % 1000) / 10,
                 hw->bat_a_ma / 1000, (hw->bat_a_ma % 1000) / 10, hp);
    else
        snprintf(l1, sizeof l1, " battery health %s", hp);
    if (hw->pd_n > 0)
        snprintf(l2, sizeof l2, " USB-C PD ports: %d (in: %s)", hw->pd_n,
                 hw->pd_in_mw > 0 ? "live" : "--");
    const char *info[3] = { l0, l1, l2 };
    stack_charts(n, r, CD_POWER, hw->pd_n > 0 ? CD_COUNT(CD_POWER) : 2,
                 true, 2, CHART_ROWS_MAX, info, hw->pd_n > 0 ? 3 : 2);
}

/* ---- the registry: order here is order on screen -------------------- */

static const telem_section_t SECTIONS[] = {
    { "CPU", sec_cpu, detail_cpu },
    { "GPU", sec_gpu, detail_gpu },
    { "iGPU", sec_igpu, detail_igpu },
    { "RAM", sec_ram, detail_ram },
    { "STORAGE", sec_disk, detail_disk },
    { "NET", sec_net, detail_net },
    { "POWER", sec_power, detail_power },
};

void panel_telem_full_draw(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int nsec = TELEM_SEC_N;
    int det = g_ui.telem_detail;
    if (det < 0 || det >= nsec)
        det = -1;

    char title[64];
    if (det < 0)
        snprintf(title, sizeof title, "TELEMETRY");
    else
        snprintf(title, sizeof title, "TELEMETRY · %s", SECTIONS[det].title);
    ui_window(n, r, title);

    if (det >= 0) {
        rect_t ir = { r->x + 1, r->y + 1, r->w - 2, r->h - 3 };
        if (ir.w > 4 && ir.h > 2)
            SECTIONS[det].detail(n, &ir);
        ui_putln(n, r->x + 2, r->y + r->h - 2, r->w - 4,
                 " esc grid · h/l section · 1-7 jump · t close · q quit · read-only",
                 pal->muted, false);
        return;
    }

    int sel = g_ui.telem_sel;
    if (sel < 0 || sel >= nsec)
        sel = 0;
    int ix = r->x + 1, iy = r->y + 1;
    int iw = r->w - 2, ih = r->h - 3; /* last row: the footer hint */
    int cols = iw >= 76 ? 2 : 1;
    int rows = (nsec + cols - 1) / cols;

    for (int i = 0; i < nsec; i++) {
        int col = i % cols, row = i / cols;
        int cw = iw / cols;
        int sx = ix + col * cw;
        int sw = (col == cols - 1) ? iw - (cols - 1) * cw : cw;
        int sy = iy + row * (ih / rows);
        int sh = (row == rows - 1) ? ih - (rows - 1) * (ih / rows)
                                   : ih / rows;
        if (sw < 14 || sh < 2)
            continue; /* too tiny: skip rather than cram */
        rect_t sr = { sx, sy, sw, sh };
        ui_box(n, &sr, SECTIONS[i].title, i == sel);
        tgt_register(sr.x, sr.y, sr.w, sr.h, TGT(TGT_PANEL_TELEM, i));
        SECTIONS[i].draw(n, &sr);
    }

    ui_putln(n, r->x + 2, r->y + r->h - 2, r->w - 4,
             " j/k select · enter detail · 1-7 jump · t close · q quit · read-only",
             pal->muted, false);
}

/* ---- input: grid navigation + detail pages ---------------------------- */

/* open (or switch to) a section's detail page. A fresh live snapshot
 * keeps the info lines honest: PPT/EPP/boost/health are live-only
 * reads that the 250 ms poll never touches. */
static void telem_detail_open(int idx)
{
    int n = TELEM_SEC_N;
    if (n == 0)
        return;
    idx = ((idx % n) + n) % n;
    if (g_ui.telem_detail != idx)
        hw_refresh_live(g_ui.hw);
    g_ui.telem_detail = idx;
    g_ui.telem_sel = idx; /* grid cursor follows: esc lands you there */
}

void panel_telem_full_key(uint32_t key)
{
    int n = TELEM_SEC_N;
    if (n == 0)
        return;

    if (key >= '1' && (int)key < '1' + n) {
        telem_detail_open((int)(key - '1'));
        return;
    }

    if (g_ui.telem_detail >= 0) {
        switch (key) {
        case 'h':
        case NCKEY_LEFT:
            telem_detail_open(g_ui.telem_detail - 1);
            return;
        case 'l':
        case NCKEY_RIGHT:
            telem_detail_open(g_ui.telem_detail + 1);
            return;
        case NCKEY_ESC:
        case NCKEY_BACKSPACE:
        case 127:
        case 8:
            g_ui.telem_detail = -1; /* back to the grid */
            return;
        default:
            return; /* read-only page: everything else is swallowed */
        }
    }

    /* grid navigation is spatial, matching the boxes on screen: the
     * same 2-column/1-column rule the draw pass uses. Left/right
     * step inside a row, up/down step between rows; edges clamp —
     * no wraparound, "down" from the bottom row stays put */
    if (g_ui.telem_sel < 0 || g_ui.telem_sel >= n)
        g_ui.telem_sel = 0;
    int cols = (g_ui.rc_overlay.w - 2) >= 76 ? 2 : 1;
    int sel = g_ui.telem_sel;
    int rows_n = (n + cols - 1) / cols;
    int col = sel % cols, row = sel / cols;

    switch (key) {
    case 'j':
    case NCKEY_DOWN:
        if (row + 1 < rows_n && sel + cols < n)
            g_ui.telem_sel = sel + cols;
        return;
    case 'k':
    case NCKEY_UP:
        if (row > 0)
            g_ui.telem_sel = sel - cols;
        return;
    case 'l':
    case NCKEY_RIGHT:
        if (col + 1 < cols && sel + 1 < n)
            g_ui.telem_sel = sel + 1;
        return;
    case 'h':
    case NCKEY_LEFT:
        if (col > 0)
            g_ui.telem_sel = sel - 1;
        return;
    case NCKEY_ENTER:
    case '\r':
    case '\n':
    case ' ':
        telem_detail_open(g_ui.telem_sel);
        return;
    case NCKEY_ESC:
        g_ui.telem_full = false; /* grid esc closes the overlay */
        return;
    default:
        return;
    }
}

/* click a section box: first click selects, second opens the detail
 * (the presets-modal convention); the detail page has no targets yet */
void panel_telem_full_act(int id)
{
    if (g_ui.telem_detail >= 0 || id < 0 || id >= TELEM_SEC_N)
        return;
    if (g_ui.telem_sel == id)
        telem_detail_open(id);
    else
        g_ui.telem_sel = id;
}

/* ---- sampling -------------------------------------------------------- */

/* Called from the poll loop after each fast refresh (never a write).
 * Disk and network throughput are deltas between accepted ticks, so
 * the rates stay honest even when the tick gap stretches past 250 ms. */
void telem_poll(hw_state_t *hw, long now_ms)
{
    static long prev_ms = -1;
    static long long prev_rd = -1, prev_wr = -1;
    static long long prev_rx = -1, prev_tx = -1;

    if (!telem_tick(now_ms))
        return;

    double rd_bs = -1, wr_bs = -1;
    if (hw->disk_rd_sectors >= 0 && prev_rd >= 0 && prev_ms > 0 &&
        now_ms > prev_ms) {
        double dt = (double)(now_ms - prev_ms) / 1000.0;
        long long d = hw->disk_rd_sectors - prev_rd;
        if (d >= 0)
            rd_bs = (double)d * 512.0 / dt;
        d = hw->disk_wr_sectors - prev_wr;
        if (d >= 0)
            wr_bs = (double)d * 512.0 / dt;
    }
    prev_rd = hw->disk_rd_sectors;
    prev_wr = hw->disk_wr_sectors;

    /* same delta rule over the summed interface counters; a counter
     * reset (interface gone) reads as one unknown tick, not a spike */
    double rx_bs = -1, tx_bs = -1;
    if (hw->net_rx_tot >= 0 && prev_rx >= 0 && prev_ms > 0 &&
        now_ms > prev_ms) {
        double dt = (double)(now_ms - prev_ms) / 1000.0;
        long long d = hw->net_rx_tot - prev_rx;
        if (d >= 0)
            rx_bs = (double)d / dt;
        d = hw->net_tx_tot - prev_tx;
        if (d >= 0)
            tx_bs = (double)d / dt;
    }
    prev_rx = hw->net_rx_tot;
    prev_tx = hw->net_tx_tot;
    prev_ms = now_ms;

    telem_push(TS_CPU_TEMP, hw->cpu_temp);
    telem_push(TS_CPU_MHZ, hw->cpu_mhz_cur);
    telem_push(TS_CPU_LOAD, hw->load1);
    telem_push(TS_GPU_TEMP, hw->gpu_temp);
    telem_push(TS_GPU_MHZ, hw->gpu_mhz);
    telem_push(TS_GPU_MW, hw->gpu_mw);
    telem_push(TS_GPU_UTIL, hw->gpu_util);
    telem_push(TS_IGPU_TEMP, hw->igpu_temp);
    telem_push(TS_IGPU_MHZ, hw->igpu_mhz);
    telem_push(TS_IGPU_MW, hw->igpu_mw);
    telem_push(TS_FAN_CPU, hw->rpm_cpu);
    telem_push(TS_FAN_GPU, hw->rpm_gpu);
    telem_push(TS_MEM_USED, hw->mem_used_mb);
    telem_push(TS_DISK_RD, rd_bs);
    telem_push(TS_DISK_WR, wr_bs);
    telem_push(TS_BAT_MW, hw->bat_mw_known ? (hw->bat_mw < 0 ? -hw->bat_mw
                                                             : hw->bat_mw)
                                           : -1);
    telem_push(TS_BAT_PCT, hw->bat_pct);
    telem_push(TS_PD_MW, hw->pd_in_mw > 0 ? hw->pd_in_mw : -1);
    telem_push(TS_NET_RX, rx_bs);
    telem_push(TS_NET_TX, tx_bs);
}
