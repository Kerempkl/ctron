#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ui_internal.h"
#include "../hw.h"
#include "../telem.h"
#include "../util.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* Fullscreen TELEMETRY ('t'): read-only rolling view over the telem
 * rings. Sections are a REGISTRY — adding, removing or moving one is
 * an edit to SECTIONS[] below; nothing else in the tree changes. The
 * screen never writes hardware; sampling happens in telem_poll(),
 * which ui.c calls after every fast refresh.
 *
 * Two modes: the section grid (j/k cursor, enter/1-8 opens) and one
 * section's detail page — a spaced numeric report (now/min/avg/max,
 * bold zone-coloured "now", separators around the live + legend
 * blocks; packed layout on short terminals) over the same rings. */

typedef void (*telem_sec_fn)(struct ncplane *n, const rect_t *r);

typedef struct {
    const char *title;
    telem_sec_fn draw;    /* grid box */
    telem_sec_fn detail;  /* full page inside the overlay */
} telem_section_t;

#define TELEM_SEC_N ((int)(sizeof SECTIONS / sizeof SECTIONS[0]))

/* mouse-target id of the footer reset button (section boxes use 0..7) */
#define TELEM_TGT_RESET 0x100

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

typedef enum { U_TEMP, U_MHZ, U_W, U_PCT, U_LOAD, U_MBPS, U_MB, U_RPM,
               U_PSI } unit_t;

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
        /* pressure sits below 1 % most of the time — the decimals
         * ARE the signal */
        case U_PSI:  snprintf(out, n, "%.2f %%", v); break;
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
    /* mirrored from the iGPU section: the same ring, no second
     * storage — watching a CPU test wants the package power beside */
    { "APU PPT", U_W, TS_IGPU_MW,
     "whole-APU package power: CPU + iGPU", ZN_NONE, -1, NULL },
    /* the per-core table below is a NOW snapshot with no stats by
     * design (16 cores x min/avg/max x 2 modes would bury the page);
     * the time dimension of "how busy do the cores get" lives here,
     * in one ordinary series with the usual window/since stats */
    { "busiest", U_PCT, TS_BUSIEST_UTIL,
     "highest single-core utilisation (busiest thread)", ZN_UTIL, -1, NULL },
    /* mean beside the max: overall fill vs the hottest core; unknown
     * cores are skipped, so the priming poll reads -- not 0 */
    { "avg", U_PCT, TS_CPU_AVG_UTIL,
     "mean core utilisation (physical cores, SMT merged)", ZN_UTIL, -1, NULL },
    /* PSI: the stall signal utilisation cannot show — a runqueue full
     * of IO-waiters reads 0% busy. No invented alarm thresholds: the
     * number itself is the signal, colour would cry wolf at 1 % */
    { "psi", U_PSI, TS_PSI_CPU,
     "% of the last 10 s some task stalled on CPU (psi some)",
     ZN_NONE, -1, NULL },
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
    /* gfx ACTIVITY, not watts — the u32@28 confusion lesson of
     * 2026-10-29 stays spelled out where the value is shown */
    { "util", U_PCT, TS_IGPU_UTIL,
     "iGPU activity (gpu_busy_percent) — utilisation, NOT power",
     ZN_UTIL, -1, NULL },
};
static const chartdef_t CD_RAM[] = {
    { "used", U_MB, TS_MEM_USED,
     "memory in use (total minus available)", ZN_NONE, -1, NULL },
    /* hottest module: the detail page lists both, a series needs one
     * number — the hot side is the one worth watching */
    { "temp", U_TEMP, TS_RAM_TEMP,
     "hottest RAM module temperature (spd5118)", ZN_TEMP, -1, NULL },
    { "psi", U_PSI, TS_PSI_MEM,
     "% of the last 10 s some task stalled on memory (psi some)",
     ZN_NONE, -1, NULL },
};
static const chartdef_t CD_DISK[] = {
    { "read", U_MBPS, TS_DISK_RD, "NVMe read/write throughput (whole disks)",
      ZN_NONE, TS_DISK_WR, "write" },
    { "temp", U_TEMP, TS_NVME_TEMP,
     "hottest NVMe sensor across drives (composite + sensors 1/2)",
     ZN_TEMP, -1, NULL },
    { "psi", U_PSI, TS_PSI_IO,
     "% of the last 10 s some task stalled on IO (psi some)",
     ZN_NONE, -1, NULL },
};
static const chartdef_t CD_NET[] = {
    { "rx", U_MBPS, TS_NET_RX, "network rx/tx throughput (counted ifaces)",
      ZN_NONE, TS_NET_TX, "tx" },
    { "signal", U_PCT, TS_WIFI_LINK,
      "wifi link quality (percent of the driver's 0-70 scale)",
      ZN_CHARGE, -1, NULL },
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
    /* watts, not the stored mW — "8.11" beside "8.11 W" reads, raw
     * milliwatts next to a watt value did not */
    case U_W:    snprintf(out, n, "%.2f", v / 1000.0); break;
    case U_PCT:  snprintf(out, n, "%.0f", v); break;
    case U_LOAD: snprintf(out, n, "%.2f", v); break;
    case U_MBPS: snprintf(out, n, "%.0f", v / 1024.0); break; /* kB/s */
    case U_MB:   snprintf(out, n, "%.0f", v); break;
    case U_RPM:  snprintf(out, n, "%.0f", v); break;
    case U_PSI:  snprintf(out, n, "%.2f", v); break;
    }
}

