#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "display.h"
#include "../util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Hyprland via `hyprctl monitors -j`.
 *
 * Recent hyprctl prints a JSON *array* of monitor objects:
 *   [{ "name":"eDP-1", "width":1920, "height":1080, "refreshRate":144.06,
 *      "x":0, "y":0, "scale":1.00, "focused":true,
 *      "availableModes":["1920x1080@60.06Hz","1920x1080@144.06Hz"] }]
 * There is no "monitors" wrapper and no "currentMode" object (those were
 * assumed by the first v2 port). Parse the focused entry, else the first.
 */

#define HYPR_BUF 16384

typedef struct {
    char name[64];
    int w, h;
    double refresh;
    int x, y;
    double scale;
} hypr_mon_t;

static int hypr_query(char *buf, size_t n)
{
    return ut_exec_raw("hyprctl monitors -j", buf, n);
}

static bool json_str(const char *obj, const char *key, char *out, size_t n)
{
    char pat[72];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *k = strstr(obj, pat);
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

static bool json_num(const char *obj, const char *key, double *out)
{
    char pat[72];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *k = strstr(obj, pat);
    if (!k)
        return false;
    *out = strtod(k + strlen(pat), NULL);
    return true;
}

/* Bound a monitor object: from `{` after `[` or `,` until matching `}`. */
static int hypr_extract_obj(const char *from, char *out, size_t n)
{
    const char *brace = strchr(from, '{');
    if (!brace)
        return -1;
    int depth = 0;
    size_t i = 0;
    for (const char *p = brace; *p && i + 1 < n; p++) {
        out[i++] = *p;
        if (*p == '{')
            depth++;
        else if (*p == '}') {
            depth--;
            if (depth == 0) {
                out[i] = '\0';
                return 0;
            }
        }
    }
    return -1;
}

static int hypr_parse_mon(const char *obj, hypr_mon_t *m)
{
    memset(m, 0, sizeof(*m));
    m->scale = 1.0;
    if (!json_str(obj, "name", m->name, sizeof(m->name)))
        return -1;
    double dw = 0, dh = 0, dr = 0, dx = 0, dy = 0, ds = 1;
    /* Prefer currentMode.* when present (older hyprctl), else top-level. */
    if (!json_num(obj, "width", &dw) || !json_num(obj, "height", &dh))
        return -1;
    if (!json_num(obj, "refreshRate", &dr))
        return -1;
    json_num(obj, "x", &dx);
    json_num(obj, "y", &dy);
    json_num(obj, "scale", &ds);
    m->w = (int)dw;
    m->h = (int)dh;
    m->refresh = dr;
    m->x = (int)dx;
    m->y = (int)dy;
    m->scale = ds > 0 ? ds : 1.0;
    return 0;
}

static int hypr_read_focused(hypr_mon_t *m)
{
    char buf[HYPR_BUF];
    if (hypr_query(buf, sizeof(buf)) != 0)
        return -1;

    hypr_mon_t first = {0};
    int have_first = 0;
    const char *p = buf;
    while ((p = strchr(p, '{')) != NULL) {
        char obj[HYPR_BUF];
        if (hypr_extract_obj(p, obj, sizeof(obj)) != 0)
            break;
        hypr_mon_t cur;
        if (hypr_parse_mon(obj, &cur) == 0 && cur.w > 0) {
            if (!have_first) {
                first = cur;
                have_first = 1;
            }
            if (strstr(obj, "\"focused\": true") || strstr(obj, "\"focused\":true")) {
                *m = cur;
                return 0;
            }
        }
        p++;
    }
    if (have_first) {
        *m = first;
        return 0;
    }
    return -1;
}

/* ---- ops ------------------------------------------------------------- */

static bool hypr_detect(void)
{
    if (!getenv("HYPRLAND_INSTANCE_SIGNATURE"))
        return false;
    if (!ut_have_cmd("hyprctl"))
        return false;
    hypr_mon_t m;
    return hypr_read_focused(&m) == 0;
}

static int hypr_current_hz(void)
{
    hypr_mon_t m;
    if (hypr_read_focused(&m) != 0)
        return -1;
    return (int)(m.refresh + 0.5);
}

static int hypr_modes_hz(int *out, int max)
{
    char buf[HYPR_BUF];
    if (hypr_query(buf, sizeof(buf)) != 0)
        return 0;

    int n = 0;
    const char *p = strstr(buf, "\"availableModes\"");
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
            const char *e = strchr(q, '"');
            q = e ? e + 1 : q;
        }
    }
    if (n == 0) {
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

static int hypr_pick_mode_string(int hz, int w, int h, char *out, size_t n)
{
    char buf[HYPR_BUF];
    if (hypr_query(buf, sizeof(buf)) != 0)
        return -1;
    const char *p = strstr(buf, "\"availableModes\"");
    if (!p)
        return -1;
    const char *q = p;
    const char *best = NULL;
    int bestd = 1 << 30;
    double bestr = 0;
    while ((q = strchr(q, '"')) != NULL) {
        q++;
        int mw = 0, mh = 0;
        double r = 0;
        if (sscanf(q, "%dx%d@%lfHz", &mw, &mh, &r) == 3 && mw == w && mh == h && r > 0) {
            int ihz = (int)(r + 0.5);
            int d = ihz > hz ? ihz - hz : hz - ihz;
            if (d < bestd) {
                bestd = d;
                best = q;
                bestr = r;
            }
        }
        const char *e = strchr(q, '"');
        q = e ? e + 1 : q;
    }
    if (!best)
        return -1;
    snprintf(out, n, "%dx%d@%.2f", w, h, bestr);
    return 0;
}

static int hypr_set_hz(int hz)
{
    hypr_mon_t m;
    if (hypr_read_focused(&m) != 0)
        return -1;

    char mode[64];
    if (hypr_pick_mode_string(hz, m.w, m.h, mode, sizeof(mode)) != 0)
        snprintf(mode, sizeof(mode), "%dx%d@%d", m.w, m.h, hz);

    char cmd[640];
    /* Hyprland 0.55+ Lua parser: `keyword monitor` is a no-op (exit 0,
     * "use eval"). hl.monitor() is the live API. Fall back to keyword
     * for 0.54 and older. */
    snprintf(cmd, sizeof(cmd),
             "hyprctl eval 'hl.monitor({ output = \"%s\", mode = \"%s\", "
             "position = \"%dx%d\", scale = %g })'",
             m.name, mode, m.x, m.y, m.scale);
    int rc = ut_exec(cmd, NULL, 0);
    if (rc != 0) {
        snprintf(cmd, sizeof(cmd),
                 "hyprctl keyword monitor %s,%s,%dx%d,%.2f",
                 m.name, mode, m.x, m.y, m.scale);
        rc = ut_exec(cmd, NULL, 0);
    }
    ut_log("display(hypr): %s -> rc=%d", cmd, rc);
    /* 0.55 `keyword` exits 0 without applying. Trust the compositor. */
    {
        const char *at = strchr(mode, '@');
        int want = at ? (int)(strtod(at + 1, NULL) + 0.5) : hz;
        for (int t = 0; t < 10; t++) {
            int now = hypr_current_hz();
            int d = now > want ? now - want : want - now;
            if (now > 0 && d <= 1)
                return 0;
            usleep(30000);
        }
    }
    return -1;
}

const display_ops_t disp_hypr_ops = {
    .name        = "hyprland",
    .detect      = hypr_detect,
    .current_hz  = hypr_current_hz,
    .modes_hz    = hypr_modes_hz,
    .set_hz      = hypr_set_hz,
};
