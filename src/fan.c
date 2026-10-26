#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "fan.h"
#include "hw.h"
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

int fan_enable_raw(int on)
{
    return on ? 1 : 2;
}

int fan_raw_is_on(int raw)
{
    return raw == 1;
}

/* Compiled-in snapshot of THIS machine's asusd state (FA608PP,
 * /etc/asusd/fan_curves.ron as of 2026-10-17). Provenance (probed
 * 2026-10-18): asusd's own regenerated defaults are GENERIC fallbacks
 * (every profile disabled, a 255 °C sentinel point), so this ron —
 * which these tables match exactly — is the best available factory
 * reference, most plausibly seeded from the EC by asusd at first
 * init. Residual caveat: asusd rewrites the file on every curve
 * change and ctron's own fan writes persist here too, so edits made
 * on this machine before 2026-10-17 may be baked in. Used only when
 * asusd is absent. */
static const int S_SNAPSHOT_QUIET_CPU_T[FAN_POINTS] = { 38, 63, 66, 69, 71, 76, 79, 105 };
static const int S_SNAPSHOT_QUIET_CPU_P[FAN_POINTS] = { 22, 25, 35, 43, 50, 60, 85, 100 };
static const int S_SNAPSHOT_QUIET_GPU_T[FAN_POINTS] = { 60, 62, 63, 64, 65, 68, 70, 105 };
static const int S_SNAPSHOT_QUIET_GPU_P[FAN_POINTS] = { 53, 53, 53, 53, 75, 75, 105, 105 };

static const int S_SNAPSHOT_BAL_CPU_T[FAN_POINTS]   = { 40, 54, 61, 67, 71, 75, 80, 90 };
static const int S_SNAPSHOT_BAL_CPU_P[FAN_POINTS]   = { 22, 75, 80, 98, 125, 160, 190, 210 };
static const int S_SNAPSHOT_BAL_GPU_T[FAN_POINTS]   = { 51, 53, 59, 64, 66, 70, 74, 83 };
static const int S_SNAPSHOT_BAL_GPU_P[FAN_POINTS]   = { 33, 53, 73, 93, 140, 180, 215, 245 };

static const int S_SNAPSHOT_PERF_CPU_T[FAN_POINTS]  = { 38, 62, 71, 79, 83, 89, 96, 105 };
static const int S_SNAPSHOT_PERF_CPU_P[FAN_POINTS]  = { 47, 105, 135, 163, 205, 230, 255, 255 };
static const int S_SNAPSHOT_PERF_GPU_T[FAN_POINTS]  = { 58, 62, 71, 76, 84, 91, 98, 105 };
static const int S_SNAPSHOT_PERF_GPU_P[FAN_POINTS]  = { 78, 113, 158, 193, 210, 220, 230, 255 };

const char *fan_preset_name(fan_preset_t p)
{
    switch (p) {
    case FAN_PRESET_QUIET:    return "Quiet";
    case FAN_PRESET_BALANCED: return "Balanced";
    case FAN_PRESET_PERF:     return "Performance";
    case FAN_PRESET_ACTIVE:   return "Active (Live)";
    default:                  return "Unknown";
    }
}

static int parse_fan_block(const char *block, const char *fan_tag, fan_curve_t *fc,
                           bool *en)
{
    if (!fc || !block || !fan_tag)
        return -1;
    const char *p = strstr(block, fan_tag);
    if (!p)
        return -1;
    const char *next_fan = strstr(p + strlen(fan_tag), "fan:");
    const char *pwm_str = strstr(p, "pwm:");
    const char *temp_str = strstr(p, "temp:");
    if (!pwm_str || !temp_str)
        return -1;
    if (next_fan && pwm_str > next_fan)
        return -1;
    if (next_fan && temp_str > next_fan)
        return -1;

    /* "enabled: true/false" belongs to this fan's block when it sits
     * between the tag and the next "fan:" (absent = true, asusd always
     * writes it) */
    if (en) {
        *en = true;
        const char *e = strstr(p, "enabled:");
        if (e && (!next_fan || e < next_fan)) {
            const char *v = e + strlen("enabled:");
            while (*v == ' ')
                v++;
            if (!strncmp(v, "false", 5))
                *en = false;
        }
    }

    const char *po = strchr(pwm_str, '(');
    const char *to = strchr(temp_str, '(');
    if (!po || !to)
        return -1;
    po++;
    to++;

    int pwms[FAN_POINTS], temps[FAN_POINTS];
    int np = ut_parse_ints(po, pwms, FAN_POINTS);
    int nt = ut_parse_ints(to, temps, FAN_POINTS);
    int n = np < nt ? np : nt;
    if (n < 1)
        return -1;
    fc->n = n;
    for (int i = 0; i < n; i++) {
        int tc = temps[i], pc = pwms[i];
        clamp_point(&tc, &pc);
        fc->temp_c[i] = tc;
        fc->pwm[i] = pc;
    }
    fan_normalize(fc, 0);
    return 0;
}