/* column right-edges for the detail table: label zone left, then
 * now/min/avg/max spread evenly across the content width (the detail
 * page is nearly fullscreen — use it, a packed left table reads
 * "grid-sized"). Capped so ultrawide terminals do not detach the
 * columns from the label. */
static void dt_cols(int x, int w, int ce[4])
{
    int col = (w - 14) / 4;
    if (col > 40)
        col = 40;
    if (col < 12)
        col = 12;
    if (12 + 4 * col > w)
        col = (w - 12) / 4;
    if (col < 8)
        col = 8;
    for (int i = 0; i < 4; i++)
        ce[i] = x + 12 + (i + 1) * col;
}

/* one numeric row: upper-case label left, the unit-carrying "now"
 * BOLD in its zone colour, min/avg/max spread to the right */
static void stat_row(struct ncplane *n, int x, int y,
                     const chartdef_t *cd, int sidx, const char *lab,
                     const int ce[4])
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    double last = telem_last(sidx), mn = -1, mx = -1, av = -1;
    if (g_ui.telem_since)
        telem_since(sidx, &mn, &mx, &av);
    else {
        telem_minmax(sidx, &mn, &mx);
        av = telem_avg(sidx);
    }
    char now[40], a[24], b[24], c2[24], labn[16];
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
    int li = 0;
    for (const char *p = lab; p && *p && li < 10; p++)
        labn[li++] = (*p >= 'a' && *p <= 'z')
                         ? (char)(*p - 'a' + 'A') : *p; /* ASCII-only:
                         toupper() is locale-aware (tr_TR: i->İ) */
    labn[li] = '\0';
    ui_putln(n, x, y, 12, labn, pal->text, false);
    char *cols[4] = { now, a, b, c2 };
    for (int i = 0; i < 4; i++) {
        int vc = utf_cells(cols[i]);
        int cx = ce[i] - vc;
        if (cx < x + 12)
            cx = x + 12;
        ui_putln(n, cx, y, vc + 1, cols[i],
                 i == 0 ? zone_color(cd->zone, last, pal) : pal->text,
                 i == 0);
    }
}

/* "── tag ────..." across the content width: groups the live info
 * block and the legend block so mixed lines stop reading as data */
static void dt_sep(struct ncplane *n, int x, int y, int w, const char *tag)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    char s[512];
    int off = snprintf(s, sizeof s, "── %s ", tag);
    while (off > 0 && (size_t)off + 3 < sizeof s && utf_cells(s) < w - 1) {
        memcpy(s + off, "─", 3);
        off += 3;
        s[off] = '\0';
    }
    ui_putln(n, x, y, w, s, pal->muted, false);
}

