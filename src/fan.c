#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "fan.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fan_default(fan_curve_t *fc)
{
    static const int t[FAN_POINTS] = { 30, 40, 55, 65, 75, 85, 95, 105 };
    static const int p[FAN_POINTS] = { 25, 40, 80, 120, 160, 200, 240, 255 };
    fc->n = FAN_POINTS;
    for (int i = 0; i < FAN_POINTS; i++) {
        fc->temp_c[i] = t[i];
        fc->pwm[i] = p[i];
    }
}

static void clamp_point(int *temp, int *pwm)
{
    if (*temp < FAN_TMIN) *temp = FAN_TMIN;
    if (*temp > FAN_TMAX) *temp = FAN_TMAX;
    if (*pwm < 0) *pwm = 0;
    if (*pwm > 255) *pwm = 255;
}

/* Insertion sort by temperature, then force strictly increasing temps.
 * Returns the new index of `keep_idx`. */
static int fan_normalize(fan_curve_t *fc, int keep_idx)
{
    int n = fc->n;
    if (n < 0) {
        fc->n = 0;
        return 0;
    }
    if (n > FAN_POINTS)
        n = fc->n = FAN_POINTS;
    if (n < 2) {
        if (keep_idx < 0) return 0;
        if (keep_idx >= n) return n ? n - 1 : 0;
        return keep_idx;
    }

    int order[FAN_POINTS];
    for (int i = 0; i < n; i++)
        order[i] = i;
    for (int i = 1; i < n; i++) {
        int oi = order[i];
        int j = i;
        while (j > 0 && fc->temp_c[order[j - 1]] > fc->temp_c[oi]) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = oi;
    }

    int nt[FAN_POINTS], np[FAN_POINTS], new_keep = 0;
    for (int i = 0; i < n; i++) {
        nt[i] = fc->temp_c[order[i]];
        np[i] = fc->pwm[order[i]];
        if (order[i] == keep_idx)
            new_keep = i;
    }
    for (int i = 0; i < n; i++) {
        fc->temp_c[i] = nt[i];
        fc->pwm[i] = np[i];
    }

    /* strictly increasing temps within [FAN_TMIN, FAN_TMAX] */
    for (int i = 1; i < n; i++) {
        if (fc->temp_c[i] <= fc->temp_c[i - 1])
            fc->temp_c[i] = fc->temp_c[i - 1] + 1;
    }
    if (fc->temp_c[n - 1] > FAN_TMAX) {
        fc->temp_c[n - 1] = FAN_TMAX;
        for (int i = n - 2; i >= 0; i--) {
            if (fc->temp_c[i] >= fc->temp_c[i + 1])
                fc->temp_c[i] = fc->temp_c[i + 1] - 1;
            if (fc->temp_c[i] < FAN_TMIN)
                fc->temp_c[i] = FAN_TMIN;
        }
        for (int i = 1; i < n; i++) {
            if (fc->temp_c[i] <= fc->temp_c[i - 1])
                fc->temp_c[i] = fc->temp_c[i - 1] + 1;
        }
    }
    return new_keep;
}

int fan_from_csv(fan_curve_t *fc, const char *temps, const char *pwms)
{
    int t[FAN_POINTS], p[FAN_POINTS];
    int nt = ut_parse_ints(temps, t, FAN_POINTS);
    int np = ut_parse_ints(pwms, p, FAN_POINTS);
    int n = nt < np ? nt : np;
    if (n < 1)
        return -1;
    fc->n = n;
    for (int i = 0; i < n; i++) {
        int tc = t[i], pc = p[i];
        clamp_point(&tc, &pc);
        fc->temp_c[i] = tc;
        fc->pwm[i] = pc;
    }
    fan_normalize(fc, 0);
    return fc->n;
}

