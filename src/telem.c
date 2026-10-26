#include "telem.h"

#include <stdio.h>
#include <string.h>

/* Single global store: ctron is a single-instance TUI and the pure
 * core stays testable through telem_reset(). */
typedef struct {
    double v[TELEM_N];
    int head, count;
} ring_t;

static ring_t s_rings[TS_COUNT];
static long s_last_tick = -1;

static const struct {
    const char *key;
    double mn, mx;
} DEFS[] = {
#define TELEM_DEF(id, key, mn, mx) { key, mn, mx },
    TELEM_SERIES(TELEM_DEF)
#undef TELEM_DEF
};

const char *telem_key(int idx)
{
    return (idx >= 0 && idx < TS_COUNT) ? DEFS[idx].key : NULL;
}

void telem_range(int idx, double *mn, double *mx)
{
    if (mn)
        *mn = (idx >= 0 && idx < TS_COUNT) ? DEFS[idx].mn : TELEM_AUTO;
    if (mx)
        *mx = (idx >= 0 && idx < TS_COUNT) ? DEFS[idx].mx : TELEM_AUTO;
}

void telem_reset(void)
{
    memset(s_rings, 0, sizeof s_rings);
    s_last_tick = -1;
}

int telem_tick(long mono_ms)
{
    if (s_last_tick >= 0 && mono_ms - s_last_tick < 250)
        return 0;
    s_last_tick = mono_ms;
    return 1;
}

void telem_push(int idx, double v)
{
    if (idx < 0 || idx >= TS_COUNT || v < 0)
        return;
    ring_t *r = &s_rings[idx];
    int pos = (r->head + r->count) % TELEM_N;
    r->v[pos] = v;
    if (r->count < TELEM_N)
        r->count++;
    else
        r->head = (r->head + 1) % TELEM_N;
}

double telem_last(int idx)
{
    if (idx < 0 || idx >= TS_COUNT || s_rings[idx].count == 0)
        return -1;
    ring_t *r = &s_rings[idx];
    return r->v[(r->head + r->count - 1) % TELEM_N];
}

int telem_count(int idx)
{
    return (idx >= 0 && idx < TS_COUNT) ? s_rings[idx].count : 0;
}

bool telem_at(int idx, int back, double *v)
{
    if (v)
        *v = -1;
    if (idx < 0 || idx >= TS_COUNT || back < 0)
        return false;
    ring_t *r = &s_rings[idx];
    if (back >= r->count)
        return false;
    if (v)
        *v = r->v[(r->head + r->count - 1 - back) % TELEM_N];
    return true;
}

void telem_minmax(int idx, double *mn, double *mx)
{
    if (idx < 0 || idx >= TS_COUNT || s_rings[idx].count == 0) {
        if (mn)
            *mn = -1;
        if (mx)
            *mx = -1;
        return;
    }
    ring_t *r = &s_rings[idx];
    double lo = r->v[r->head], hi = r->v[r->head];
    for (int i = 1; i < r->count; i++) {
        double v = r->v[(r->head + i) % TELEM_N];
        if (v < lo)
            lo = v;
        if (v > hi)
            hi = v;
    }
    if (mn)
        *mn = lo;
    if (mx)
        *mx = hi;
}

void telem_spark(int idx, char *out, size_t n, int cells)
{
    if (!out || n == 0)
        return;
    out[0] = '\0';
    if (idx < 0 || idx >= TS_COUNT || cells <= 0)
        return;
    ring_t *r = &s_rings[idx];
    if (r->count == 0) {
        snprintf(out, n, "--");
        return;
    }

    double mn, mx;
    telem_range(idx, &mn, &mx);
    if (mn == TELEM_AUTO && mx == TELEM_AUTO)
        telem_minmax(idx, &mn, &mx);
    if (mx <= mn) {
        /* flat window: every sample sits at the mid block */
        mn -= 1.0;
        mx += 1.0;
    }

    int take = r->count < cells ? r->count : cells;
    size_t w = 0;
    for (int k = take - 1; k >= 0; k--) {
        double v = r->v[(r->head + r->count - 1 - k) % TELEM_N];
        double lvl = (v - mn) / (mx - mn);
        if (lvl < 0)
            lvl = 0;
        if (lvl > 1)
            lvl = 1;
        int b = (int)(lvl * 7.999);
        if (w + 4 > n)
            break;
        out[w++] = (char)0xE2;
        out[w++] = (char)0x96;
        out[w++] = (char)(0x81 + b);
    }
    out[w] = '\0';
}