/* detail page: a numeric now/min/avg/max report (no charts). Roomy
 * terminals get breathing space — a blank line after every series
 * (pairs stay together) and section separators around the live info
 * and legend blocks; short terminals fall back to the packed layout
 * and then to tail clipping as before. `tail` is an optional block
 * drawn between the live info and the legend (the CPU section's
 * per-core table); tail_min is the height below which it is skipped —
 * it also joins roomy_n, so on mid-size terminals spacing yields to
 * the table rather than the table dying. */
typedef int (*detail_tail_fn)(struct ncplane *n, int x, int y, int w,
                              int rows);

static void detail_table(struct ncplane *n, const rect_t *r,
                         const chartdef_t *cd, int ncd,
                         const char *const *info, int ninfo,
                         detail_tail_fn tail, int tail_min)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int x = r->x + 2, w = r->w - 4;
    int avail = r->h - 2;
    if (avail < 1 || w < 30)
        return;
    int ce[4];
    dt_cols(x, w, ce);

    int halves = 0;
    for (int i = 0; i < ncd; i++)
        halves += cd[i].sidx2 >= 0 ? 2 : 1;
    /* +1 for the stats-source line under the header */
    int roomy_n = 1 + 1 + 1 + halves + ncd + (ninfo > 0 ? 1 : 0) +
                  ninfo + 1 + (ncd > 0 ? 1 : 0) + ncd;
    if (tail)
        roomy_n += tail_min + 1;
    bool roomy = avail >= roomy_n;

    int y = r->y + 1;
    int used = 0;
    /* header aligned with the value columns */
    static const char *const hn[4] = { "NOW", "MIN", "AVG", "MAX" };
    ui_putln(n, x, y, 12, "METRIC", pal->muted, false);
    for (int i = 0; i < 4; i++)
        ui_putln(n, ce[i] - (int)strlen(hn[i]), y, 8, hn[i],
                 pal->muted, false);
    y++;
    used++;
    if (roomy) {
        /* self-describing stats source (the two modes differ in what
         * MIN/AVG/MAX cover — the measurement workflow needs both) */
        char mode[128];
        if (g_ui.telem_since)
            snprintf(mode, sizeof mode,
                     " stats: since reset — 's' back to the 60 s window");
        else
            snprintf(mode, sizeof mode,
                     " stats: 60 s window — 's' switches to since reset");
        info_line(n, x, y, w, mode);
        y++;
        used++;
    }
    if (roomy) {
        y++;
        used++;
    }
    for (int i = 0; i < ncd; i++) {
        int hh = cd[i].sidx2 >= 0 ? 2 : 1;
        for (int h = 0; h < hh && used < avail; h++, used++, y++)
            stat_row(n, x, y, &cd[i], h ? cd[i].sidx2 : cd[i].sidx,
                     h ? cd[i].label2 : cd[i].label, ce);
        if (roomy && used < avail) {
            y++;
            used++;
        }
    }
    if (ninfo > 0) {
        if (roomy && used < avail) {
            dt_sep(n, x, y, w, "live");
            y++;
            used++;
        }
        for (int i = 0; i < ninfo && used < avail; i++, used++, y++)
            info_line(n, x, y, w, info[i]);
    }
    if (tail) {
        if (roomy && used < avail) {
            y++;
            used++;
        }
        if (avail - used >= tail_min) {
            int u = tail(n, x, y, w, avail - used);
            y += u;
            used += u;
        }
    }
    if (ncd > 0) {
        if (roomy && used < avail) {
            y++;
            used++;
            if (used < avail) {
                dt_sep(n, x, y, w, "what these mean");
                y++;
                used++;
            }
        }
        for (int i = 0; i < ncd && used < avail; i++, used++, y++) {
            char d[128];
            if (cd[i].sidx2 >= 0) {
                char pair[24];
                snprintf(pair, sizeof pair, "%s/%s", cd[i].label,
                         cd[i].label2);
                snprintf(d, sizeof d, " %-10s %s", pair, cd[i].desc);
            } else {
                snprintf(d, sizeof d, " %-10s %s", cd[i].label, cd[i].desc);
            }
            info_line(n, x, y, w, d);
        }
    }
}

