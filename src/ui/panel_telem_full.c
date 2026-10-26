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
 * Two modes: the section grid (j/k cursor, enter/1-6 opens) and one
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

/* one "label  current-value  sparkline" row */
static void metric(struct ncplane *n, int x, int y, int w,
                   const char *label, const char *cur, int sidx)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    char spark[600];
    int cells = w - 24;
    if (cells < 6)
        cells = 6;
    if ((size_t)cells * 3 + 1 > sizeof spark)
        cells = (int)(sizeof spark - 1) / 3;
    telem_spark(sidx, spark, sizeof spark, cells);
    char line[700];
    snprintf(line, sizeof line, " %-8s %-12s %s", label, cur, spark);
    ui_putln(n, x, y, w, line, pal->text, false);
}

static void info_line(struct ncplane *n, int x, int y, int w, const char *s)
{
    ui_putln(n, x, y, w, s, ui_palette(g_prefs.theme)->muted, false);
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

/* multi-row block chart over the whole ring window, newest on the
 * right. Each column covers a slice of samples and keeps their max, so
 * a spike between two columns still shows. Half blocks double the
 * vertical resolution: 2*rows levels. */
#define CHART_COLS_MAX 256
static void chart(struct ncplane *n, int x, int y, int w, int rows, int sidx)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int count = telem_count(sidx);
    if (count < 1 || rows < 1)
        return;
    int cols = w - 2;
    if (cols < 4)
        cols = 4;
    if (cols > CHART_COLS_MAX)
        cols = CHART_COLS_MAX;

    double mn, mx;
    telem_range(sidx, &mn, &mx);
    if (mn == TELEM_AUTO && mx == TELEM_AUTO)
        telem_minmax(sidx, &mn, &mx);
    if (mx <= mn) { /* flat window: stand on the middle, like the spark */
        mn -= 1.0;
        mx += 1.0;
    }

    int h[CHART_COLS_MAX];
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
        if (peak < 0) {
            h[c] = -1; /* no sample in this column */
            continue;
        }
        double lvl = (peak - mn) / (mx - mn);
        if (lvl < 0)
            lvl = 0;
        if (lvl > 1)
            lvl = 1;
        h[c] = (int)(lvl * (2 * rows) + 0.5);
        if (h[c] > 2 * rows)
            h[c] = 2 * rows;
    }

    char buf[CHART_COLS_MAX * 3 + 4];
    for (int rr = 0; rr < rows; rr++) {
        int br = rows - 1 - rr; /* bottom-based row index */
        size_t wb = 0;
        for (int c = 0; c < cols && wb + 4 < sizeof buf; c++) {
            if (h[c] >= 2 * (br + 1)) {
                buf[wb++] = (char)0xE2; /* █ */
                buf[wb++] = (char)0x96;
                buf[wb++] = (char)0x88;
            } else if (h[c] == 2 * br + 1) {
                buf[wb++] = (char)0xE2; /* ▄ lower half */
                buf[wb++] = (char)0x96;
                buf[wb++] = (char)0x84;
            } else {
                buf[wb++] = ' ';
            }
        }
        buf[wb] = '\0';
        ui_putln(n, x + 1, y + rr, w, buf, pal->accent, false);
    }
}

typedef struct {
    const char *label;
    unit_t u;
    int sidx;
} chartdef_t;

/* one labelled block: value row, `rows` chart rows, min/max/last row */
static void chart_block(struct ncplane *n, int x, int y, int w, int rows,
                        const chartdef_t *cd)
{
    char v[40], a[40], b[40], c2[40], line[160];
    double last = telem_last(cd->sidx);
    fmt_val(v, sizeof v, cd->u, last);
    snprintf(line, sizeof line, " %-10s %s", cd->label, v);
    ui_putln(n, x, y, w, line, ui_palette(g_prefs.theme)->text, false);

    chart(n, x, y + 1, w, rows, cd->sidx);

    double mn, mx;
    telem_minmax(cd->sidx, &mn, &mx);
    fmt_val(a, sizeof a, cd->u, mn);
    fmt_val(b, sizeof b, cd->u, mx);
    fmt_val(c2, sizeof c2, cd->u, last);
    snprintf(line, sizeof line, " min %s · max %s · last %s", a, b, c2);
    info_line(n, x, y + 1 + rows, w, line);
}

