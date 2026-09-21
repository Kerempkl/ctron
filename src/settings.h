#ifndef CTRON_SETTINGS_H
#define CTRON_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

#include "hw.h"

/* Write-method preference (Settings panel). */
enum {
    PREF_ASUSCTL_FIRST = 0, /* default */
    PREF_SYSFS_FIRST,
    PREF_NO_SUDO          /* never escalate; sysfs writes only */
};

typedef struct {
    int poll_ms;       /* telemetry refresh, 100..2000 */
    int write_pref;    /* enum above */
    int theme;         /* palette index, 0..4 */
    bool gpu_temp;     /* query nvidia-smi */
    /* layout (Settings overlay → LAYOUT; applied live by ui_layout) */
    bool swap_left;    /* CONTROLS above PROFILES in the left column */
    bool telem_top;    /* telemetry strip at the top instead of bottom */
    int left_pct;      /* left column width, 25..50 % */
    int split_pct;     /* upper left panel's share, 25..75 % */
    int telem_h;       /* telemetry height, 3..10 rows */
} prefs_t;

/* Global preferences, loaded at startup. */
extern prefs_t g_prefs;

void settings_dir(char *out, size_t n);
const char *settings_file(char *out, size_t n);
const char *settings_profiles_dir(char *out, size_t n);
const char *settings_modes_file(char *out, size_t n);

/* Load prefs + persisted hardware state (fan curves, cpu limit).
 * Missing file keeps defaults. Returns 0 when the file existed. */
int settings_load(hw_state_t *hw);

/* Persist prefs + hardware state. */
int settings_save(const hw_state_t *hw);

#endif /* CTRON_SETTINGS_H */
