#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ui_internal.h"
#include "../util.h"

#include <stdio.h>
#include <time.h>

/* Always-on live strip: two telemetry lines + last log line. */

static void val_or_dash(char *out, size_t n, const char *fmt, int v)
{
    if (v < 0)
        snprintf(out, n, "--");
    else
        snprintf(out, n, fmt, v);
}

void panel_telemetry_draw(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    hw_state_t *hw = g_ui.hw;

    ui_box(n, r, "LIVE", g_ui.focus == FOC_TELEM);

    char l1[256], l2[256];
    char cpu_t[16], gpu_t[16], rpmc[16], rpmg[16], hz[16], pwr[24];
    val_or_dash(cpu_t, sizeof(cpu_t), "%d°C", hw->cpu_temp);
    val_or_dash(gpu_t, sizeof(gpu_t), "%d°C", hw->gpu_temp);
    val_or_dash(rpmc, sizeof(rpmc), "%d", hw->rpm_cpu);
    val_or_dash(rpmg, sizeof(rpmg), "%d", hw->rpm_gpu);
    if (hw->hz_cur > 0)
        snprintf(hz, sizeof(hz), "%d Hz", hw->hz_cur);
    else
        snprintf(hz, sizeof(hz), "-- Hz");

    snprintf(l1, sizeof(l1),
             " CPU %s   GPU %s   fan %s/%s rpm   %d MHz (max %d) ",
             cpu_t, gpu_t, rpmc, rpmg, hw->cpu_mhz_cur, hw->cpu_mhz_limit);
    hw_fmt_power(pwr, sizeof(pwr), hw->bat_mw_known, hw->bat_mw);
    snprintf(l2, sizeof(l2),
             " BAT %d%% %s %s%s   %s   %s",
             hw->bat_pct, hw->bat_status, pwr, hw->ac_online ? " ⚡AC" : "",
             hw_profile_name(hw->profile), hz);

    int x = r->x + 2, w = r->w - 4;
    if (w > 4) {
        ui_putln(n, x, r->y + 1, w, l1, pal->text, true);
        ui_putln(n, x, r->y + 2, w, l2, pal->text, false);
        if (atomic_load(&g_ui.apply_busy)) {
            /* live apply status replaces the log row: what/phase,
             * elapsed time, spinner — plus the honesty note that
             * reads are paused while the worker owns the write path */
            static const char spin[] = "|/-\\";
            static int frame;
            char ptxt[96];
            int pct;
            ut_progress_get(ptxt, sizeof(ptxt), &pct);
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            long el = ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
            el = (el - g_ui.apply_start_ms) / 100; /* tenths of a s */
            char line[160];
            if (pct >= 0)
                snprintf(line, sizeof(line),
                         "%c %.20s: %.60s · %d%% · %ld.%ld s · reads paused · q waits",
                         spin[frame++ % 4], g_ui.apply_what,
                         ptxt[0] ? ptxt : "working", pct, el / 10, el % 10);
            else
                snprintf(line, sizeof(line),
                         "%c %.20s: %.80s · %ld.%ld s · reads paused · q waits",
                         spin[frame++ % 4], g_ui.apply_what,
                         ptxt[0] ? ptxt : "working", el / 10, el % 10);
            ui_putln(n, x, r->y + 3, w, line, pal->accent, true);
        } else {
            const char *lg = ut_log_get(0);
            ui_putln(n, x, r->y + 3, w,
                     lg[0] ? lg : "q quit · 1-4 focus · esc settings · ? help",
                     pal->muted, false);
        }
    }
}