/* grid-box content: NUMBERS ONLY (user request) — label left, value
 * RIGHT-ALIGNED and BOLD in its zone colour, the window's min–max
 * range in muted beside it (now + spread at a glance), one row per
 * series (pairs get two), then the info line. */
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
            double mn, mx;
            telem_minmax(sidx, &mn, &mx);
            if (telem_count(sidx) > 1 && mx > mn) {
                char r0[20], r1[20], rng[44];
                fmt_num(r0, sizeof r0, cd[i].u, mn);
                fmt_num(r1, sizeof r1, cd[i].u, mx);
                snprintf(rng, sizeof rng, "%s-%s", r0, r1);
                int rc = utf_cells(rng);
                int rx = x + w - vc - 2 - rc;
                if (rx > x + 11)
                    ui_putln(n, rx, y, rc + 1, rng, pal->muted, false);
            }
            ui_putln(n, x + w - vc, y, vc + 1, cur,
                     zone_color(cd[i].zone, last, pal), true);
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
    /* compact topology, same facts as the detail page: physical
     * cores / threads. cpu_n alone would relabel threads as cores
     * (16 "cores" on an 8-core SMT part — user report) */
    char topo[32];
    if (hw->core_n > 0 && hw->cpu_n > hw->core_n)
        snprintf(topo, sizeof topo, "%dc/%dt", hw->core_n, hw->cpu_n);
    else if (hw->core_n > 0)
        snprintf(topo, sizeof topo, "%dc", hw->core_n);
    else
        snprintf(topo, sizeof topo, "%dt", hw->cpu_n);
    snprintf(line, sizeof line,
             " fan %d/%d rpm · %s · EPP %s%s",
             hw->rpm_cpu, hw->rpm_gpu, topo, hw_epp_name(hw->epp), cc);
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
        /* df basis (matches the STORAGE detail rows): denominator is
         * used + available, rounding up — the bar and % agree with
         * `df`'s Use% */
        int basis = hw->fs_avail_mb > 0
                        ? hw->fs_used_mb + hw->fs_avail_mb
                        : hw->fs_total_mb;
        bar(b, sizeof b, hw->fs_used_mb, basis > 0 ? basis : 1, cells);
        int pct = basis > 0
                      ? (hw->fs_used_mb * 100 + basis - 1) / basis : 0;
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
    char line[176];
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
    if (hw->wifi_link_pct >= 0) {
        len = snprintf(line + w, sizeof line - w, " · wifi %d%%",
                       hw->wifi_link_pct);
        if (len > 0 && (size_t)len < sizeof line - w &&
            hw->wifi_level_dbm < 0) {
            w += (size_t)len;
            len = snprintf(line + w, sizeof line - w, " (%d dBm)",
                           hw->wifi_level_dbm);
        }
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

/* per-core NOW table (CPU detail): a snapshot grid of core id · util% ·
 * live clock, the SMT pair merged into one row per physical core.
 * Deliberately NO min/avg/max — the cells are "right now"; the time
 * dimension is the "busiest" series row in the table above, which
 * carries the usual window/since-reset stats. Returns lines drawn. */
static int core_now_table(struct ncplane *n, int x, int y, int w, int rows)
{
    hw_state_t *hw = g_ui.hw;
    if (hw->core_n <= 0 || rows < 3 || w < 34)
        return 0;
    const palette_t *pal = ui_palette(g_prefs.theme);
    dt_sep(n, x, y, w, "cores (now)");
    y++;
    rows--;
    /* cell = "c12 100% 4916"; 4 columns on normal detail widths, 2 on
     * narrow ones (stride 16 keeps id+util+clock from touching) */
    int cols = w >= 60 ? 4 : 2;
    int stride = w / cols;
    if (stride < 16) {
        cols = 2;
        stride = w / 2;
    }
    for (int c = 0; c < cols; c++) {
        int cx = x + c * stride;
        if (cx + 15 >= x + w)
            break;
        ui_putln(n, cx, y, 6, "CORE", pal->muted, false);
        ui_putln(n, cx + 6, y, 5, "UTIL", pal->muted, false);
        ui_putln(n, cx + 11, y, 6, "CLOCK", pal->muted, false);
    }
    y++;
    rows--;
    int drawn = 0;
    for (int i = 0; i < hw->core_n && rows > 0; i += cols, drawn++) {
        for (int c = 0; c < cols && i + c < hw->core_n; c++) {
            int cx = x + c * stride;
            if (cx + 15 >= x + w)
                break;
            int u = hw->core_util[i + c], m = hw->core_mhz[i + c];
            char id[16], uv[16], mv[16];
            snprintf(id, sizeof id, "c%d", hw->core_cpu[i + c]);
            if (u >= 0)
                snprintf(uv, sizeof uv, "%d%%", u);
            else
                snprintf(uv, sizeof uv, "--");
            if (m >= 0)
                snprintf(mv, sizeof mv, "%d", m);
            else
                snprintf(mv, sizeof mv, "--");
            ui_putln(n, cx, y, 6, id, pal->muted, false);
            int uc = utf_cells(uv);
            ui_putln(n, cx + 9 - uc, y, uc + 1, uv,
                     u >= 0 ? zone_color(ZN_UTIL, u, pal) : pal->muted,
                     u >= 0);
            int mc = utf_cells(mv);
            ui_putln(n, cx + 15 - mc, y, mc + 1, mv, pal->text, false);
        }
        y++;
        rows--;
    }
    return drawn + 2;
}

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

    /* per-core ceilings as a numeric summary: the old one-block-per-
     * thread bar read as one solid rectangle at real font sizes (a
     * single cell is too small to tell the 8 levels apart — user
     * report), so the shape carried no information. The hw max rides
     * along whenever the threads are not all sitting on it. */
    int clo = 0, chi = 0, at_max = 0, seen = 0;
    for (int i = 0; i < hw->cpu_n; i++) {
        int mhz = hw->cpu_mhz_core[hw->cpu_ids[i]];
        if (mhz <= 0)
            continue;
        seen++;
        if (clo == 0 || mhz < clo)
            clo = mhz;
        if (mhz > chi)
            chi = mhz;
        if (hw->cpu_mhz_max > 0 && mhz >= hw->cpu_mhz_max)
            at_max++;
    }
    if (seen == 0)
        snprintf(l2, sizeof l2, " per-core ceilings: --");
    else if (clo == chi && (hw->cpu_mhz_max <= 0 || clo == hw->cpu_mhz_max))
        snprintf(l2, sizeof l2, " per-core ceilings: all %d threads @ %d MHz",
                 seen, clo);
    else if (clo == chi)
        snprintf(l2, sizeof l2,
                 " per-core ceilings: all %d threads @ %d MHz (hw max %d)",
                 seen, clo, hw->cpu_mhz_max);
    else if (hw->cpu_mhz_max > 0)
        snprintf(l2, sizeof l2,
                 " per-core ceilings: %d-%d MHz · %d/%d @ hw max %d",
                 clo, chi, at_max, seen, hw->cpu_mhz_max);
    else
        snprintf(l2, sizeof l2, " per-core ceilings: %d-%d MHz", clo, chi);

    /* 5/15-min load ride along the polled 1-min series (same source
     * line, parsed together) */
    char l3[96];
    if (hw->load1 >= 0 && hw->load5 >= 0 && hw->load15 >= 0)
        snprintf(l3, sizeof l3, " load 1/5/15 min: %.2f / %.2f / %.2f",
                 hw->load1, hw->load5, hw->load15);
    else if (hw->load1 >= 0)
        snprintf(l3, sizeof l3, " load 1 min: %.2f", hw->load1);
    else
        snprintf(l3, sizeof l3, " load --");

    const char *info[4] = { l0, l1, l2, l3 };
    detail_table(n, r, CD_CPU, CD_COUNT(CD_CPU),
                 info, 4, core_now_table, 3);
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
                     info, 3, NULL, 0);
        return;
    }
    const char *info[2] = { l0, l1 };
    detail_table(n, r, CD_GPU, CD_COUNT(CD_GPU),
                 info, 2, NULL, 0);
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
    snprintf(l2, sizeof l2,
             " APU PPT = whole-package power (CPU + iGPU) — no honest"
             " per-side split on this silicon");
    const char *info[3] = { l0, l1, l2 };
    detail_table(n, r, CD_IGPU, CD_COUNT(CD_IGPU),
                 info, 3, NULL, 0);
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
                     info2, 2, NULL, 0);
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
                 info, 3, NULL, 0);
}

