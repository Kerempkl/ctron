#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "modes.h"
#include "cmds.h"
#include "settings.h"
#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

const char *modes_path(char *out, size_t n)
{
    return settings_modes_file(out, n);
}

static const struct { const char *name, *steps; } default_modes[] = {
    { "turbo",      "profile performance, ppt P80, fan cool, hz max" },
    { "performance","profile performance, fan stock, hz max" },
    { "balanced",   "profile balanced, fan stock" },
    { "quiet",      "profile quiet, fan stock, epp balance_power" },
    { "silent",     "profile quiet, ppt Q45, fan silent, hz 60, epp power" },
};

void modes_write_defaults(void)
{
    char path[500];
    modes_path(path, sizeof(path));
    if (ut_path_exists(path))
        return;

    char dir[460];
    settings_dir(dir, sizeof(dir));
    ut_mkdir_p(dir);

    FILE *f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "# ctron modes — single-command setting bundles.\n");
    fprintf(f, "# Applied with: ctron --mode <name>  (also shown in TUI Settings).\n");
    for (size_t i = 0; i < sizeof(default_modes) / sizeof(default_modes[0]); i++) {
        fprintf(f, "\n[%s]\nsteps = %s\n", default_modes[i].name, default_modes[i].steps);
    }
    fclose(f);
}

int modes_load(mode_def_t *arr, int max)
{
    modes_write_defaults(); /* seeds the file on the very first call */

    char path[500];
    modes_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;

    int n = 0;
    char line[512], section[MODE_NAME_MAX] = {0};
    while (fgets(line, sizeof(line), f) && n < max) {
        char *s = ut_trim(line);
        if (*s == '#' || *s == ';' || !*s)
            continue;
        if (*s == '[') {
            char *end = strchr(s, ']');
            if (end) {
                *end = '\0';
                snprintf(section, sizeof(section), "%s", s + 1);
            }
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq || !section[0])
            continue;
        *eq = '\0';
        char *key = ut_trim(s);
        char *val = ut_trim(eq + 1);
        if (strcasecmp(key, "steps") != 0)
            continue;

        /* new entry (or fill the section found earlier) */
        int idx = mode_find(arr, n, section);
        if (idx < 0 && n < max) {
            idx = n++;
            memset(&arr[idx], 0, sizeof(arr[idx]));
            snprintf(arr[idx].name, MODE_NAME_MAX, "%s", section);
        }
        if (idx >= 0)
            snprintf(arr[idx].steps, MODE_STEPS_MAX, "%s", val);
    }
    fclose(f);
    return n;
}

int modes_save(const mode_def_t *arr, int n)
{
    char path[500];
    modes_path(path, sizeof(path));
    char dir[460];
    settings_dir(dir, sizeof(dir));
    ut_mkdir_p(dir);

    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fprintf(f, "# ctron modes — single-command setting bundles.\n");
    fprintf(f, "# Applied with: ctron --mode <name>  (also shown in TUI Settings).\n");
    for (int i = 0; i < n; i++) {
        if (!arr[i].name[0])
            continue;
        fprintf(f, "\n[%s]\nsteps = %s\n", arr[i].name, arr[i].steps);
    }
    fclose(f);
    return 0;
}

int mode_find(const mode_def_t *arr, int n, const char *name)
{
    for (int i = 0; i < n; i++)
        if (!strcasecmp(arr[i].name, name))
            return i;
    return -1;
}

/* ---- applied-mode tracking --------------------------------------------- */

/* step key → tracked-field bits; 0 = untracked (aura, asusd auto
 * rows, flush-style commands) */
static unsigned key_to_mask(const char *key, const char *val)
{
    if (!strcasecmp(key, "profile"))       return MS_PROFILE;
    if (!strcasecmp(key, "epp"))           return MS_EPP;
    if (!strcasecmp(key, "ppt"))           return MS_PPT;
    if (!strcasecmp(key, "hz"))            return MS_HZ;
    if (!strcasecmp(key, "battery"))       return MS_BAT;
    if (!strcasecmp(key, "kbd"))           return MS_KBD;
    if (!strcasecmp(key, "cpu-boost"))     return MS_BOOST;
    if (!strcasecmp(key, "panel-od"))      return MS_OD;
    if (!strcasecmp(key, "nv-boost"))      return MS_NVB;
    if (!strcasecmp(key, "nv-temp"))       return MS_NVT;
    if (!strcasecmp(key, "freq"))          return MS_FREQ;
    if (!strcasecmp(key, "gpu-clock"))     return MS_GPUCLOCK;
    if (!strcasecmp(key, "fan"))           return MS_FAN_CPU | MS_FAN_GPU;
    if (!strcasecmp(key, "fan-curve")) {
        char side[8] = {0};
        sscanf(val, "%7s", side);
        if (!strcasecmp(side, "cpu"))      return MS_FAN_CPU;
        if (!strcasecmp(side, "gpu"))      return MS_FAN_GPU;
        return 0;
    }
    return 0;
}

