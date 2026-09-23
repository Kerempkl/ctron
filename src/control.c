#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "control.h"
#include "daeboard.h"
#include "util.h"
#include "settings.h"
#include "display/display.h"

#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define NB_WMI "/sys/devices/platform/asus-nb-wmi"
#define ARMOURY_ATTR "/sys/class/firmware-attributes/asus-armoury/attributes"

/* ---- aura tables ------------------------------------------------------ */

const char *const AURA_EFFECTS[] = {
    "static", "breathe", "rainbow-cycle", "rainbow-wave",
    "pulse", "comet", "flash", "stars",
    "rain", "highlight", "laser", "ripple"
};
const int AURA_EFFECT_COUNT = (int)(sizeof(AURA_EFFECTS) / sizeof(AURA_EFFECTS[0]));

const char *const AURA_COLOR_NAMES[] = {
    "Ice", "Red", "Green", "Blue", "Yellow", "Purple", "Orange", "White"
};
const char *const AURA_COLOR_HEX[] = {
    "00ffff", "ff0000", "00ff00", "0055ff", "ffff00", "ff00ff", "ff7700", "ffffff"
};
const int AURA_COLOR_COUNT = (int)(sizeof(AURA_COLOR_NAMES) / sizeof(AURA_COLOR_NAMES[0]));

int ctrl_aura_effect_idx(const char *name)
{
    if (!name)
        return -1;
    for (int i = 0; i < AURA_EFFECT_COUNT; i++)
        if (!strcasecmp(name, AURA_EFFECTS[i]))
            return i;
    return -1;
}

/* ---- small helpers ---------------------------------------------------- */

static int nbwmi_write(const char *attr, int v)
{
    char path[300], val[24];
    snprintf(path, sizeof(path), NB_WMI "/%s", attr);
    snprintf(val, sizeof(val), "%d", v);
    return ut_priv_write(path, val);
}

static bool use_asusctl_first(void)
{
    return g_prefs.write_pref == 0;
}

/* amd-pstate gives every CPU its own cpufreq policy (related_cpus holds a
 * single member each), so a limit must be written to all of them. Writing
 * cpu0 alone left 31 cores clamped at base on the FA608PP. */
static int cpufreq_write_all(const char *leaf, const char *val)
{
    int rc = -1;
    for (int i = 0; i < 1024; i++) {
        char path[300];
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/%s", i, leaf);
        if (!ut_path_exists(path))
            break;
        if (ut_priv_write(path, val) == 0)
            rc = 0; /* keep going: one bad node must not stop the rest */
    }
    return rc;
}

/* ---- platform --------------------------------------------------------- */

int ctrl_set_profile(hw_state_t *hw, hw_profile_t p)
{
    if (p < 0 || p >= HW_PROF_COUNT)
        return -1;
    const char *name = hw_profile_name(p);
    int rc = -1;

    if (hw->has_asusctl && use_asusctl_first()) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "asusctl profile set %s", name);
        rc = ut_exec(cmd, NULL, 0);
    }
    if (rc != 0) {
        const char *acpi = (p == HW_QUIET) ? "quiet"
                         : (p == HW_PERFORMANCE) ? "performance"
                         : "balanced";
        rc = ut_priv_write("/sys/firmware/acpi/platform_profile", acpi);
    }
    if (rc == 0) {
        hw->profile = p;
        ut_log("profile: %s", name);
    } else {
        ut_log("profile: FAILED (need asusctl or passwordless sudo)");
    }
    return rc;
}

int ctrl_set_epp(hw_state_t *hw, hw_epp_t e)
{
    if (e < 0 || e >= HW_EPP_COUNT)
        return -1;
    int rc = cpufreq_write_all("energy_performance_preference", hw_epp_name(e));
    if (rc == 0) {
        hw->epp = e;
        ut_log("epp: %s", hw_epp_name(e));
    } else {
        ut_log("epp: FAILED (needs root; no passwordless sudo)");
    }
    return rc;
}

