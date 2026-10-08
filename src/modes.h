#ifndef CTRON_MODES_H
#define CTRON_MODES_H

#include <stddef.h>

#include "hw.h"

/* User modes ("CLI shortcuts"). Stored in modes.ini as
 *   [turbo]
 *   steps = profile performance, ppt P80, fan cool, hz max
 * and applied step-by-step through the shared command table. */

#define MODES_MAX     32
#define MODE_NAME_MAX 24
#define MODE_STEPS_MAX 220

typedef struct {
    char name[MODE_NAME_MAX];
    char steps[MODE_STEPS_MAX];
} mode_def_t;

const char *modes_path(char *out, size_t n);

/* Load all modes; writes the defaults first when the file is missing. */
int modes_load(mode_def_t *arr, int max);
int modes_save(const mode_def_t *arr, int n);

/* Seed modes.ini with the stock shortcuts when absent. */
void modes_write_defaults(void);

int mode_find(const mode_def_t *arr, int n, const char *name);

/* ---- applied-mode tracking (CONTROLS Mode row) ------------------------ *
 * After a mode is applied, the Mode row shows "- / name / name*" — the
 * star marks drift: a tracked field the mode touched now differs from
 * the value captured at apply time. Session-only (no persistence);
 * unknown/stale live values never count as drift; fields the mode's
 * steps did not touch are ignored. Pure helpers, unit-tested. */

/* which set-point fields a step key maps to (0 = not tracked: aura,
 * ac/battery-profile, fan-write, battery-oneshot) */
enum {
    MS_PROFILE = 1 << 0,
    MS_EPP     = 1 << 1,
    MS_PPT     = 1 << 2,   /* the SPL/SPPT/FPPT triple as one unit */
    MS_HZ      = 1 << 3,
    MS_BAT     = 1 << 4,
    MS_KBD     = 1 << 5,
    MS_BOOST   = 1 << 6,
    MS_OD      = 1 << 7,
    MS_NVB     = 1 << 8,
    MS_NVT     = 1 << 9,
    MS_FREQ    = 1 << 10,
    MS_FAN_CPU = 1 << 11,
    MS_FAN_GPU = 1 << 12,
    MS_GPUCLOCK = 1 << 13,
};

/* values of the tracked fields at apply time */
typedef struct {
    int profile, epp;
    int spl, sppt, fppt;
    int hz, bat, kbd;
    int boost, od;             /* 0/1 */
    int nvb, nvt, mhz, gclock;
    fan_curve_t fan_cpu, fan_gpu;
} mode_snap_t;

/* Parse the step list and OR together the tracked-field bits. Pure. */
unsigned mode_touch_mask(const char *steps);

/* Copy the tracked fields from hw into snap, restricted to `want`.
 * Bits whose live value is unknown/stale (ppt 0, hz 0, ...) are DROPPED
 * from the returned mask: what cannot be read cannot be tracked. */
unsigned mode_snapshot(const hw_state_t *hw, unsigned want, mode_snap_t *snap);

/* Number of tracked fields that now differ from the snapshot. Live
 * values that are unknown/stale do not count as drift. Pure. */
int mode_drift_count(const hw_state_t *hw, unsigned mask,
                     const mode_snap_t *snap);

/* Split a step list on commas that begin a new command. Commas inside
 * values (ppt 45,55,55 and fan-curve csv) stay in the step. Returns
 * the number of steps. `out` may be NULL to count only. Each row of
 * `out` is MODE_STEPS_MAX bytes. */
int mode_split_steps(const char *steps, char (*out)[MODE_STEPS_MAX], int max);

/* Execute a comma separated step list ("profile performance, fan cool").
 * Stops at the first failing step. Returns 0 when all steps applied. */
int mode_apply(hw_state_t *hw, const char *steps, char *err, size_t errn);

#endif /* CTRON_MODES_H */