static int step_starts_key(const char *s)
{
    char key[32];
    int i = 0;

    if (!s)
        return 0;
    while (s[i] && s[i] != ' ' && s[i] != '\t' && s[i] != ',' &&
           i < (int)sizeof(key) - 1) {
        key[i] = s[i];
        i++;
    }
    if (i == 0)
        return 0;
    key[i] = '\0';
    return cmd_is_key(key);
}

/* Comma that begins the next command, or NULL when this is the last step. */
static const char *step_sep(const char *start)
{
    const char *c = strchr(start, ',');
    while (c) {
        const char *n = c + 1;
        while (*n == ' ' || *n == '\t')
            n++;
        if (*n == '\0' || step_starts_key(n))
            return c;
        c = strchr(c + 1, ',');
    }
    return NULL;
}

int mode_split_steps(const char *steps, char (*out)[MODE_STEPS_MAX], int max)
{
    const char *p = steps ? steps : "";
    int n = 0;

    while (*p) {
        const char *end;
        size_t len;

        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == ',') {
            p++;
            continue;
        }
        if (!*p)
            break;
        end = step_sep(p);
        len = end ? (size_t)(end - p) : strlen(p);
        while (len && (p[len - 1] == ' ' || p[len - 1] == '\t'))
            len--;
        if (out && n < max) {
            if (len >= MODE_STEPS_MAX)
                len = MODE_STEPS_MAX - 1;
            memcpy(out[n], p, len);
            out[n][len] = '\0';
        }
        n++;
        if (!end)
            break;
        p = end + 1;
    }
    return n;
}

static void step_key_val(const char *step, char *key, size_t keyn, const char **val)
{
    const char *sp = strchr(step, ' ');
    size_t kn = 0;

    if (sp)
        kn = (size_t)(sp - step);
    else
        kn = strlen(step);
    if (kn >= keyn)
        kn = keyn - 1;
    memcpy(key, step, kn);
    key[kn] = '\0';
    *val = sp ? sp + 1 : "";
}

unsigned mode_touch_mask(const char *steps)
{
    char got[32][MODE_STEPS_MAX];
    int n = mode_split_steps(steps, got, 32);
    unsigned mask = 0;

    if (n > 32)
        n = 32;
    for (int i = 0; i < n; i++) {
        char key[32];
        const char *val;
        step_key_val(got[i], key, sizeof(key), &val);
        mask |= key_to_mask(key, ut_trim((char *)val));
    }
    return mask;
}

unsigned mode_snapshot(const hw_state_t *hw, unsigned want, mode_snap_t *snap)
{
    unsigned kept = 0;

    memset(snap, 0, sizeof(*snap));
    if (want & MS_PROFILE) { kept |= MS_PROFILE; snap->profile = hw->profile; }
    if (want & MS_EPP)     { kept |= MS_EPP;     snap->epp = hw->epp; }
    if (want & MS_PPT) {
        /* a stale read (0) cannot be tracked: drop the whole triple */
        if (hw->ppt_spl > 0 && hw->ppt_sppt > 0 && hw->ppt_fppt > 0) {
            kept |= MS_PPT;
            snap->spl = hw->ppt_spl;
            snap->sppt = hw->ppt_sppt;
            snap->fppt = hw->ppt_fppt;
        }
    }
    if (want & MS_HZ) {
        if (hw->hz_cur > 0) { kept |= MS_HZ; snap->hz = hw->hz_cur; }
    }
    if (want & MS_BAT) {
        if (hw->bat_limit > 0) { kept |= MS_BAT; snap->bat = hw->bat_limit; }
    }
    if (want & MS_KBD)   { kept |= MS_KBD;   snap->kbd = hw->kbd; }
    if (want & MS_BOOST) { kept |= MS_BOOST; snap->boost = hw->cpu_boost; }
    if (want & MS_OD)    { kept |= MS_OD;    snap->od = hw->panel_od; }
    if (want & MS_NVB) {
        if (hw->nv_boost > 0) { kept |= MS_NVB; snap->nvb = hw->nv_boost; }
    }
    if (want & MS_NVT) {
        if (hw->nv_temp > 0) { kept |= MS_NVT; snap->nvt = hw->nv_temp; }
    }
    if (want & MS_FREQ) {
        if (hw->cpu_mhz_limit > 0) { kept |= MS_FREQ; snap->mhz = hw->cpu_mhz_limit; }
    }
    if (want & MS_GPUCLOCK) { kept |= MS_GPUCLOCK; snap->gclock = hw->gpu_clock_lock; }
    if (want & MS_FAN_CPU) {
        if (hw->fan_cpu.n > 0) {
            kept |= MS_FAN_CPU;
            snap->fan_cpu = hw->fan_cpu;
        }
    }
    if (want & MS_FAN_GPU) {
        if (hw->fan_gpu.n > 0) {
            kept |= MS_FAN_GPU;
            snap->fan_gpu = hw->fan_gpu;
        }
    }
    return kept;
}

