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
 * Two modes: the section grid (j/k cursor, enter/1-8 opens) and one
 * section's detail page — a numeric now/min/avg/max table over the same
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

typedef enum { U_TEMP, U_MHZ, U_W, U_PCT, U_LOAD, U_MBPS, U_MB, U_RPM } unit_t;

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
        case U_RPM:  snprintf(out, n, "%.0f rpm", v); break;
        }
    }
}

/* glances-style threshold zones (OK → CAREFUL → WARNING → CRITICAL):
 * the value AND the curve take the zone colour, so the screen shouts
 * when something is wrong. Fixed palette-independent warning colours. */
enum { ZN_NONE, ZN_TEMP, ZN_UTIL, ZN_CHARGE };
static uint32_t zone_color(int zn, double v, const palette_t *pal)
{
    switch (zn) {
    case ZN_TEMP: /* °C: 70/85/95 steps */
        return v >= 95 ? 0xe14b4bu : v >= 85 ? 0xd9704au
               : v >= 70 ? 0xd8b04cu : pal->accent;
    case ZN_UTIL: /* %: 70/90/98 */
        return v >= 98 ? 0xe14b4bu : v >= 90 ? 0xd9704au
               : v >= 70 ? 0xd8b04cu : pal->accent;
    case ZN_CHARGE: /* inverted: low battery is the alarm */
        return v < 20 ? 0xe14b4bu : v < 40 ? 0xd8b04cu : pal->accent;
    default:
        return pal->accent;
    }
}

typedef struct {
    const char *label;
    unit_t u;
    int sidx;
    const char *desc;    /* what the series measures, drawn under the
                            chart (detail) / over the mini (grid) */
    int zone;            /* ZN_*: threshold colouring of value+curve */
    int sidx2;           /* optional 2nd series in the SAME chart
                            (btop NET convention: rx/tx, read/write) */
    const char *label2;  /* its label (label row, stats, grid rows) */
} chartdef_t;

/* the series each section charts — one list per section, shared by
 * its grid box and its detail page so the two never drift apart.
 * Every entry fully initialised (-Wextra): ZN_NONE, 0, NULL. */
static const chartdef_t CD_CPU[] = {
    { "temp", U_TEMP, TS_CPU_TEMP,
      "Tctl — CPU die temperature (hottest sensor)", ZN_TEMP, -1, NULL },
    { "clock", U_MHZ, TS_CPU_MHZ,
      "thread-0 live frequency (sampled cpu)", ZN_NONE, -1, NULL },
    { "load", U_LOAD, TS_CPU_LOAD,
      "1-min load average (runnable tasks)", ZN_NONE, -1, NULL },
};
static const chartdef_t CD_GPU[] = {
    { "temp", U_TEMP, TS_GPU_TEMP, "dGPU temperature", ZN_TEMP, -1, NULL },
    { "power", U_W, TS_GPU_MW, "dGPU board power draw", ZN_NONE, -1, NULL },
    { "clock", U_MHZ, TS_GPU_MHZ, "graphics clock (current)",
      ZN_NONE, -1, NULL },
    { "util", U_PCT, TS_GPU_UTIL, "GPU utilisation (sm activity)",
      ZN_UTIL, -1, NULL },
};
static const chartdef_t CD_IGPU[] = {
    { "temp", U_TEMP, TS_IGPU_TEMP, "iGPU edge temperature",
      ZN_TEMP, -1, NULL },
    /* amdgpu power1_input is the SoC PPT: the WHOLE APU package
     * (CPU cores + iGPU + fabric), not the iGPU rail alone */
    { "APU PPT", U_W, TS_IGPU_MW,
      "whole-APU package power: CPU + iGPU", ZN_NONE, -1, NULL },
    { "clock", U_MHZ, TS_IGPU_MHZ, "iGPU graphics clock",
      ZN_NONE, -1, NULL },
};
static const chartdef_t CD_RAM[] = {
    { "used", U_MB, TS_MEM_USED,
      "memory in use (total minus available)", ZN_NONE, -1, NULL },
};
static const chartdef_t CD_DISK[] = {
    { "read", U_MBPS, TS_DISK_RD, "NVMe read/write throughput (whole disks)",
      ZN_NONE, TS_DISK_WR, "write" },
};
static const chartdef_t CD_NET[] = {
    { "rx", U_MBPS, TS_NET_RX, "network rx/tx throughput (counted ifaces)",
      ZN_NONE, TS_NET_TX, "tx" },
};
static const chartdef_t CD_FAN[] = {
    { "cpu", U_RPM, TS_FAN_CPU, "CPU / GPU fan speeds (rpm)",
      ZN_NONE, TS_FAN_GPU, "gpu" },
};
static const chartdef_t CD_POWER[] = {
    { "draw", U_W, TS_BAT_MW, "battery power flow (discharge watts)",
      ZN_NONE, -1, NULL },
    { "charge", U_PCT, TS_BAT_PCT, "battery charge level",
      ZN_CHARGE, -1, NULL },
    { "usb-c", U_W, TS_PD_MW,
      "USB-C PD input power (-- when unplugged)", ZN_NONE, -1, NULL },
};
#define CD_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* multi-row wash chart (btop-style) with a WASH fill: the area
 * under the curve is a solid background color wash (no glyphs — the
 * fill has zero texture), and the curve itself is an eighth-block
 * edge (▁▂▃▄▅▆▇█) painted bright over the same wash so it rises
 * smoothly out of the area. rows*8 vertical levels; each column
 * keeps its window slice's PEAK so a spike between columns still
 * shows; the newest sample carries a bright dot marker. */