static void detail_disk(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char l0[128];
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

    /* per-mount usage (whole-disk mounts, one row per device — btrfs
     * subvolumes of one device are deduped in the read layer) */
    char ml[HW_MOUNT_MAX + 1][160];
    const char *info[1 + HW_MOUNT_MAX + 1];
    int ni = 0;
    info[ni++] = l0;
    for (int i = 0; i < hw->mnt_n && ni < (int)(sizeof info / sizeof info[0]);
         i++) {
        long tot = hw->mnt_total_mb[i], used = hw->mnt_used_mb[i];
        long avail = hw->mnt_avail_mb[i];
        /* df basis: root reserve counts as used, denominator is
         * used+bavail, and the % rounds UP — matches `df`'s Use% */
        long basis = (used + avail) > 0 ? used + avail : tot;
        int pct = basis > 0 ? (int)((used * 100 + basis - 1) / basis) : 0;
        char sz[48];
        if (tot >= 1024)
            snprintf(sz, sizeof sz, "%.0f/%.0f GB",
                     used / 1024.0, tot / 1024.0);
        else
            snprintf(sz, sizeof sz, "%ld/%ld MB", used, tot);
        snprintf(ml[i], sizeof ml[i], " %.52s · %s %.20s · %s · %d%%",
                 hw->mnt[i].point, hw->mnt[i].fs, hw->mnt[i].dev, sz, pct);
        info[ni++] = ml[i];
    }
    if (hw->mnt_dupes > 0 &&
        ni < (int)(sizeof info / sizeof info[0])) {
        snprintf(ml[HW_MOUNT_MAX], sizeof ml[HW_MOUNT_MAX],
                 " %d more mount%s of an already-listed device share its space",
                 hw->mnt_dupes, hw->mnt_dupes == 1 ? "" : "s");
        info[ni++] = ml[HW_MOUNT_MAX];
    }
    detail_table(n, r, CD_DISK, CD_COUNT(CD_DISK),
                 info, ni, NULL, 0);
}

