#ifndef CTRON_FAN_H
#define CTRON_FAN_H

#include <stdbool.h>
#include <stddef.h>

/* ASUS custom fan curves hold up to 8 points. X = temperature °C,
 * Y = pwm 0..255. Points are kept sorted by temperature with unique
 * temps (the EC rejects duplicate/unsorted tables). */
#define FAN_POINTS 8
#define FAN_TMIN 20
#define FAN_TMAX 105

typedef struct {
    int n;                    /* points in use, 0..FAN_POINTS */
    int temp_c[FAN_POINTS];
    int pwm[FAN_POINTS];
} fan_curve_t;

/* A sane default curve used when no hwmon table exists yet. */
void fan_default(fan_curve_t *fc);

/* CSV round-trip: "40,55,70" / "80,120,180". Returns point count or -1. */
int fan_from_csv(fan_curve_t *fc, const char *temps, const char *pwms);
void fan_to_csv(const fan_curve_t *fc, char *t, size_t tn, char *p, size_t pn);

/* Manipulation. All return the (possibly new) index of the affected
 * point, or -1 on failure. Temps are clamped to [FAN_TMIN, FAN_TMAX],
 * pwm to [0, 255]; the table is re-sorted and made unique. */
int fan_add_point(fan_curve_t *fc, int temp, int pwm);
int fan_del_point(fan_curve_t *fc, int idx);
int fan_nudge_point(fan_curve_t *fc, int idx, int dtemp, int dpwm);
int fan_set_point(fan_curve_t *fc, int idx, int temp, int pwm);

/* Scale every pwm by num/den (clamped). Used by the Silent/Cool presets. */
void fan_scale(fan_curve_t *dst, const fan_curve_t *src, int num, int den);

/* asus_custom_fan_curve pwmN_enable (kernel asus-wmi, 6.18 and 7.x):
 * 1 applies the stored curve, 2 is factory automatic. The points stay
 * in sysfs either way. 0 is rejected. 3 restores the factory curve.
 * Writing any point clears the enable, so 1 has to be written after
 * the points. `on` is nonzero for "custom curve on". */
int fan_enable_raw(int on);
int fan_raw_is_on(int raw);

/* Preset fan curves. QUIET/BALANCED/PERF come from asusd's
 * fan_curves.ron when present (asusd's CURRENT state — it rewrites
 * the file on every curve change), or from a compiled-in snapshot of
 * this machine's asusd state (EC-seeded factory reference, probed
 * 2026-10-18). ACTIVE re-reads the live EC curves. */
typedef enum {
    FAN_PRESET_QUIET = 0,
    FAN_PRESET_BALANCED,
    FAN_PRESET_PERF,
    FAN_PRESET_ACTIVE,
    FAN_PRESET_COUNT
} fan_preset_t;

const char *fan_preset_name(fan_preset_t p);

/* Where a preset's data came from (shown in the presets modal). */
typedef enum {
    FAN_PSRC_RON,    /* /etc/asusd/fan_curves.ron (asusd's current state) */
    FAN_PSRC_BUILTIN,/* compiled-in snapshot of this machine's ron (EC-seeded factory ref) */
    FAN_PSRC_HWMON,  /* live EC hwmon read (ACTIVE) */
    FAN_PSRC_STOCK,  /* init-time snapshot (ACTIVE, hwmon unreadable) */
    FAN_PSRC_DEFAULT /* generic fan_default (ACTIVE, no data at all) */
} fan_preset_src_t;

typedef struct {
    fan_preset_src_t src;
    bool cpu_en;   /* asusd enabled flag for this profile (ron only) */
    bool gpu_en;
} fan_preset_info_t;

struct hw_state;

/* Pure built-in snapshot fill (no file/hardware access) — exported for
 * tests so the tables can be pinned without /etc present. */
int fan_preset_builtin(fan_preset_t preset, fan_curve_t *cpu, fan_curve_t *gpu);

/* Retrieve preset curves. For QUIET/BALANCED/PERF, tries /etc/asusd/fan_curves.ron
 * first, falling back to the built-in snapshot. For ACTIVE, re-reads live sysfs
 * hwmon curves. Either cpu or gpu may be NULL if caller only wants one.
 * info (optional) reports the source and asusd's enabled flags.
 * Returns 0 on success, -1 on failure. */
int fan_preset_get(struct hw_state *hw, fan_preset_t preset, fan_curve_t *cpu,
                   fan_curve_t *gpu, fan_preset_info_t *info);

/* Pure RON parser for /etc/asusd/fan_curves.ron content. cpu_en/gpu_en
 * (optional) receive the profile's enabled flags, true when absent. */
int fan_ron_parse(const char *text, fan_preset_t preset, fan_curve_t *cpu,
                  fan_curve_t *gpu, bool *cpu_en, bool *gpu_en);

#endif /* CTRON_FAN_H */

