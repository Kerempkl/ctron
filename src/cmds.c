#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "cmds.h"
#include "control.h"
#include "util.h"
#include "display/display.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

int cmd_parse_bool(const char *s)
{
    if (!s)
        return -1;
    if (!strcasecmp(s, "on") || !strcasecmp(s, "1") || !strcasecmp(s, "true"))
        return 1;
    if (!strcasecmp(s, "off") || !strcasecmp(s, "0") || !strcasecmp(s, "false"))
        return 0;
    return -1;
}

static void errf(char *err, size_t errn, const char *fmt, ...)
{
    if (!err || errn == 0)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, errn, fmt, ap);
    va_end(ap);
}

/* PPT token: Q45 | B60 | P80 | off | on | spl,sppt,fppt */
static int cmd_ppt(hw_state_t *hw, const char *val, char *err, size_t errn)
{
    if (!strcasecmp(val, "Q45"))
        return ctrl_set_ppt(hw, 45, 55, 55);
    if (!strcasecmp(val, "B60"))
        return ctrl_set_ppt(hw, 60, 75, 75);
    if (!strcasecmp(val, "P80"))
        return ctrl_set_ppt(hw, 80, 80, 80);
    if (!strcasecmp(val, "off"))
        return ctrl_ppt_off(hw);
    if (!strcasecmp(val, "on"))
        return ctrl_ppt_restore(hw);
    int a, b, c;
    if (sscanf(val, "%d,%d,%d", &a, &b, &c) == 3)
        return ctrl_set_ppt(hw, a, b, c);
    errf(err, errn, "ppt: expected Q45|B60|P80|off|on or spl,sppt,fppt");
    return -1;
}

/* "cpu 40,60 80,120" or "gpu 40,60 80,120" */
static int cmd_fan_curve(hw_state_t *hw, const char *val, char *err, size_t errn)
{
    char which[8] = {0}, temps[128] = {0}, pwms[128] = {0};
    int n = sscanf(val, "%7s %127s %127s", which, temps, pwms);
    if (n != 3 || (strcasecmp(which, "cpu") && strcasecmp(which, "gpu"))) {
        errf(err, errn, "fan-curve: expected 'cpu|gpu <temps> <pwms>'");
        return -1;
    }
    fan_curve_t *fc = strcasecmp(which, "gpu") ? &hw->fan_cpu : &hw->fan_gpu;
    if (fan_from_csv(fc, temps, pwms) < 0) {
        errf(err, errn, "fan-curve: bad csv");
        return -1;
    }
    return 0;
}

/* "static", "static red", "static ffaabb" */
static int cmd_aura(hw_state_t *hw, const char *val, char *err, size_t errn)
{
    char eff[32] = {0}, col[32] = {0};
    if (sscanf(val, "%31s %31s", eff, col) < 1) {
        errf(err, errn, "aura: expected effect [color|hex]");
        return -1;
    }
    int e = ctrl_aura_effect_idx(eff);
    if (e < 0) {
        errf(err, errn, "aura: unknown effect '%s'", eff);
        return -1;
    }
    if (!col[0])
        return ctrl_set_aura(hw, e, 0);
    /* hex? */
    if (strspn(col, "0123456789abcdefABCDEF") == strlen(col) && strlen(col) == 6)
        return ctrl_set_aura_hex(hw, col);
    for (int i = 0; i < AURA_COLOR_COUNT; i++) {
        if (!strcasecmp(col, AURA_COLOR_NAMES[i]))
            return ctrl_set_aura(hw, e, i);
    }
    errf(err, errn, "aura: unknown color '%s'", col);
    return -1;
}

static int cmd_hz(hw_state_t *hw, const char *val, char *err, size_t errn)
{
    int hz;
    if (!strcasecmp(val, "max")) {
        hz = 0;
        for (int i = 0; i < hw->hz_count; i++)
            if (hw->hz_modes[i] > hz)
                hz = hw->hz_modes[i];
        if (hz == 0)
            hz = hw->hz_cur > 0 ? hw->hz_cur : 60;
    } else {
        hz = atoi(val);
    }
    if (hz < 30 || hz > 500) {
        errf(err, errn, "hz: bad value '%s'", val);
        return -1;
    }
    return ctrl_set_hz(hw, hz);
}