static int utf_cells(const char *s)
{
    int c = 0;
    for (const char *p = s; *p; p++)
        if ((*p & 0xC0) != 0x80)
            c++;
    return c;
}

/* unit-less number for the min/avg/max columns (the unit rides on
 * the "now" value); MBPS rows rescale per-row in stat_row */
static void fmt_num(char *out, size_t n, unit_t u, double v)
{
    if (v < 0) {
        snprintf(out, n, "--");
        return;
    }
    switch (u) {
    case U_TEMP: snprintf(out, n, "%.1f", v); break;
    case U_MHZ:  snprintf(out, n, "%.0f", v); break;
    case U_W:    snprintf(out, n, "%.2f", v); break;
    case U_PCT:  snprintf(out, n, "%.0f", v); break;
    case U_LOAD: snprintf(out, n, "%.2f", v); break;
    case U_MBPS: snprintf(out, n, "%.0f", v / 1024.0); break; /* kB/s */
    case U_MB:   snprintf(out, n, "%.0f", v); break;
    case U_RPM:  snprintf(out, n, "%.0f", v); break;
    }
}

/* one numeric row: label - now (unit, zone colour) - min avg max */
static void stat_row(struct ncplane *n, int x, int y, int w,
                     const chartdef_t *cd, int sidx, const char *lab)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    double last = telem_last(sidx), mn, mx;
    telem_minmax(sidx, &mn, &mx);
    double av = telem_avg(sidx);
    char now[40], a[24], b[24], c2[24], num[160], labn[24];
    fmt_val(now, sizeof now, cd->u, last);
    if (cd->u == U_MBPS && mx >= 1024.0 * 1024.0) { /* row scale MB/s */
        snprintf(a, sizeof a, "%.1f", mn / 1048576.0);
        snprintf(b, sizeof b, "%.1f", av / 1048576.0);
        snprintf(c2, sizeof c2, "%.1f", mx / 1048576.0);
    } else {
        fmt_num(a, sizeof a, cd->u, mn);
        fmt_num(b, sizeof b, cd->u, av);
        fmt_num(c2, sizeof c2, cd->u, mx);
    }
    snprintf(labn, sizeof labn, " %-10s", lab);
    ui_putln(n, x, y, 12, labn, pal->text, false);
    snprintf(num, sizeof num, "%14s %10s %10s %10s", now, a, b, c2);
    ui_putln(n, x + 11, y, w - 11, num,
             zone_color(cd->zone, last, pal), false);
}

/* detail page: a plain NUMERIC table (user request — no charts):
 * one row per series (pairs two), now/min/avg/max over the 60 s
 * ring, the live info lines, then the per-series desc lines. */
static void detail_table(struct ncplane *n, const rect_t *r,
                         const chartdef_t *cd, int ncd,
                         const char *const *info, int ninfo)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int x = r->x + 2, w = r->w - 4;
    int avail = r->h - 2;
    if (avail < 1)
        return;
    int y = r->y + 1;
    char hdr[96];
    snprintf(hdr, sizeof hdr, "%11s%14s%10s%10s%10s",
             "metric", "now", "min", "avg", "max");
    ui_putln(n, x, y, w, hdr, pal->muted, false);
    y++;
    int used = 1;
    for (int i = 0; i < ncd; i++) {
        int halves = cd[i].sidx2 >= 0 ? 2 : 1;
        for (int h = 0; h < halves && used < avail; h++, used++, y++)
            stat_row(n, x, y, w, &cd[i], h ? cd[i].sidx2 : cd[i].sidx,
                     h ? cd[i].label2 : cd[i].label);
    }
    for (int i = 0; i < ninfo && used < avail; i++, used++, y++)
        info_line(n, x, y, w, info[i]);
    for (int i = 0; i < ncd && used < avail; i++, used++, y++) {
        char d[128];
        if (cd[i].sidx2 >= 0)
            snprintf(d, sizeof d, " %s/%s — %s", cd[i].label,
                     cd[i].label2, cd[i].desc);
        else
            snprintf(d, sizeof d, " %s — %s", cd[i].label, cd[i].desc);
        info_line(n, x, y, w, d);
    }
}