int ctrl_set_cpu_max_mhz(hw_state_t *hw, int mhz)
{
    mhz = ut_clamp_i(mhz, hw->cpu_mhz_min, hw->cpu_mhz_max);
    char val[24];
    snprintf(val, sizeof(val), "%d", mhz * 1000);
    int rc = cpufreq_write_all("scaling_max_freq", val);
    if (rc == 0) {
        hw->cpu_mhz_limit = mhz;
        ut_log("cpu max: %d MHz", mhz);
    } else {
        ut_log("cpu max: FAILED (needs root; no passwordless sudo)");
    }
    return rc;
}

int ctrl_set_cpu_boost(hw_state_t *hw, bool on)
{
    int rc = ut_priv_write("/sys/devices/system/cpu/cpufreq/boost", on ? "1" : "0");
    if (rc == 0) {
        hw->cpu_boost = on;
        ut_log("cpu boost: %s", on ? "on" : "off");
    } else {
        ut_log("cpu boost: FAILED");
    }
    return rc;
}

/* ---- power ------------------------------------------------------------ */

void ctrl_ppt_limits(const hw_state_t *hw,
                     int *spl_min, int *spl_max,
                     int *sppt_min, int *sppt_max,
                     int *fppt_min, int *fppt_max)
{
    (void)hw;
    /* Try firmware-attributes limits first. */
    struct { const char *attr; int *min, *max; int dmin, dmax; } req[] = {
        { "ppt_pl1_spl",  spl_min,  spl_max,  15,  90 },
        { "ppt_pl2_sppt", sppt_min, sppt_max, 35, 120 },
        { "ppt_pl3_fppt", fppt_min, fppt_max, 35, 120 },
    };
    for (size_t i = 0; i < sizeof(req) / sizeof(req[0]); i++) {
        char path[384], buf[32];
        *req[i].min = req[i].dmin;
        *req[i].max = req[i].dmax;
        snprintf(path, sizeof(path), "%s/%s/min", ARMOURY_ATTR, req[i].attr);
        if (ut_read_file(path, buf, sizeof(buf)) == 0 && atoi(buf) > 0)
            *req[i].min = atoi(buf);
        snprintf(path, sizeof(path), "%s/%s/max", ARMOURY_ATTR, req[i].attr);
        if (ut_read_file(path, buf, sizeof(buf)) == 0 && atoi(buf) > 0)
            *req[i].max = atoi(buf);
    }
}

void ctrl_ppt_order(int *spl, int *sppt, int *fppt,
                    int smin, int smax, int pmin, int pmax, int fmin, int fmax)
{
    *spl  = ut_clamp_i(*spl,  smin, smax);
    *sppt = ut_clamp_i(*sppt, pmin, pmax);
    *fppt = ut_clamp_i(*fppt, fmin, fmax);
    if (*sppt < *spl)
        *sppt = *spl;
    if (*fppt < *sppt)
        *fppt = *sppt;
}

int ctrl_set_ppt(hw_state_t *hw, int spl, int sppt, int fppt)
{
    int smin, smax, pmin, pmax, fmin, fmax;
    ctrl_ppt_limits(hw, &smin, &smax, &pmin, &pmax, &fmin, &fmax);
    ctrl_ppt_order(&spl, &sppt, &fppt, smin, smax, pmin, pmax, fmin, fmax);

    int rc = 0;
    if (nbwmi_write("ppt_pl1_spl", spl) != 0)  rc = -1;
    if (nbwmi_write("ppt_pl2_sppt", sppt) != 0) rc = -1;
    if (nbwmi_write("ppt_fppt", fppt) != 0)    rc = -1;

    if (rc == 0) {
        hw->ppt_spl = spl;
        hw->ppt_sppt = sppt;
        hw->ppt_fppt = fppt;
        hw->ppt_off = false;
        ut_log("ppt: %d/%d/%d W", spl, sppt, fppt);
    } else {
        ut_log("ppt: FAILED (needs root; no passwordless sudo)");
    }
    return rc;
}