/* stacked full-width chart blocks with trailing info lines. Charts
 * shrink first and then drop (newest-last) on short terminals; a
 * page that fits nothing shows the info lines alone. */
static void detail_charts(struct ncplane *n, const rect_t *r,
                          const chartdef_t *cd, int ncd,
                          const char *const *info, int ninfo)
{
    int x = r->x + 2, w = r->w - 4;
    int avail = r->h - 2;
    int nch = ncd;
    while (nch > 0 && (avail - ninfo) / nch - 2 < 2)
        nch--;
    if (nch <= 0) {
        for (int i = 0; i < ninfo && i < avail; i++)
            info_line(n, x, r->y + 1 + i, w, info[i]);
        return;
    }
    int per = (avail - ninfo) / nch - 2;
    if (per > 10)
        per = 10;
    int y = r->y + 1;
    for (int i = 0; i < nch; i++) {
        chart_block(n, x, y, w, per, &cd[i]);
        y += per + 2;
    }
    for (int i = 0; i < ninfo; i++)
        info_line(n, x, y + i, w, info[i]);
}

/* ---- sections (each draws inside its own boxed rect) ----------------- */

static void sec_cpu(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char v[32], line[160];
    int x = r->x + 2, w = r->w - 4;
    if (r->h < 2)
        return;
    fmt_int(v, sizeof v, "°C", hw->cpu_temp);
    metric(n, x, r->y + 1, w, "temp", v, TS_CPU_TEMP);
    if (r->h < 3)
        return;
    fmt_int(v, sizeof v, " MHz", hw->cpu_mhz_cur);
    metric(n, x, r->y + 2, w, "clock", v, TS_CPU_MHZ);
    if (r->h < 4)
        return;
    if (hw->load1 >= 0)
        snprintf(v, sizeof v, "%.2f", hw->load1);
    else
        snprintf(v, sizeof v, "--");
    metric(n, x, r->y + 3, w, "load", v, TS_CPU_LOAD);
    if (r->h < 5)
        return;
    snprintf(line, sizeof line, " fan %d/%d rpm · %d cores · EPP %s",
             hw->rpm_cpu, hw->rpm_gpu, hw->cpu_n, hw_epp_name(hw->epp));
    info_line(n, x, r->y + 4, w, line);
}

static void sec_gpu(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char v[32], line[160];
    int x = r->x + 2, w = r->w - 4;
    if (r->h < 2)
        return;
    fmt_int(v, sizeof v, "°C", hw->gpu_temp);
    metric(n, x, r->y + 1, w, "temp", v, TS_GPU_TEMP);
    if (r->h < 3)
        return;
    fmt_int(v, sizeof v, " MHz", hw->gpu_mhz);
    metric(n, x, r->y + 2, w, "clock", v, TS_GPU_MHZ);
    if (r->h < 4)
        return;
    if (hw->gpu_mw >= 0)
        snprintf(v, sizeof v, "%.2f W", hw->gpu_mw / 1000.0);
    else
        snprintf(v, sizeof v, "--");
    metric(n, x, r->y + 3, w, "power", v, TS_GPU_MW);
    if (r->h < 5)
        return;
    if (hw->gpu_vram_total_mb > 0)
        snprintf(line, sizeof line, " util %d%% · VRAM %d/%d MB · %d MHz max",
                 hw->gpu_util, hw->gpu_vram_used_mb, hw->gpu_vram_total_mb,
                 hw->gpu_mhz_max);
    else
        snprintf(line, sizeof line, " util %d%%", hw->gpu_util);
    info_line(n, x, r->y + 4, w, line);
}

static void sec_igpu(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char v[32], line[160];
    int x = r->x + 2, w = r->w - 4;
    if (r->h < 2)
        return;
    fmt_int(v, sizeof v, "°C", hw->igpu_temp);
    metric(n, x, r->y + 1, w, "temp", v, TS_IGPU_TEMP);
    if (r->h < 3)
        return;
    fmt_int(v, sizeof v, " MHz", hw->igpu_mhz);
    metric(n, x, r->y + 2, w, "clock", v, TS_IGPU_MHZ);
    if (r->h < 4)
        return;
    if (hw->igpu_mw >= 0)
        snprintf(line, sizeof line, " power %.2f W · amdgpu",
                 hw->igpu_mw / 1000.0);
    else
        snprintf(line, sizeof line, " power -- · amdgpu");
    info_line(n, x, r->y + 3, w, line);
}