/* grid-box content: NUMBERS ONLY (user request) — label left, value
 * RIGHT-ALIGNED in its zone colour, one row per series (pairs get
 * two), then the info line. */
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
    int used = 0;
    for (int i = 0; i < ncd; i++) {
        int halves = cd[i].sidx2 >= 0 ? 2 : 1;
        for (int h = 0; h < halves && used < avail; h++, used++, y++) {
            const char *lab = h ? cd[i].label2 : cd[i].label;
            int sidx = h ? cd[i].sidx2 : cd[i].sidx;
            double last = telem_last(sidx);
            char cur[40], labn[24];
            fmt_val(cur, sizeof cur, cd[i].u, last);
            int vc = utf_cells(cur);
            snprintf(labn, sizeof labn, " %-9s", lab);
            ui_putln(n, x, y, 11, labn, pal->text, false);
            ui_putln(n, x + w - vc, y, vc + 1, cur,
                     zone_color(cd[i].zone, last, pal), false);
        }
    }
    for (int i = 0; i < ninfo && i < avail - used; i++, y++)
        info_line(n, x, y, w, info[i]);
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

static void sec_fan(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char line[128];
    int len = snprintf(line, sizeof line, " custom curve cpu %s · gpu %s",
                       hw->fan_cpu_on ? "on" : "off",
                       hw->fan_gpu_on ? "on" : "off");
    if (len > 0 && (size_t)len < sizeof line && hw->fan_staged)
        snprintf(line + len, sizeof line - len, " · staged");
    const char *info[1] = { line };
    grid_rows(n, r, CD_FAN, CD_COUNT(CD_FAN), info, 1);
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
    detail_table(n, r, CD_CPU, CD_COUNT(CD_CPU), 
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
        detail_table(n, r, CD_GPU, CD_COUNT(CD_GPU), 
                     info, 3);
        return;
    }
    const char *info[2] = { l0, l1 };
    detail_table(n, r, CD_GPU, CD_COUNT(CD_GPU), 
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
    detail_table(n, r, CD_IGPU, CD_COUNT(CD_IGPU), 
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
        detail_table(n, r, CD_RAM, CD_COUNT(CD_RAM), 
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
        int cnt = 0;
        for (int i = 0; i < hw->ram_temp_n; i++) {
            if (!(hw->ram_alarm & (1 << i)))
                continue;
            int w = snprintf(p, left, "%s%d", cnt ? "," : "", i + 1);
            if (w < 0 || (size_t)w >= left)
                break;
            p += w;
            left -= (size_t)w;
            cnt++;
        }
    }
    snprintf(l2, sizeof l2, "%s%s%s", lim,
             hw->ram_alarm ? " · over max: module " : "",
             hw->ram_alarm ? mod : "");
    const char *info[3] = { l0, l1, l2 };
    detail_table(n, r, CD_RAM, CD_COUNT(CD_RAM), 
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
    detail_table(n, r, CD_DISK, CD_COUNT(CD_DISK), 
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
    detail_table(n, r, CD_NET, CD_COUNT(CD_NET), 
                 info, ni);
}

static void detail_fan(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char l0[128], l1[96];
    int len = snprintf(l0, sizeof l0, " custom curve: cpu %s · gpu %s",
                       hw->fan_cpu_on ? "on" : "off",
                       hw->fan_gpu_on ? "on" : "off");
    if (len > 0 && (size_t)len < sizeof l0 && hw->fan_staged)
        snprintf(l0 + len, sizeof l0 - len, " · edits staged (Write pending)");
    snprintf(l1, sizeof l1, " curves are edited in the FAN workspace view "
                            "(this screen is read-only)");
    const char *info[2] = { l0, l1 };
    detail_table(n, r, CD_FAN, CD_COUNT(CD_FAN), 
                 info, 2);
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
    detail_table(n, r, CD_POWER, hw->pd_n > 0 ? CD_COUNT(CD_POWER) : 2,
                 info, hw->pd_n > 0 ? 3 : 2);
}

/* ---- the registry: order here is order on screen -------------------- */

static const telem_section_t SECTIONS[] = {
    { "CPU", sec_cpu, detail_cpu },
    { "GPU", sec_gpu, detail_gpu },
    { "iGPU", sec_igpu, detail_igpu },
    { "RAM", sec_ram, detail_ram },
    { "STORAGE", sec_disk, detail_disk },
    { "NET", sec_net, detail_net },
    { "FAN", sec_fan, detail_fan },
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
                 " esc grid · h/l section · 1-8 jump · t close · q quit · read-only",
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
        /* title carries the digit shortcut (btop convention) */
        char t[40];
        snprintf(t, sizeof t, "%s %d", SECTIONS[i].title, i + 1);
        ui_box(n, &sr, t, i == sel);
        tgt_register(sr.x, sr.y, sr.w, sr.h, TGT(TGT_PANEL_TELEM, i));
        SECTIONS[i].draw(n, &sr);
    }

    ui_putln(n, r->x + 2, r->y + r->h - 2, r->w - 4,
             " j/k select · enter detail · 1-8 jump · t close · q quit · read-only",
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