int ctrl_ppt_off(hw_state_t *hw)
{
    if (!hw->ppt_off) {
        hw->ppt_saved_spl  = hw->ppt_spl;
        hw->ppt_saved_sppt = hw->ppt_sppt;
        hw->ppt_saved_fppt = hw->ppt_fppt;
    }

    int smin, smax, pmin, pmax, fmin, fmax;
    ctrl_ppt_limits(hw, &smin, &smax, &pmin, &pmax, &fmin, &fmax);
    int rc = 0;
    if (nbwmi_write("ppt_pl1_spl", smax) != 0)   rc = -1;
    if (nbwmi_write("ppt_pl2_sppt", pmax) != 0)  rc = -1;
    if (nbwmi_write("ppt_fppt", fmax) != 0)      rc = -1;

    if (rc == 0) {
        hw->ppt_spl = smax;
        hw->ppt_sppt = pmax;
        hw->ppt_fppt = fmax;
        hw->ppt_off = true;
        ut_log("ppt: limits removed (%d/%d/%d W maxima)", smax, pmax, fmax);
    } else {
        ut_log("ppt: removing limits FAILED (needs root)");
    }
    return rc;
}

int ctrl_ppt_restore(hw_state_t *hw)
{
    if (hw->ppt_saved_spl <= 0 && hw->ppt_saved_sppt <= 0 && hw->ppt_saved_fppt <= 0) {
        hw->ppt_off = false;
        ut_log("ppt: no previous values — pick a preset (Q45/B60/P80)");
        return 0;
    }
    int rc = ctrl_set_ppt(hw,
                          hw->ppt_saved_spl  > 0 ? hw->ppt_saved_spl  : 45,
                          hw->ppt_saved_sppt > 0 ? hw->ppt_saved_sppt : 55,
                          hw->ppt_saved_fppt > 0 ? hw->ppt_saved_fppt : 55);
    if (rc == 0)
        hw->ppt_off = false;
    return rc;
}

int ctrl_set_nv_boost(hw_state_t *hw, int watts)
{
    watts = ut_clamp_i(watts, 5, 25);
    int rc = nbwmi_write("nv_dynamic_boost", watts);
    if (rc == 0) {
        hw->nv_boost = watts;
        ut_log("nv dynamic boost: %d W", watts);
    } else {
        ut_log("nv dynamic boost: FAILED");
    }
    return rc;
}

int ctrl_set_nv_temp(hw_state_t *hw, int celsius)
{
    celsius = ut_clamp_i(celsius, 75, 87);
    int rc = nbwmi_write("nv_temp_target", celsius);
    if (rc == 0) {
        hw->nv_temp = celsius;
        ut_log("nv temp target: %d°C", celsius);
    } else {
        ut_log("nv temp target: FAILED");
    }
    return rc;
}

int ctrl_set_panel_od(hw_state_t *hw, bool on)
{
    int rc = nbwmi_write("panel_od", on ? 1 : 0);
    if (rc == 0) {
        hw->panel_od = on;
        ut_log("panel overdrive: %s", on ? "on" : "off");
    } else {
        ut_log("panel overdrive: FAILED");
    }
    return rc;
}

int ctrl_set_battery_limit(hw_state_t *hw, int pct)
{
    pct = ut_clamp_i(pct, 20, 100);
    int rc = -1;

    if (hw->has_asusctl && use_asusctl_first()) {
        char cmd[96];
        snprintf(cmd, sizeof(cmd), "asusctl battery limit %d", pct);
        rc = ut_exec(cmd, NULL, 0);
    }
    if (rc != 0) {
        glob_t g;
        if (glob("/sys/class/power_supply/BAT*/charge_control_end_threshold",
                 0, NULL, &g) == 0) {
            for (size_t i = 0; i < g.gl_pathc && rc != 0; i++) {
                char val[8];
                snprintf(val, sizeof(val), "%d", pct);
                rc = ut_priv_write(g.gl_pathv[i], val);
            }
            globfree(&g);
        }
    }
    if (rc == 0) {
        hw->bat_limit = pct;
        ut_log("battery charge limit: %d%%", pct);
    } else {
        ut_log("battery charge limit: FAILED");
    }
    return rc;
}

