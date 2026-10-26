#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "display.h"
#include "../util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* KDE Plasma (Wayland/X11) backend via kscreen-doctor.
 *
 * Query:  `kscreen-doctor -o` (human output, ANSI already stripped by
 *         ut_exec_raw). Relevant lines:
 *           Output: 1 eDP-2 38c0a9e2-...
 *           Modes:  1:2560x1600@165.00*!  2:2560x1600@60.00 ...
 *         '*' marks the current mode, '!' the preferred one.
 * Apply:  `kscreen-doctor output.<name>.mode.<W>x<H>@<refresh>`
 */

#define KDE_BUF 8192

typedef struct {
    int w, h;
    double refresh;
    bool current;
} kde_mode_t;

/* Bounded substring search: needle fully inside [s, e). */
static const char *kde_find(const char *s, const char *e, const char *needle)
{
    size_t len = strlen(needle);
    for (const char *p = s; p + len <= e; p++)
        if (memcmp(p, needle, len) == 0)
            return p;
    return NULL;
}

/* Parse "Output: <idx> <name> <uuid>" from a line starting at
 * "Output:" -> name. */
static int kde_parse_name_at(const char *p, char *name, size_t n)
{
    p += strlen("Output:");
    while (*p && isspace((unsigned char)*p))
        p++;
    /* skip output index */
    while (*p && !isspace((unsigned char)*p))
        p++;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (!*p)
        return -1;
    size_t i = 0;
    while (p[i] && !isspace((unsigned char)p[i]) && i + 1 < n)
        i++;
    if (i == 0)
        return -1;
    memcpy(name, p, i);
    name[i] = '\0';
    return 0;
}

/* Pick the output block we control and bound it to [start, next
 * "Output:" or end): the laptop panel (eDP*) when present, else the
 * first block. On multi-monitor listings the chosen block's own Modes
 * line is parsed, so a second monitor's modes never leak into the
 * panel's list (and vice versa). */
static const char *kde_pick_block(const char *text, char *name, size_t n,
                                  const char **end_out)
{
    const char *best = NULL, *best_end = NULL;
    char best_name[64] = {0};
    const char *p = strstr(text, "Output:");
    while (p) {
        const char *next = strstr(p + strlen("Output:"), "Output:");
        const char *bend = next ? next : p + strlen(p);
        char nm[64];
        if (kde_parse_name_at(p, nm, sizeof nm) == 0 &&
            (!best || (strncmp(nm, "eDP", 3) == 0 &&
                       strncmp(best_name, "eDP", 3) != 0))) {
            best = p;
            best_end = bend;
            snprintf(best_name, sizeof best_name, "%s", nm);
        }
        p = next;
    }
    if (!best)
        return NULL;
    snprintf(name, n, "%s", best_name);
    *end_out = best_end;
    return best;
}

/* Scan every "N:WxH@R[*][!]" token in the "Modes:" line inside
 * [start, end). */
static int kde_parse_modes_range(const char *start, const char *end,
                                 kde_mode_t *out, int max)
{
    const char *line = kde_find(start, end, "Modes:");
    if (!line)
        return 0;
    const char *nl = memchr(line, '\n', (size_t)(end - line));
    const char *lend = nl ? nl : end;

    int n = 0;
    const char *p = line;
    while (p < lend && n < max) {
        const char *colon = memchr(p, ':', (size_t)(lend - p));
        if (!colon || colon + 1 >= lend)
            break;
        const char *q = colon + 1;
        int w = (int)strtol(q, (char **)&q, 10);
        if (*q != 'x') {
            p = q;
            continue;
        }
        q++;
        int h = (int)strtol(q, (char **)&q, 10);
        if (*q != '@') {
            p = q;
            continue;
        }
        q++;
        double r = strtod(q, (char **)&q);
        bool cur = false;
        while (q < lend && (*q == '*' || *q == '!')) {
            if (*q == '*')
                cur = true;
            q++;
        }
        if (w > 0 && h > 0 && r > 0.0) {
            out[n].w = w;
            out[n].h = h;
            out[n].refresh = r;
            out[n].current = cur;
            n++;
        }
        p = q;
    }
    return n;
}

static int kde_query(char *buf, size_t n, char *name, size_t name_n,
                     kde_mode_t *modes, int *mode_count, int max_modes)
{
    const char *q[] = { "kscreen-doctor", "-o", NULL };
    if (ut_exec_argv_raw(q, buf, n) != 0)
        return -1;
    const char *bend = NULL;
    const char *block = kde_pick_block(buf, name, name_n, &bend);
    if (!block)
        return -1;
    *mode_count = kde_parse_modes_range(block, bend, modes, max_modes);
    return 0;
}