static int fan_curve_differs(const fan_curve_t *a, const fan_curve_t *b)
{
    if (a->n != b->n)
        return 1;
    for (int i = 0; i < a->n && i < FAN_POINTS; i++)
        if (a->temp_c[i] != b->temp_c[i] || a->pwm[i] != b->pwm[i])
            return 1;
    return 0;
}

int mode_drift_count(const hw_state_t *hw, unsigned mask,
                     const mode_snap_t *snap)
{
    int drift = 0;

    if (mask & MS_PROFILE) {
        if ((int)hw->profile != snap->profile)
            drift++;
    }
    if (mask & MS_EPP) {
        if ((int)hw->epp != snap->epp)
            drift++;
    }
    if (mask & MS_PPT) {
        /* unknown/stale live values (0) are not drift */
        if (hw->ppt_spl > 0 && hw->ppt_sppt > 0 && hw->ppt_fppt > 0 &&
            (hw->ppt_spl != snap->spl || hw->ppt_sppt != snap->sppt ||
             hw->ppt_fppt != snap->fppt))
            drift++;
    }
    if (mask & MS_HZ) {
        if (hw->hz_cur > 0 && hw->hz_cur != snap->hz)
            drift++;
    }
    if (mask & MS_BAT) {
        if (hw->bat_limit > 0 && hw->bat_limit != snap->bat)
            drift++;
    }
    if (mask & MS_KBD) {
        if ((int)hw->kbd != snap->kbd)
            drift++;
    }
    if (mask & MS_BOOST) {
        if (hw->cpu_boost != snap->boost)
            drift++;
    }
    if (mask & MS_OD) {
        if (hw->panel_od != snap->od)
            drift++;
    }
    if (mask & MS_NVB) {
        if (hw->nv_boost > 0 && hw->nv_boost != snap->nvb)
            drift++;
    }
    if (mask & MS_NVT) {
        if (hw->nv_temp > 0 && hw->nv_temp != snap->nvt)
            drift++;
    }
    if (mask & MS_FREQ) {
        if (hw->cpu_mhz_limit > 0 && hw->cpu_mhz_limit != snap->mhz)
            drift++;
    }
    if (mask & MS_GPUCLOCK) {
        if (hw->gpu_clock_lock != snap->gclock)
            drift++;
    }
    if (mask & MS_FAN_CPU) {
        if (fan_curve_differs(&hw->fan_cpu, &snap->fan_cpu))
            drift++;
    }
    if (mask & MS_FAN_GPU) {
        if (fan_curve_differs(&hw->fan_gpu, &snap->fan_gpu))
            drift++;
    }
    return drift;
}

int mode_apply(hw_state_t *hw, const char *steps, char *err, size_t errn)
{
    char got[32][MODE_STEPS_MAX];
    int n = mode_split_steps(steps, got, 32);
    if (n > 32) {
        if (err && errn)
            snprintf(err, errn, "too many steps");
        return -1;
    }

    for (int i = 0; i < n; i++) {
        char *step = got[i];
        if (!*step)
            continue;

        char key[32];
        const char *val;
        step_key_val(step, key, sizeof(key), &val);
        val = ut_trim((char *)val);

        char serr[128];
        if (cmd_run(hw, key, val, serr, sizeof(serr)) != 0) {
            if (err && errn)
                snprintf(err, errn, "%s", serr[0] ? serr : "step failed");
            ut_log("mode step FAILED: %s", step);
            return -1;
        }
    }
    return 0;
}