void fan_to_csv(const fan_curve_t *fc, char *t, size_t tn, char *p, size_t pn)
{
    size_t to = 0, po = 0;
    if (t && tn) t[0] = '\0';
    if (p && pn) p[0] = '\0';
    int n = (fc && fc->n > 0 && fc->n <= FAN_POINTS) ? fc->n : 0;
    for (int i = 0; i < n; i++) {
        int w;
        if (t && tn > to) {
            w = snprintf(t + to, tn - to, "%s%d", i ? "," : "", fc->temp_c[i]);
            if (w > 0)
                to += (size_t)w;
        }
        if (p && pn > po) {
            w = snprintf(p + po, pn - po, "%s%d", i ? "," : "", fc->pwm[i]);
            if (w > 0)
                po += (size_t)w;
        }
    }
}

int fan_add_point(fan_curve_t *fc, int temp, int pwm)
{
    if (fc->n < 0)
        fc->n = 0;
    if (fc->n >= FAN_POINTS)
        return -1;

    if (temp < 0) {
        /* auto-placement in the widest gap */
        if (fc->n == 0) {
            temp = 40;
            pwm = 80;
        } else {
            int best_i = -1, best_gap = fc->temp_c[0] - FAN_TMIN;
            for (int i = 0; i < fc->n - 1; i++) {
                int g = fc->temp_c[i + 1] - fc->temp_c[i];
                if (g > best_gap) {
                    best_gap = g;
                    best_i = i;
                }
            }
            int tail = FAN_TMAX - fc->temp_c[fc->n - 1];
            if (tail > best_gap) {
                temp = (fc->temp_c[fc->n - 1] + FAN_TMAX) / 2;
                pwm = fc->pwm[fc->n - 1];
            } else if (best_i < 0) {
                temp = (FAN_TMIN + fc->temp_c[0]) / 2;
                pwm = fc->pwm[0];
            } else {
                temp = (fc->temp_c[best_i] + fc->temp_c[best_i + 1]) / 2;
                pwm = (fc->pwm[best_i] + fc->pwm[best_i + 1]) / 2;
            }
        }
    }
    clamp_point(&temp, &pwm);
    fc->temp_c[fc->n] = temp;
    fc->pwm[fc->n] = pwm;
    fc->n++;
    return fan_normalize(fc, fc->n - 1);
}

int fan_del_point(fan_curve_t *fc, int idx)
{
    if (fc->n < 1)
        return -1;
    if (idx < 0 || idx >= fc->n)
        idx = fc->n - 1;
    for (int i = idx; i < fc->n - 1; i++) {
        fc->temp_c[i] = fc->temp_c[i + 1];
        fc->pwm[i] = fc->pwm[i + 1];
    }
    fc->n--;
    return idx < fc->n ? idx : fc->n - 1;
}

int fan_nudge_point(fan_curve_t *fc, int idx, int dtemp, int dpwm)
{
    if (fc->n < 1 || idx < 0 || idx >= fc->n)
        return -1;
    int t = fc->temp_c[idx] + dtemp;
    int p = fc->pwm[idx] + dpwm;
    clamp_point(&t, &p);
    fc->temp_c[idx] = t;
    fc->pwm[idx] = p;
    return fan_normalize(fc, idx);
}

int fan_set_point(fan_curve_t *fc, int idx, int temp, int pwm)
{
    if (fc->n < 1)
        return fan_add_point(fc, temp, pwm);
    if (idx < 0 || idx >= fc->n)
        idx = fc->n - 1;
    clamp_point(&temp, &pwm);
    fc->temp_c[idx] = temp;
    fc->pwm[idx] = pwm;
    return fan_normalize(fc, idx);
}

void fan_scale(fan_curve_t *dst, const fan_curve_t *src, int num, int den)
{
    *dst = *src;
    if (dst->n < 1)
        dst->n = FAN_POINTS;
    for (int i = 0; i < dst->n && i < FAN_POINTS; i++) {
        int p = src->pwm[i] * num / den;
        dst->pwm[i] = ut_clamp_i(p, 0, 255);
    }
}