int ctrl_battery_oneshot(void)
{
    int rc = ut_exec("asusctl battery oneshot", NULL, 0);
    ut_log("battery oneshot: %s", rc == 0 ? "ok" : "failed (asusctl only)");
    return rc;
}

/* ---- display ---------------------------------------------------------- */

int ctrl_set_hz(hw_state_t *hw, int hz)
{
    const display_ops_t *d = display_get();
    if (!d) {
        ut_log("display: no backend for this session");
        return -1;
    }
    int want = display_nearest_hz(hz);
    if (d->set_hz(want) == 0) {
        hw->hz_cur = want;
        ut_log("display: %d Hz (%s)", want, d->name);
        return 0;
    }
    ut_log("display: set %d Hz FAILED", want);
    return -1;
}

/* ---- keyboard / aura --------------------------------------------------- */

int ctrl_set_kbd(hw_state_t *hw, hw_kbd_t lvl)
{
    if (lvl < 0 || lvl >= HW_KBD_COUNT)
        return -1;
    if (db_up()) {
        if (db_set_brightness((int)lvl) == 0) {
            hw->kbd = lvl;
            ut_log("kbd backlight: %s via daeboard", hw_kbd_name(lvl));
            return 0;
        }
        ut_log("kbd backlight: daeboard refused");
        return -1;
    }
    int rc = -1;

    if (hw->has_asusctl && use_asusctl_first()) {
        char cmd[96];
        snprintf(cmd, sizeof(cmd), "asusctl leds set %s", hw_kbd_name(lvl));
        rc = ut_exec(cmd, NULL, 0);
    }
    if (rc != 0)
        rc = ut_priv_write("/sys/class/leds/asus::kbd_backlight/brightness",
                           hw_kbd_name(lvl));
    if (rc == 0) {
        hw->kbd = lvl;
        ut_log("kbd backlight: %s", hw_kbd_name(lvl));
    } else {
        ut_log("kbd backlight: FAILED");
    }
    return rc;
}

int ctrl_set_aura(hw_state_t *hw, int effect_idx, int color_idx)
{
    if (effect_idx < 0 || effect_idx >= AURA_EFFECT_COUNT)
        return -1;
    if (color_idx < 0 || color_idx >= AURA_COLOR_COUNT)
        color_idx = 0;

    const char *effect = AURA_EFFECTS[effect_idx];
    const char *hex = AURA_COLOR_HEX[color_idx];

    if (db_up()) {
        if (!strcmp(effect, "static")) {
            if (db_set_color(hex) == 0) {
                ut_log("aura: static via daeboard");
                return 0;
            }
            ut_log("aura: daeboard color failed");
            return -1;
        }
        ut_log("aura: %s stays with daeboard macros, not asusctl", effect);
        return -1;
    }

    if (!hw->has_asusctl) {
        ut_log("aura: asusctl not available");
        return -1;
    }
    char cmd[192];

    if (!strcmp(effect, "static") || !strcmp(effect, "pulse") ||
        !strcmp(effect, "comet") || !strcmp(effect, "flash")) {
        snprintf(cmd, sizeof(cmd), "asusctl aura effect %s -c %s", effect, hex);
    } else if (!strcmp(effect, "highlight") || !strcmp(effect, "laser") ||
               !strcmp(effect, "ripple")) {
        snprintf(cmd, sizeof(cmd), "asusctl aura effect %s -c %s --speed med", effect, hex);
    } else if (!strcmp(effect, "breathe") || !strcmp(effect, "stars")) {
        snprintf(cmd, sizeof(cmd),
                 "asusctl aura effect %s --colour %s --colour2 000000 --speed med",
                 effect, hex);
    } else if (!strcmp(effect, "rainbow-cycle") || !strcmp(effect, "rain")) {
        snprintf(cmd, sizeof(cmd), "asusctl aura effect %s --speed med", effect);
    } else if (!strcmp(effect, "rainbow-wave")) {
        snprintf(cmd, sizeof(cmd),
                 "asusctl aura effect rainbow-wave --direction right --speed med");
    } else {
        snprintf(cmd, sizeof(cmd), "asusctl aura effect %s -c %s", effect, hex);
    }

    int rc = ut_exec(cmd, NULL, 0);
    ut_log("aura: %s (%s) %s", effect, AURA_COLOR_NAMES[color_idx],
           rc == 0 ? "ok" : "FAILED");
    return rc == 0 ? 0 : -1;
}

