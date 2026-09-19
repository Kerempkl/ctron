#ifndef CTRON_FAN_H
#define CTRON_FAN_H

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

#endif /* CTRON_FAN_H */
