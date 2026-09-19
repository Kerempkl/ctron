#ifndef CTRON_DISPLAY_H
#define CTRON_DISPLAY_H

#include <stdbool.h>

/* Display refresh-rate backend contract.
 *
 * One file per compositor (display_kde.c, display_hypr.c, ...). A backend
 * is selected once at startup: DISPLAY_BACKEND=<name> forces it, otherwise
 * the first backend whose detect() succeeds wins. Adding a backend means
 * adding one file and one line in display.c's backend list.
 *
 * The Hyprland backend is intentionally kept simple; it is the designated
 * handoff point for a second developer (see README "Hyprland handoff"). */
typedef struct display_ops {
    const char *name;                /* "kde", "hyprland" */
    bool (*detect)(void);            /* is this my session? */
    int  (*current_hz)(void);        /* -1 unknown */
    int  (*modes_hz)(int *out, int max); /* unique supported Hz, ascending; count */
    int  (*set_hz)(int hz);          /* 0 ok, -1 failed/unsupported */
} display_ops_t;

/* Resolved backend or NULL when nothing matched (headless / unknown). */
const display_ops_t *display_get(void);

/* Convenience: pick the Hz from the mode list closest to `hz`. */
int display_nearest_hz(int hz);

#endif /* CTRON_DISPLAY_H */