int fan_ron_parse(const char *text, fan_preset_t preset, fan_curve_t *cpu,
                  fan_curve_t *gpu, bool *cpu_en, bool *gpu_en)
{
    if (!text)
        return -1;
    const char *tag = NULL;
    switch (preset) {
    case FAN_PRESET_QUIET:    tag = "quiet:"; break;
    case FAN_PRESET_BALANCED: tag = "balanced:"; break;
    case FAN_PRESET_PERF:     tag = "performance:"; break;
    default:                  return -1;
    }

    const char *start = strstr(text, tag);
    if (!start && preset == FAN_PRESET_PERF)
        start = strstr(text, "perf:");
    if (!start)
        return -1;

    const char *b_open = strchr(start, '[');
    if (!b_open)
        return -1;
    const char *b_close = strchr(b_open, ']');
    if (!b_close)
        return -1;

    size_t len = (size_t)(b_close - b_open + 1);
    char *block = malloc(len + 1);
    if (!block)
        return -1;
    memcpy(block, b_open, len);
    block[len] = '\0';

    int rc = -1;
    if (cpu_en)
        *cpu_en = true;
    if (gpu_en)
        *gpu_en = true;
    if (cpu) {
        if (parse_fan_block(block, "fan: CPU", cpu, cpu_en) == 0)
            rc = 0;
    }
    if (gpu) {
        if (parse_fan_block(block, "fan: GPU", gpu, gpu_en) == 0)
            rc = 0;
    }
    free(block);
    return rc;
}

/* Pure built-in snapshot fill — no file or hardware access. */
int fan_preset_builtin(fan_preset_t preset, fan_curve_t *cpu, fan_curve_t *gpu)
{
    const int *ct = NULL, *cp = NULL, *gt = NULL, *gp = NULL;
    switch (preset) {
    case FAN_PRESET_QUIET:
        ct = S_SNAPSHOT_QUIET_CPU_T; cp = S_SNAPSHOT_QUIET_CPU_P;
        gt = S_SNAPSHOT_QUIET_GPU_T; gp = S_SNAPSHOT_QUIET_GPU_P;
        break;
    case FAN_PRESET_BALANCED:
        ct = S_SNAPSHOT_BAL_CPU_T; cp = S_SNAPSHOT_BAL_CPU_P;
        gt = S_SNAPSHOT_BAL_GPU_T; gp = S_SNAPSHOT_BAL_GPU_P;
        break;
    case FAN_PRESET_PERF:
        ct = S_SNAPSHOT_PERF_CPU_T; cp = S_SNAPSHOT_PERF_CPU_P;
        gt = S_SNAPSHOT_PERF_GPU_T; gp = S_SNAPSHOT_PERF_GPU_P;
        break;
    default:
        return -1;
    }

    if (cpu && ct && cp) {
        cpu->n = FAN_POINTS;
        for (int i = 0; i < FAN_POINTS; i++) {
            cpu->temp_c[i] = ct[i];
            cpu->pwm[i] = cp[i];
        }
    }
    if (gpu && gt && gp) {
        gpu->n = FAN_POINTS;
        for (int i = 0; i < FAN_POINTS; i++) {
            gpu->temp_c[i] = gt[i];
            gpu->pwm[i] = gp[i];
        }
    }
    return 0;
}

int fan_preset_get(hw_state_t *hw, fan_preset_t preset, fan_curve_t *cpu,
                   fan_curve_t *gpu, fan_preset_info_t *info)
{
    if (info) {
        info->src = FAN_PSRC_DEFAULT;
        info->cpu_en = true;
        info->gpu_en = true;
    }

    if (preset == FAN_PRESET_ACTIVE) {
        if (hw && hw_fan_hwmon_read_live(hw, cpu, gpu) == 0) {
            if (info)
                info->src = FAN_PSRC_HWMON;
            return 0;
        }
        if (hw) {
            if (cpu) *cpu = hw->fan_cpu_stock;
            if (gpu) *gpu = hw->fan_gpu_stock;
            if (info)
                info->src = FAN_PSRC_STOCK;
            return 0;
        }
        if (cpu) fan_default(cpu);
        if (gpu) fan_default(gpu);
        return 0; /* info stays DEFAULT */
    }

    /* Try asusd fan_curves.ron first */
    char buf[8192];
    if (ut_read_file_all("/etc/asusd/fan_curves.ron", buf, sizeof(buf)) == 0) {
        bool c_en = true, g_en = true;
        if (fan_ron_parse(buf, preset, cpu, gpu, &c_en, &g_en) == 0) {
            if (info) {
                info->src = FAN_PSRC_RON;
                info->cpu_en = c_en;
                info->gpu_en = g_en;
            }
            return 0;
        }
    }

    /* Built-in snapshot fallback */
    if (info)
        info->src = FAN_PSRC_BUILTIN;
    return fan_preset_builtin(preset, cpu, gpu);
}