int ctrl_set_aura_hex(hw_state_t *hw, const char *hex)
{
    if (!hex || strlen(hex) < 6 || strspn(hex, "0123456789abcdefABCDEF") != strlen(hex))
        return -1;
    if (db_up()) {
        if (db_set_color(hex) == 0) {
            ut_log("aura: static #%s via daeboard", hex);
            return 0;
        }
        ut_log("aura: daeboard color failed");
        return -1;
    }
    if (!hw->has_asusctl) {
        ut_log("aura: asusctl not available");
        return -1;
    }
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "asusctl aura effect static -c %s", hex);
    int rc = ut_exec(cmd, NULL, 0);
    ut_log("aura: static #%s %s", hex, rc == 0 ? "ok" : "FAILED");
    return rc == 0 ? 0 : -1;
}

/* ---- fans ------------------------------------------------------------- */

static int fan_curve_base(char *out, size_t n)
{
    return hw_hwmon_path("asus_custom_fan_curve", out, n);
}

/* "30c:40,45c:90,..." for asusctl fan-curve --data. */
static void fan_curve_data_str(const fan_curve_t *fc, char *out, size_t n)
{
    size_t off = 0;
    out[0] = '\0';
    int cnt = fc->n > 0 ? fc->n : 0;
    for (int i = 0; i < cnt; i++) {
        int w = snprintf(out + off, n - off, "%s%dc:%d",
                         i ? "," : "", fc->temp_c[i], fc->pwm[i]);
        if (w < 0 || (size_t)w >= n - off)
            break;
        off += (size_t)w;
    }
}