static void detail_net(struct ncplane *n, const rect_t *r)
{
    hw_state_t *hw = g_ui.hw;
    char l[HW_NET_IF_MAX + 2][96];
    const char *info[HW_NET_IF_MAX + 2];
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
    if (hw->wifi_link_pct >= 0) {
        if (hw->wifi_level_dbm < 0)
            snprintf(l[ni], sizeof l[ni], " wifi %s: link %d%%",
                     hw->wifi_if, hw->wifi_link_pct);
        else
            snprintf(l[ni], sizeof l[ni], " wifi %s: link %d%% · %d dBm",
                     hw->wifi_if, hw->wifi_link_pct, hw->wifi_level_dbm);
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
                 info, ni, NULL, 0);
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
                 info, 2, NULL, 0);
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
                 info, hw->pd_n > 0 ? 3 : 2, NULL, 0);
}

/* 'r' key / footer button: clear every series ring AND the lifetime
 * "since" accumulators. min/avg/max then rebuild from a clean state —
 * old samples cannot colour a fresh measurement run (workflow: reset,
 * run the test — however long — read the stats, 's' switches the
 * table to "since reset"). "now" values refill on the next poll tick
 * (<= 250 ms). */
static long tmono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void telem_stats_reset(void)
{
    telem_reset();
    g_ui.telem_ack_ms = tmono_ms();
}

