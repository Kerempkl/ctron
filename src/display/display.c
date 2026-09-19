#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "display.h"
#include "../util.h"

#include <stdlib.h>
#include <string.h>

/* Backend registry. Order = auto-detect priority. */
extern const display_ops_t disp_kde_ops;
extern const display_ops_t disp_hypr_ops;

static const display_ops_t *const s_backends[] = {
    &disp_kde_ops,
    &disp_hypr_ops,
    NULL
};

const display_ops_t *display_get(void)
{
    static const display_ops_t *cached;
    static bool resolved = false;

    if (resolved)
        return cached;
    resolved = true;

    const char *force = getenv("DISPLAY_BACKEND");
    if (force && force[0]) {
        for (int i = 0; s_backends[i]; i++) {
            if (!strcasecmp(s_backends[i]->name, force)) {
                cached = s_backends[i];
                return cached;
            }
        }
        ut_log("display: DISPLAY_BACKEND=%s unknown", force);
        return NULL;
    }

    for (int i = 0; s_backends[i]; i++) {
        if (s_backends[i]->detect()) {
            cached = s_backends[i];
            ut_log("display backend: %s", cached->name);
            return cached;
        }
    }
    return NULL;
}

int display_nearest_hz(int hz)
{
    const display_ops_t *d = display_get();
    if (!d)
        return hz;
    int modes[32];
    int n = d->modes_hz(modes, 32);
    if (n < 1)
        return hz;

    int best = modes[0], bestd = 1 << 30;
    for (int i = 0; i < n; i++) {
        int diff = modes[i] > hz ? modes[i] - hz : hz - modes[i];
        if (diff < bestd) {
            bestd = diff;
            best = modes[i];
        }
    }
    return best;
}
