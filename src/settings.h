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

#define SETTINGS_SNAP_MAX    48
#define SETTINGS_BOOT_ID_MAX 64

typedef struct {
    int poll_ms;       /* telemetry refresh, 100..2000 */
    int write_pref;    /* enum above */
    int theme;         /* palette index, 0..4 */
    bool gpu_temp;     /* query nvidia-smi */
    int startup_view;  /* initial TUI view, 0..4 (Power..Telemetry) */
    char startup_snapshot[SETTINGS_SNAP_MAX]; /* "" = off (default), else .ctr name */
    /* legacy layout fields kept for backward-compatible settings.ini */
    bool swap_left;
    bool telem_top;
    int left_pct;
    int split_pct;
    int telem_h;
} prefs_t;

/* Global preferences, loaded at startup. */
extern prefs_t g_prefs;

void settings_dir(char *out, size_t n);
const char *settings_file(char *out, size_t n);
const char *settings_profiles_dir(char *out, size_t n);
const char *settings_fancurves_dir(char *out, size_t n);
const char *settings_modes_file(char *out, size_t n);

/* Read current kernel boot UUID (/proc/sys/kernel/random/boot_id).
 * Returns 0 on success, -1 if unavailable. */
int settings_boot_id(char *out, size_t n);

/* Validate and set g_prefs.startup_snapshot ("" = off). Rejects path
 * traversal ("..", leading/trailing '/', disallowed characters). */
void settings_set_startup_snapshot(const char *name);

/* Keep startup_snapshot in sync when a snapshot is renamed or deleted.
 * Patches settings.ini atomically only when startup_snapshot matched. */
void settings_on_snapshot_renamed(const char *old_name, const char *new_name);
void settings_on_snapshot_deleted(const char *name);

/* Load prefs + persisted hardware state (fan curves, cpu limit, same-boot
 * PPT/GPU-clock memory). Missing file keeps defaults. Returns 0 when the
 * file existed. */
int settings_load(hw_state_t *hw);

/* Persist prefs + hardware state. */
int settings_save(const hw_state_t *hw);

#endif /* CTRON_SETTINGS_H */