/* overlay footer: key hints left, a clickable "[r] reset stats" button
 * right (drawn only when the two cannot collide; the 'r' key works on
 * every width) */
static void telem_footer(struct ncplane *n, const rect_t *r, const char *hint)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int y = r->y + r->h - 2;
    ui_putln(n, r->x + 2, y, r->w - 4, hint, pal->muted, false);
    static const char btn[] = "[r] reset stats";
    int bw = (int)strlen(btn);
    int bx = r->x + r->w - 2 - bw;
    if (bx > r->x + 4 + utf_cells(hint)) {
        ui_putln(n, bx, y, bw + 1, btn, pal->accent, false);
        tgt_register(bx, y, bw, 1, TGT(TGT_PANEL_TELEM, TELEM_TGT_RESET));
    }
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
    int nsec = TELEM_SEC_N;
    int det = g_ui.telem_detail;
    if (det < 0 || det >= nsec)
        det = -1;

    char title[64];
    if (det < 0)
        snprintf(title, sizeof title, "TELEMETRY");
    else
        snprintf(title, sizeof title, "TELEMETRY · %s", SECTIONS[det].title);
    /* reset acknowledgement rides BESIDE the section name (time-based:
     * no draw-count clock, so a closed overlay can never carry a stale
     * flash into a reopen) */
    if (g_ui.telem_ack_ms > 0 &&
        tmono_ms() - g_ui.telem_ack_ms < 3000)
        snprintf(title + strlen(title), sizeof(title) - strlen(title),
                 " · cleared");
    ui_window(n, r, title);

    if (det >= 0) {
        rect_t ir = { r->x + 1, r->y + 1, r->w - 2, r->h - 3 };
        if (ir.w > 4 && ir.h > 2)
            SECTIONS[det].detail(n, &ir);
        telem_footer(n, r,
                     " esc grid · h/l section · 1-8 · r reset · s stats · t close · q quit");
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

    telem_footer(n, r,
                 " j/k select · enter detail · 1-8 · r reset · s stats · t close · q quit");
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

    /* works on the grid AND every detail page: the stats tables are
     * the thing being reset */
    if (key == 'r' || key == 'R') {
        telem_stats_reset();
        return;
    }

    /* stats source of the detail tables: the rolling 60 s window or
     * everything since the last reset (long measurement runs) */
    if (key == 's' || key == 'S') {
        g_ui.telem_since = !g_ui.telem_since;
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
 * (the presets-modal convention); the footer button resets the stats
 * on both the grid and the detail pages */
void panel_telem_full_act(int id)
{
    if (id == TELEM_TGT_RESET) {
        telem_stats_reset();
        return;
    }
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
    telem_push(TS_BUSIEST_UTIL, hw->busiest_util);
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
    telem_push(TS_WIFI_LINK, hw->wifi_link_pct);

    /* hottest-of readings: a series needs one number, the detail pages
     * list every module/drive beside it */
    int hot = -1;
    for (int i = 0; i < hw->ram_temp_n; i++)
        if (hw->ram_temp[i] > hot)
            hot = hw->ram_temp[i];
    telem_push(TS_RAM_TEMP, hot);
    hot = -1;
    for (int i = 0; i < hw->nvme_temp_n; i++) {
        if (hw->nvme_temp[i] > hot)
            hot = hw->nvme_temp[i];
        if (hw->nvme_sensor_max[i] > hot)
            hot = hw->nvme_sensor_max[i];
    }
    telem_push(TS_NVME_TEMP, hot);

    telem_push(TS_CPU_AVG_UTIL, hw_core_avg_util(hw->core_util, hw->core_n));
    telem_push(TS_IGPU_UTIL, hw->igpu_busy);
    telem_push(TS_SWAP_USED, hw->swap_used_mb);
    telem_push(TS_PSI_CPU, hw->psi_cpu);
    telem_push(TS_PSI_MEM, hw->psi_mem);
    telem_push(TS_PSI_IO, hw->psi_io);
}
