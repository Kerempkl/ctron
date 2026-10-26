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
 * which ui.c calls after every fast refresh. */

typedef void (*telem_sec_fn)(struct ncplane *n, const rect_t *r);

typedef struct {
    const char *title;
    telem_sec_fn draw;
} telem_section_t;

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

/* ---- the registry: order here is order on screen -------------------- */

static const telem_section_t SECTIONS[] = {
    { "CPU", sec_cpu },
    { "GPU", sec_gpu },
    { "iGPU", sec_igpu },
    { "RAM", sec_ram },
    { "STORAGE", sec_disk },
    { "POWER", sec_power },
};

void panel_telem_full_draw(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    ui_window(n, r, "TELEMETRY");

    int nsec = (int)(sizeof SECTIONS / sizeof SECTIONS[0]);
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
        ui_box(n, &sr, SECTIONS[i].title, false);
        SECTIONS[i].draw(n, &sr);
    }

    ui_putln(n, r->x + 2, r->y + r->h - 2, r->w - 4,
             " t/esc close · q quit · window ~60 s · sampling 250 ms · read-only",
             pal->muted, false);
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