int cmd_run(hw_state_t *hw, const char *key, const char *val,
            char *err, size_t errn)
{
    if (!key)
        return -1;
    if (err && errn)
        err[0] = '\0';
    if (!val)
        val = "";

    if (!strcasecmp(key, "profile")) {
        int p = hw_profile_from_name(val);
        if (p < 0) {
            errf(err, errn, "profile: quiet|balanced|performance");
            return -1;
        }
        return ctrl_set_profile(hw, (hw_profile_t)p);
    }
    if (!strcasecmp(key, "epp")) {
        int e = hw_epp_from_name(val);
        if (e < 0) {
            errf(err, errn, "epp: power|balance_power|balance_performance|performance");
            return -1;
        }
        return ctrl_set_epp(hw, (hw_epp_t)e);
    }
    if (!strcasecmp(key, "freq")) {
        int mhz = atoi(val);
        if (mhz < 100 || mhz > 10000) {
            errf(err, errn, "freq: MHz value");
            return -1;
        }
        return ctrl_set_cpu_max_mhz(hw, mhz);
    }
    if (!strcasecmp(key, "hz"))
        return cmd_hz(hw, val, err, errn);
    if (!strcasecmp(key, "battery")) {
        int pct = atoi(val);
        if (pct < 20 || pct > 100) {
            errf(err, errn, "battery: 20..100");
            return -1;
        }
        return ctrl_set_battery_limit(hw, pct);
    }
    if (!strcasecmp(key, "battery-oneshot"))
        return ctrl_battery_oneshot();
    if (!strcasecmp(key, "ppt"))
        return cmd_ppt(hw, val, err, errn);
    if (!strcasecmp(key, "nv-boost")) {
        int w = atoi(val);
        if (w < 5 || w > 25) {
            errf(err, errn, "nv-boost: 5..25 W");
            return -1;
        }
        return ctrl_set_nv_boost(hw, w);
    }
    if (!strcasecmp(key, "nv-temp")) {
        int t = atoi(val);
        if (t < 75 || t > 87) {
            errf(err, errn, "nv-temp: 75..87 °C");
            return -1;
        }
        return ctrl_set_nv_temp(hw, t);
    }
    if (!strcasecmp(key, "panel-od")) {
        int on = cmd_parse_bool(val);
        if (on < 0) {
            errf(err, errn, "panel-od: on|off");
            return -1;
        }
        return ctrl_set_panel_od(hw, on != 0);
    }
    if (!strcasecmp(key, "cpu-boost")) {
        int on = cmd_parse_bool(val);
        if (on < 0) {
            errf(err, errn, "cpu-boost: on|off");
            return -1;
        }
        return ctrl_set_cpu_boost(hw, on != 0);
    }
    if (!strcasecmp(key, "kbd")) {
        int k = hw_kbd_from_name(val);
        if (k < 0) {
            errf(err, errn, "kbd: off|low|med|high");
            return -1;
        }
        return ctrl_set_kbd(hw, (hw_kbd_t)k);
    }
    if (!strcasecmp(key, "aura"))
        return cmd_aura(hw, val, err, errn);
    if (!strcasecmp(key, "fan")) {
        if (!strcasecmp(val, "stock"))
            return ctrl_fan_preset(hw, 0);
        if (!strcasecmp(val, "silent"))
            return ctrl_fan_preset(hw, 1);
        if (!strcasecmp(val, "cool"))
            return ctrl_fan_preset(hw, 2);
        if (!strcasecmp(val, "full"))
            return ctrl_fan_preset(hw, 3);
        if (!strcasecmp(val, "on"))
            return ctrl_fan_set_enabled(hw, true, true);
        if (!strcasecmp(val, "off"))
            return ctrl_fan_set_enabled(hw, false, false);
        errf(err, errn, "fan: stock|silent|cool|full|on|off");
        return -1;
    }
    if (!strcasecmp(key, "fan-curve"))
        return cmd_fan_curve(hw, val, err, errn);
    if (!strcasecmp(key, "fan-write"))
        return ctrl_fan_write(hw);

    errf(err, errn, "unknown key '%s'", key);
    return -1;
}