/* Offline parser entry point for tests (no kscreen-doctor involved). */
int disp_kde_test_parse(const char *text, char *name, size_t nn,
                        int *hz_cur, int *hz_out, int hz_max)
{
    const char *bend = NULL;
    const char *block = kde_pick_block(text, name, nn, &bend);
    if (!block)
        return -1;
    kde_mode_t modes[64];
    int mc = kde_parse_modes_range(block, bend, modes, 64);
    int n = 0;
    for (int i = 0; i < mc && n < hz_max; i++) {
        if (hz_cur && modes[i].current)
            *hz_cur = (int)(modes[i].refresh + 0.5);
        int hz = (int)(modes[i].refresh + 0.5);
        bool seen = false;
        for (int j = 0; j < n; j++)
            if (hz_out[j] == hz)
                seen = true;
        if (!seen)
            hz_out[n++] = hz;
    }
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && hz_out[j - 1] > hz_out[j]; j--) {
            int t = hz_out[j - 1];
            hz_out[j - 1] = hz_out[j];
            hz_out[j] = t;
        }
    return n;
}

/* ---- ops ------------------------------------------------------------- */

static bool kde_detect(void)
{
    char buf[KDE_BUF], name[64];
    kde_mode_t modes[64];
    int mc = 0;
    if (!ut_have_cmd("kscreen-doctor"))
        return false;
    /* Cheap session hint first, but rely on parseability as the source of
     * truth so the backend also works over ssh into a KDE session. */
    if (kde_query(buf, sizeof(buf), name, sizeof(name), modes, &mc, 64) != 0)
        return false;
    return mc > 0;
}

static int kde_current_hz(void)
{
    char buf[KDE_BUF], name[64];
    kde_mode_t modes[64];
    int mc = 0;
    if (kde_query(buf, sizeof(buf), name, sizeof(name), modes, &mc, 64) != 0)
        return -1;
    for (int i = 0; i < mc; i++) {
        if (modes[i].current)
            return (int)(modes[i].refresh + 0.5);
    }
    return -1;
}

static int kde_modes_hz(int *out, int max)
{
    char buf[KDE_BUF], name[64];
    kde_mode_t modes[64];
    int mc = 0;
    if (kde_query(buf, sizeof(buf), name, sizeof(name), modes, &mc, 64) != 0)
        return 0;

    int n = 0;
    for (int i = 0; i < mc && n < max; i++) {
        int hz = (int)(modes[i].refresh + 0.5);
        bool seen = false;
        for (int j = 0; j < n; j++)
            if (out[j] == hz)
                seen = true;
        if (!seen)
            out[n++] = hz;
    }
    /* ascending */
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && out[j - 1] > out[j]; j--) {
            int t = out[j - 1];
            out[j - 1] = out[j];
            out[j] = t;
        }
    return n;
}

static int kde_set_hz(int hz)
{
    char buf[KDE_BUF], name[64];
    kde_mode_t modes[64];
    int mc = 0;
    if (kde_query(buf, sizeof(buf), name, sizeof(name), modes, &mc, 64) != 0)
        return -1;

    /* Keep the current resolution; switch only the refresh. */
    int cw = 0, ch = 0;
    for (int i = 0; i < mc; i++) {
        if (modes[i].current) {
            cw = modes[i].w;
            ch = modes[i].h;
            break;
        }
    }
    if (cw == 0)
        return -1;

    /* nearest mode at that resolution */
    int best = -1;
    double bestd = 1e9;
    for (int i = 0; i < mc; i++) {
        if (modes[i].w != cw || modes[i].h != ch)
            continue;
        double d = modes[i].refresh > hz ? modes[i].refresh - hz : hz - modes[i].refresh;
        if (d < bestd) {
            bestd = d;
            best = i;
        }
    }
    if (best < 0)
        return -1;

    /* kscreen-doctor wants an integer refresh ("mode.WxH@R"); it rejects
     * "R.00" with "Unable to parse arguments" — and still exits 0, so the
     * result is verified by reading the mode back. */
    char modearg[160], out[256] = {0};
    int want_hz = (int)(modes[best].refresh + 0.5);
    snprintf(modearg, sizeof(modearg), "output.%s.mode.%dx%d@%d",
             name, modes[best].w, modes[best].h, want_hz);
    const char *av[] = { "kscreen-doctor", modearg, NULL };
    ut_exec_argv(av, out, sizeof(out));

    int now = kde_current_hz();
    if (now == want_hz) {
        ut_log("display(kde): %d Hz ok", want_hz);
        return 0;
    }
    ut_log("display(kde): failed (now %d Hz%s%s)", now,
           out[0] ? "; kscreen-doctor said: " : "", out);
    return -1;
}

const display_ops_t disp_kde_ops = {
    .name        = "kde",
    .detect      = kde_detect,
    .current_hz  = kde_current_hz,
    .modes_hz    = kde_modes_hz,
    .set_hz      = kde_set_hz,
};
