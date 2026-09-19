#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "display.h"
#include "../util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Hyprland backend via hyprctl. Ported from ctron v1 (working code on the
 * FA507NVR/NixOS machine).
 *
 * HANDOFF NOTES for the Hyprland maintainer (see README):
 *  - Query: `hyprctl monitors -j` (single-line JSON).
 *  - current mode: object "currentMode" -> width/height/refreshRate.
 *  - mode list: array "availableModes" as "WxH@RHz" strings. Older
 *    Hyprland builds omit availableModes; then modes_hz only reports the
 *    current refresh. Verify on the target build and extend if needed.
 *  - Apply: `hyprctl keyword monitor <name>,<W>x<H>@<R>,0x0,1`.
 *    The monitor scale/position is not round-tripped; if a non-1 scale
 *    or a multi-monitor layout is used, read "scale" and position from
 *    the JSON and re-emit them instead of the hardcoded ,0x0,1.
 */

#define HYPR_BUF 8192

static int hypr_query(char *buf, size_t n)
{
    return ut_exec_raw("hyprctl monitors -j", buf, n);
}

/* Extract "key": value (number) that appears after `anchor`. */
static bool hypr_num_after(const char *text, const char *anchor,
                           const char *key, double *out)
{
    const char *p = strstr(text, anchor);
    if (!p)
        return false;
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *k = strstr(p, pat);
    if (!k)
        return false;
    *out = strtod(k + strlen(pat), NULL);
    return true;
}

static bool hypr_str_after(const char *text, const char *anchor,
                           const char *key, char *out, size_t n)
{
    const char *p = strstr(text, anchor);
    if (!p)
        return false;
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *k = strstr(p, pat);
    if (!k)
        return false;
    const char *q = strchr(k + strlen(pat), '"');
    if (!q)
        return false;
    q++;
    size_t i = 0;
    while (q[i] && q[i] != '"' && i + 1 < n)
        i++;
    memcpy(out, q, i);
    out[i] = '\0';
    return true;
}

static int hypr_current(char *name, size_t nn, int *w, int *h, double *refresh)
{
    char buf[HYPR_BUF];
    if (hypr_query(buf, sizeof(buf)) != 0)
        return -1;
    /* first monitor entry is the focused one on typical setups */
    if (!hypr_str_after(buf, "\"monitors\"", "name", name, nn))
        return -1;
    double dw = 0, dh = 0, dr = 0;
    if (!hypr_num_after(buf, "\"currentMode\"", "width", &dw) ||
        !hypr_num_after(buf, "\"currentMode\"", "height", &dh) ||
        !hypr_num_after(buf, "\"currentMode\"", "refreshRate", &dr))
        return -1;
    *w = (int)dw;
    *h = (int)dh;
    *refresh = dr;
    return 0;
}

/* ---- ops ------------------------------------------------------------- */

static bool hypr_detect(void)
{
    if (!getenv("HYPRLAND_INSTANCE_SIGNATURE"))
        return false;
    if (!ut_have_cmd("hyprctl"))
        return false;
    char name[64];
    int w, h;
    double r;
    return hypr_current(name, sizeof(name), &w, &h, &r) == 0;
}

static int hypr_current_hz(void)
{
    char name[64];
    int w, h;
    double r;
    if (hypr_current(name, sizeof(name), &w, &h, &r) != 0)
        return -1;
    return (int)(r + 0.5);
}

static int hypr_modes_hz(int *out, int max)
{
    char buf[HYPR_BUF];
    if (hypr_query(buf, sizeof(buf)) != 0)
        return 0;

    const char *p = strstr(buf, "\"availableModes\"");
    int n = 0;
    if (p) {
        const char *q = p;
        while ((q = strchr(q, '"')) != NULL && n < max) {
            q++;
            int w = 0, h = 0;
            double r = 0;
            if (sscanf(q, "%dx%d@%lfHz", &w, &h, &r) == 3 && w > 0 && r > 0) {
                int hz = (int)(r + 0.5);
                bool seen = false;
                for (int j = 0; j < n; j++)
                    if (out[j] == hz)
                        seen = true;
                if (!seen)
                    out[n++] = hz;
            }
            /* skip past this quoted string */
            const char *e = strchr(q, '"');
            q = e ? e + 1 : q;
        }
    }

    if (n == 0) {
        /* no availableModes: fall back to the current refresh */
        int hz = hypr_current_hz();
        if (hz > 0)
            out[n++] = hz;
    }

    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && out[j - 1] > out[j]; j--) {
            int t = out[j - 1];
            out[j - 1] = out[j];
            out[j] = t;
        }
    return n;
}

static int hypr_set_hz(int hz)
{
    char name[64];
    int w, h;
    double refresh;
    if (hypr_current(name, sizeof(name), &w, &h, &refresh) != 0)
        return -1;

    /* find the closest available mode at this refresh */
    int modes[32];
    int n = hypr_modes_hz(modes, 32);
    double target = hz;
    if (n > 0) {
        int best = modes[0], bestd = 1 << 30;
        for (int i = 0; i < n; i++) {
            int d = modes[i] > hz ? modes[i] - hz : hz - modes[i];
            if (d < bestd) {
                bestd = d;
                best = modes[i];
            }
        }
        target = best;
    }

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "hyprctl keyword monitor %s,%dx%d@%.2f,0x0,1", name, w, h, target);
    int rc = ut_exec(cmd, NULL, 0);
    ut_log("display(hypr): %s -> rc=%d", cmd, rc);
    return rc == 0 ? 0 : -1;
}

const display_ops_t disp_hypr_ops = {
    .name        = "hyprland",
    .detect      = hypr_detect,
    .current_hz  = hypr_current_hz,
    .modes_hz    = hypr_modes_hz,
    .set_hz      = hypr_set_hz,
};