int ctrl_fan_write(hw_state_t *hw)
{
    char base[256];
    int rc = 0;

    if (fan_curve_base(base, sizeof(base)) == 0) {
        for (int i = 0; i < FAN_POINTS; i++) {
            char path[300];
            /* pad with the last point when fewer are in use */
            int ic = (hw->fan_cpu.n > 0) ? (i < hw->fan_cpu.n ? i : hw->fan_cpu.n - 1) : 0;
            int ig = (hw->fan_gpu.n > 0) ? (i < hw->fan_gpu.n ? i : hw->fan_gpu.n - 1) : 0;
            int tc = (hw->fan_cpu.n > 0) ? hw->fan_cpu.temp_c[ic] : FAN_TMIN;
            int pc = (hw->fan_cpu.n > 0) ? hw->fan_cpu.pwm[ic] : 0;
            int tg = (hw->fan_gpu.n > 0) ? hw->fan_gpu.temp_c[ig] : FAN_TMIN;
            int pg = (hw->fan_gpu.n > 0) ? hw->fan_gpu.pwm[ig] : 0;
            char val[16];

            snprintf(path, sizeof(path), "%s/pwm1_auto_point%d_temp", base, i + 1);
            snprintf(val, sizeof(val), "%d", tc);
            if (ut_priv_write(path, val) != 0) rc = -1;

            snprintf(path, sizeof(path), "%s/pwm1_auto_point%d_pwm", base, i + 1);
            snprintf(val, sizeof(val), "%d", pc);
            if (ut_priv_write(path, val) != 0) rc = -1;

            snprintf(path, sizeof(path), "%s/pwm2_auto_point%d_temp", base, i + 1);
            snprintf(val, sizeof(val), "%d", tg);
            if (ut_priv_write(path, val) != 0) rc = -1;

            snprintf(path, sizeof(path), "%s/pwm2_auto_point%d_pwm", base, i + 1);
            snprintf(val, sizeof(val), "%d", pg);
            if (ut_priv_write(path, val) != 0) rc = -1;
        }
        char path[300], val[8];
        snprintf(path, sizeof(path), "%s/pwm1_enable", base);
        snprintf(val, sizeof(val), "%d", hw->fan_cpu_on ? 2 : 0);
        ut_priv_write(path, val);
        snprintf(path, sizeof(path), "%s/pwm2_enable", base);
        snprintf(val, sizeof(val), "%d", hw->fan_gpu_on ? 2 : 0);
        ut_priv_write(path, val);
    }

    /* asusctl persistence: survive asusd restarts */
    if (hw->has_asusctl) {
        const char *prof = hw_profile_name(hw->profile);
        char data[256], cmd[512];
        fan_curve_data_str(&hw->fan_cpu, data, sizeof(data));
        snprintf(cmd, sizeof(cmd),
                 "asusctl fan-curve --mod-profile %s --fan cpu --data '%s'", prof, data);
        ut_exec(cmd, NULL, 0);
        snprintf(cmd, sizeof(cmd),
                 "asusctl fan-curve --mod-profile %s --enable-fan-curve %s --fan cpu",
                 prof, hw->fan_cpu_on ? "true" : "false");
        ut_exec(cmd, NULL, 0);
        fan_curve_data_str(&hw->fan_gpu, data, sizeof(data));
        snprintf(cmd, sizeof(cmd),
                 "asusctl fan-curve --mod-profile %s --fan gpu --data '%s'", prof, data);
        ut_exec(cmd, NULL, 0);
        snprintf(cmd, sizeof(cmd),
                 "asusctl fan-curve --mod-profile %s --enable-fan-curve %s --fan gpu",
                 prof, hw->fan_gpu_on ? "true" : "false");
        ut_exec(cmd, NULL, 0);
    }

    if (rc == 0)
        ut_log("fan curves written (cpu %s, gpu %s)",
               hw->fan_cpu_on ? "on" : "off", hw->fan_gpu_on ? "on" : "off");
    else
        ut_log("fan curves: sysfs write FAILED (needs root; no passwordless sudo)");
    return rc;
}

int ctrl_fan_set_enabled(hw_state_t *hw, bool cpu_on, bool gpu_on)
{
    hw->fan_cpu_on = cpu_on;
    hw->fan_gpu_on = gpu_on;
    return ctrl_fan_write(hw);
}

int ctrl_fan_preset(hw_state_t *hw, int preset)
{
    switch (preset) {
    case 1: /* silent */
        fan_scale(&hw->fan_cpu, &hw->fan_cpu_stock, 6, 10);
        fan_scale(&hw->fan_gpu, &hw->fan_gpu_stock, 6, 10);
        break;
    case 2: /* cool */
        fan_scale(&hw->fan_cpu, &hw->fan_cpu_stock, 13, 10);
        fan_scale(&hw->fan_gpu, &hw->fan_gpu_stock, 13, 10);
        hw->fan_cpu.pwm[FAN_POINTS - 1] = 255;
        hw->fan_gpu.pwm[FAN_POINTS - 1] = 255;
        break;
    case 3: /* full */
        hw->fan_cpu = hw->fan_cpu_stock;
        hw->fan_gpu = hw->fan_gpu_stock;
        for (int i = 4; i < FAN_POINTS; i++) {
            hw->fan_cpu.pwm[i] = 255;
            hw->fan_gpu.pwm[i] = 255;
        }
        break;
    default: /* stock */
        hw->fan_cpu = hw->fan_cpu_stock;
        hw->fan_gpu = hw->fan_gpu_stock;
        break;
    }
    hw->fan_cpu_on = true;
    hw->fan_gpu_on = true;
    return ctrl_fan_write(hw);
}