static void sec_ram(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char v[32], line[160];
    int x = r->x + 2, w = r->w - 4;
    if (r->h < 2)
        return;
    if (hw->mem_total_mb > 0)
        snprintf(v, sizeof v, "%d/%d MB", hw->mem_used_mb, hw->mem_total_mb);
    else
        snprintf(v, sizeof v, "--");
    metric(n, x, r->y + 1, w, "used", v, TS_MEM_USED);
    if (r->h < 3)
        return;
    if (hw->ram_temp_n == 2)
        snprintf(line, sizeof line, " modules %d/%d °C · swap %d/%d MB",
                 hw->ram_temp[0], hw->ram_temp[1],
                 hw->swap_used_mb, hw->swap_total_mb);
    else if (hw->ram_temp_n == 1)
        snprintf(line, sizeof line, " module %d °C · swap %d/%d MB",
                 hw->ram_temp[0], hw->swap_used_mb, hw->swap_total_mb);
    else
        snprintf(line, sizeof line, " swap %d/%d MB",
                 hw->swap_used_mb, hw->swap_total_mb);
    info_line(n, x, r->y + 2, w, line);
}

static void sec_disk(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char v[32], line[200];
    int x = r->x + 2, w = r->w - 4;
    if (r->h < 2)
        return;
    fmt_bs(v, sizeof v, telem_last(TS_DISK_RD));
    metric(n, x, r->y + 1, w, "read", v, TS_DISK_RD);
    if (r->h < 3)
        return;
    fmt_bs(v, sizeof v, telem_last(TS_DISK_WR));
    metric(n, x, r->y + 2, w, "write", v, TS_DISK_WR);
    if (r->h < 4)
        return;
    char b[120];
    int cells = w - 40;
    if (cells < 8)
        cells = 8;
    if ((size_t)cells * 3 + 1 > sizeof b)
        cells = (int)(sizeof b - 1) / 3;
    if (hw->fs_total_mb > 0) {
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
    info_line(n, x, r->y + 3, w, line);
    (void)hw;
}

static void sec_power(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char v[32], line[160];
    int x = r->x + 2, w = r->w - 4;
    if (r->h < 2)
        return;
    if (hw->bat_mw_known)
        snprintf(v, sizeof v, "%.1f W", hw->bat_mw / 1000.0);
    else
        snprintf(v, sizeof v, "--");
    metric(n, x, r->y + 1, w, "draw", v, TS_BAT_MW);
    if (r->h < 3)
        return;
    fmt_int(v, sizeof v, "%", hw->bat_pct);
    metric(n, x, r->y + 2, w, "charge", v, TS_BAT_PCT);
    if (r->h < 4)
        return;
    snprintf(line, sizeof line, " %s · %s · charge limit %d%%",
             hw->bat_status, hw->ac_online ? "on AC" : "on battery",
             hw->bat_limit);
    info_line(n, x, r->y + 3, w, line);
}

/* ---- section detail pages (full page inside the overlay) ------------- */

static void detail_cpu(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    static const chartdef_t cd[] = {
        { "temp", U_TEMP, TS_CPU_TEMP },
        { "clock", U_MHZ, TS_CPU_MHZ },
        { "load", U_LOAD, TS_CPU_LOAD },
    };
    char l0[128], l1[160], l2[192];

    snprintf(l0, sizeof l0, " %d threads · %d cores · %d CCD · EPP %s · boost %s",
             hw->cpu_n, hw->core_n, hw->ccd_n, hw_epp_name(hw->epp),
             hw->cpu_boost ? "on" : "off");
    if (hw->ppt_spl > 0 || hw->ppt_sppt > 0 || hw->ppt_fppt > 0)
        snprintf(l1, sizeof l1, " PPT %d / %d / %d W · fans %d / %d rpm",
                 hw->ppt_spl, hw->ppt_sppt, hw->ppt_fppt,
                 hw->rpm_cpu, hw->rpm_gpu);
    else
        snprintf(l1, sizeof l1, " PPT -- · fans %d / %d rpm",
                 hw->rpm_cpu, hw->rpm_gpu);

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
    detail_charts(n, r, cd, 3, info, 3);
}

static void detail_gpu(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    static const chartdef_t cd[] = {
        { "temp", U_TEMP, TS_GPU_TEMP },
        { "power", U_W, TS_GPU_MW },
        { "clock", U_MHZ, TS_GPU_MHZ },
        { "util", U_PCT, TS_GPU_UTIL },
    };
    char l0[160], l1[128];
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
    const char *info[2] = { l0, l1 };
    detail_charts(n, r, cd, 4, info, 2);
}

static void detail_igpu(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    static const chartdef_t cd[] = {
        { "temp", U_TEMP, TS_IGPU_TEMP },
        { "power", U_W, TS_IGPU_MW },
        { "clock", U_MHZ, TS_IGPU_MHZ },
    };
    char vg[24], vn[24], l0[96];
    fmt_int(vg, sizeof vg, " mV", hw->igpu_vddgfx_mv);
    fmt_int(vn, sizeof vn, " mV", hw->igpu_vddnb_mv);
    snprintf(l0, sizeof l0, " vddgfx %s · vddnb %s · amdgpu hwmon", vg, vn);
    const char *info[1] = { l0 };
    detail_charts(n, r, cd, 3, info, 1);
}

static void detail_ram(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    static const chartdef_t cd[] = {
        { "used", U_MB, TS_MEM_USED },
    };
    char l0[128], l1[96];
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
    else
        snprintf(l1, sizeof l1, " no spd5118 temp sensors");
    const char *info[2] = { l0, l1 };
    detail_charts(n, r, cd, 1, info, 2);
}

static void detail_disk(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    static const chartdef_t cd[] = {
        { "read", U_MBPS, TS_DISK_RD },
        { "write", U_MBPS, TS_DISK_WR },
    };
    char l0[96], l1[200];
    if (hw->nvme_temp_n == 2)
        snprintf(l0, sizeof l0, " NVMe %d / %d °C",
                 hw->nvme_temp[0], hw->nvme_temp[1]);
    else if (hw->nvme_temp_n == 1)
        snprintf(l0, sizeof l0, " NVMe %d °C", hw->nvme_temp[0]);
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
    detail_charts(n, r, cd, 2, info, 2);
}

static void detail_power(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    static const chartdef_t cd[] = {
        { "draw", U_W, TS_BAT_MW },
        { "charge", U_PCT, TS_BAT_PCT },
    };
    char l0[128], l1[96];
    snprintf(l0, sizeof l0, " %s · %s · charge limit %d%%",
             hw->bat_status, hw->ac_online ? "on AC" : "on battery",
             hw->bat_limit);
    if (hw->bat_health >= 0)
        snprintf(l1, sizeof l1, " battery health %d%% of design capacity",
                 hw->bat_health);
    else
        snprintf(l1, sizeof l1, " battery health -- (no full/design read)");
    const char *info[2] = { l0, l1 };
    detail_charts(n, r, cd, 2, info, 2);
}

/* ---- the registry: order here is order on screen -------------------- */

static const telem_section_t SECTIONS[] = {
    { "CPU", sec_cpu, detail_cpu },
    { "GPU", sec_gpu, detail_gpu },
    { "iGPU", sec_igpu, detail_igpu },
    { "RAM", sec_ram, detail_ram },
    { "STORAGE", sec_disk, detail_disk },
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
                 " esc grid · h/l section · 1-6 jump · t close · q quit · read-only",
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
             " j/k select · enter detail · 1-6 jump · t close · q quit · read-only",
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

    switch (key) {
    case 'j':
    case 'l':
    case NCKEY_DOWN:
    case NCKEY_RIGHT:
        g_ui.telem_sel = (g_ui.telem_sel + 1) % n;
        return;
    case 'k':
    case 'h':
    case NCKEY_UP:
    case NCKEY_LEFT:
        g_ui.telem_sel = (g_ui.telem_sel + n - 1) % n;
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
 * Disk throughput is a delta between accepted ticks, so the rate is
 * honest even when the tick gap stretches past 250 ms. */
void telem_poll(hw_state_t *hw, long now_ms)
{
    static long prev_ms = -1;
    static long long prev_rd = -1, prev_wr = -1;

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
}
