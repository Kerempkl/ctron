#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hw.h"
#include "util.h"
#include "display/display.h"

#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define NB_WMI "/sys/devices/platform/asus-nb-wmi"
#define ARMOURY_ATTR "/sys/class/firmware-attributes/asus-armoury/attributes"

/* ---- name tables ----------------------------------------------------- */

const char *hw_profile_name(hw_profile_t p)
{
    switch (p) {
    case HW_QUIET:       return "Quiet";
    case HW_BALANCED:    return "Balanced";
    case HW_PERFORMANCE: return "Performance";
    default:             return "Unknown";
    }
}

int hw_profile_from_name(const char *s)
{
    if (!s) return -1;
    if (!strcasecmp(s, "quiet"))    return HW_QUIET;
    if (!strcasecmp(s, "balanced")) return HW_BALANCED;
    if (!strcasecmp(s, "performance") || !strcasecmp(s, "perf"))
        return HW_PERFORMANCE;
    return -1;
}

const char *hw_epp_name(hw_epp_t e)
{
    switch (e) {
    case HW_EPP_POWER:     return "power";
    case HW_EPP_BAL_POWER: return "balance_power";
    case HW_EPP_BAL_PERF:  return "balance_performance";
    case HW_EPP_PERF:      return "performance";
    default:               return "balance_performance";
    }
}

int hw_epp_from_name(const char *s)
{
    if (!s) return -1;
    if (!strcasecmp(s, "power"))                return HW_EPP_POWER;
    if (!strcasecmp(s, "balance_power"))        return HW_EPP_BAL_POWER;
    if (!strcasecmp(s, "balance_performance"))  return HW_EPP_BAL_PERF;
    if (!strcasecmp(s, "performance"))          return HW_EPP_PERF;
    return -1;
}

const char *hw_kbd_name(hw_kbd_t k)
{
    switch (k) {
    case HW_KBD_OFF:  return "off";
    case HW_KBD_LOW:  return "low";
    case HW_KBD_MED:  return "med";
    case HW_KBD_HIGH: return "high";
    default:          return "unknown";
    }
}

int hw_kbd_from_name(const char *s)
{
    if (!s) return -1;
    if (!strcasecmp(s, "off"))  return HW_KBD_OFF;
    if (!strcasecmp(s, "low"))  return HW_KBD_LOW;
    if (!strcasecmp(s, "med"))  return HW_KBD_MED;
    if (!strcasecmp(s, "high")) return HW_KBD_HIGH;
    return -1;
}

/* ---- helpers ---------------------------------------------------------- */

int hw_hwmon_path(const char *want, char *out, size_t n)
{
    glob_t g;
    if (glob("/sys/class/hwmon/hwmon*", 0, NULL, &g) != 0)
        return -1;
    int rc = -1;
    for (size_t i = 0; i < g.gl_pathc; i++) {
        char np[300], name[64] = {0};
        snprintf(np, sizeof(np), "%s/name", g.gl_pathv[i]);
        if (ut_read_file(np, name, sizeof(name)) != 0)
            continue;
        if (!strcmp(name, want)) {
            snprintf(out, n, "%s", g.gl_pathv[i]);
            rc = 0;
            break;
        }
    }
    globfree(&g);
    return rc;
}

int hw_armoury_read(const char *attr, char *out, size_t n)
{
    char path[384];
    snprintf(path, sizeof(path), "%s/%s/current_value", ARMOURY_ATTR, attr);
    if (ut_read_file(path, out, n) == 0 && out[0])
        return 0;
    return -1;
}

/* Find the first battery/adapter of a type. */
static int power_supply_find(const char *type, char *out, size_t n)
{
    glob_t g;
    if (glob("/sys/class/power_supply/*", 0, NULL, &g) != 0)
        return -1;
    int rc = -1;
    for (size_t i = 0; i < g.gl_pathc; i++) {
        char tp[300], t[32] = {0};
        snprintf(tp, sizeof(tp), "%s/type", g.gl_pathv[i]);
        if (ut_read_file(tp, t, sizeof(t)) != 0)
            continue;
        if (!strcmp(t, type)) {
            snprintf(out, n, "%s", g.gl_pathv[i]);
            rc = 0;
            break;
        }
    }
    globfree(&g);
    return rc;
}

/* nb-wmi node read; 0/5 are the well-known stale cache values. */
static int nbwmi_read(const char *attr)
{
    char path[300];
    snprintf(path, sizeof(path), NB_WMI "/%s", attr);
    return ut_read_int(path);
}

static int ppt_from_node(const char *attr)
{
    int v = nbwmi_read(attr);
    if (v <= 5)
        return -1; /* unknown or stale kernel cache */
    return v;
}

/* Battery power. ASUS reports current_now as a positive magnitude and
 * puts the direction in `status`, so the sign is applied here.
 * power_now is µW; otherwise µA * µV. */
static void read_bat_power(hw_state_t *hw, const char *bat)
{
    char path[340];
    long long uw = 0;
    bool have = false;

    hw->bat_mw_known = false;
    hw->bat_mw = 0;

    if (ut_path_join(path, sizeof(path), bat, "power_now") == 0) {
        int v = ut_read_int(path);
        if (v >= 0) {
            uw = v;
            have = true;
        }
    }
    if (!have &&
        ut_path_join(path, sizeof(path), bat, "current_now") == 0) {
        int ua = ut_read_int(path);
        int uv = -1;
        if (ut_path_join(path, sizeof(path), bat, "voltage_now") == 0)
            uv = ut_read_int(path);
        if (ua >= 0 && uv > 0) {
            uw = (long long)ua * (long long)uv / 1000000LL; /* µW */
            have = true;
        }
    }
    if (!have)
        return;

    int mag = (int)(uw / 1000LL); /* mW */
    if (mag < 0)
        mag = 0;
    if (!strcasecmp(hw->bat_status, "Charging"))
        hw->bat_mw = -mag;
    else if (!strcasecmp(hw->bat_status, "Discharging"))
        hw->bat_mw = mag;
    else
        hw->bat_mw = 0;
    hw->bat_mw_known = true;
}

void hw_fmt_power(char *out, size_t n, bool known, int mw)
{
    if (!out || n == 0)
        return;
    if (!known) {
        snprintf(out, n, "--");
        return;
    }
    const char *tag;
    int mag;
    if (mw >= 100) {
        tag = "dis";
        mag = mw;
    } else if (mw <= -100) {
        tag = "chg";
        mag = -mw;
    } else {
        snprintf(out, n, "0.0W");
        return;
    }
    int tenths = (mag + 50) / 100;
    if (tenths < 1)
        tenths = 1;
    snprintf(out, n, "%s %d.%dW", tag, tenths / 10, tenths % 10);
}

/* ---- fan curve hwmon -------------------------------------------------- */

static void fan_read_one(const char *base, const char *pwm, fan_curve_t *fc)
{
    char p[300];
    fc->n = FAN_POINTS;
    bool any = false;
    for (int i = 0; i < FAN_POINTS; i++) {
        int t = -1, w = -1;
        snprintf(p, sizeof(p), "%s/%s_auto_point%d_temp", base, pwm, i + 1);
        t = ut_read_int(p);
        snprintf(p, sizeof(p), "%s/%s_auto_point%d_pwm", base, pwm, i + 1);
        w = ut_read_int(p);
        if (t > 0 && w >= 0)
            any = true;
        fc->temp_c[i] = t > 0 ? t : FAN_TMIN;
        fc->pwm[i] = w >= 0 ? w : 0;
    }
    if (!any)
        fan_default(fc); /* empty table (never written): editor seed */
}

static void fan_hwmon_read(hw_state_t *hw)
{
    char base[256];
    if (hw_hwmon_path("asus_custom_fan_curve", base, sizeof(base)) != 0) {
        hw->has_fan_curve = false;
        if (hw->fan_cpu.n == 0) {
            fan_default(&hw->fan_cpu);
            fan_default(&hw->fan_gpu);
        }
        return;
    }
    hw->has_fan_curve = true;
    fan_read_one(base, "pwm1", &hw->fan_cpu);
    fan_read_one(base, "pwm2", &hw->fan_gpu);
    char p[300];
    snprintf(p, sizeof(p), "%s/pwm1_enable", base);
    hw->fan_cpu_on = (ut_read_int(p) == 2);
    snprintf(p, sizeof(p), "%s/pwm2_enable", base);
    hw->fan_gpu_on = (ut_read_int(p) == 2);
}

/* ---- GPU temperature (cached) ---------------------------------------- */

static int gpu_temp_cached(void)
{
    static int cached = -1;
    static time_t last = 0;
    time_t now = time(NULL);
    if (now - last < 2)
        return cached;

    char out[32] = {0};
    if (ut_exec("nvidia-smi --query-gpu=temperature.gpu --format=csv,noheader,nounits",
                out, sizeof(out)) == 0) {
        int t = atoi(out);
        if (t > 0 && t < 130)
            cached = t;
        else
            cached = -1;
    } else {
        cached = -1;
    }
    last = now;
    return cached;
}

/* ---- profile / epp / kbd reads --------------------------------------- */

static void read_profile(hw_state_t *hw)
{
    char buf[128] = {0};
    if (ut_read_file("/sys/firmware/acpi/platform_profile", buf, sizeof(buf)) == 0) {
        if (strstr(buf, "quiet"))          { hw->profile = HW_QUIET; return; }
        if (strstr(buf, "balanced"))       { hw->profile = HW_BALANCED; return; }
        if (strstr(buf, "performance"))    { hw->profile = HW_PERFORMANCE; return; }
    }
    if (hw->has_asusctl &&
        ut_exec("asusctl profile get", buf, sizeof(buf)) == 0) {
        if (strstr(buf, "Quiet"))          { hw->profile = HW_QUIET; return; }
        if (strstr(buf, "Balanced"))       { hw->profile = HW_BALANCED; return; }
        if (strstr(buf, "Performance"))    { hw->profile = HW_PERFORMANCE; return; }
    }
}

static void read_epp(hw_state_t *hw)
{
    char buf[64] = {0};
    if (ut_read_file("/sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference",
                     buf, sizeof(buf)) != 0)
        return;
    int e = hw_epp_from_name(buf);
    if (e >= 0)
        hw->epp = (hw_epp_t)e;
}

static void read_kbd(hw_state_t *hw)
{
    int v = ut_read_int("/sys/class/leds/asus::kbd_backlight/brightness");
    if (v >= 0 && v < HW_KBD_COUNT)
        hw->kbd = (hw_kbd_t)v;
}

/* ---- init ------------------------------------------------------------- */

void hw_init(hw_state_t *hw)
{
    memset(hw, 0, sizeof(*hw));
    hw->cpu_temp = -1;
    hw->gpu_temp = -1;
    hw->rpm_cpu = -1;
    hw->rpm_gpu = -1;

    char vendor[64] = {0}, prod[64] = {0};
    ut_read_file("/sys/devices/virtual/dmi/id/sys_vendor", vendor, sizeof(vendor));
    ut_read_file("/sys/devices/virtual/dmi/id/product_name", prod, sizeof(prod));
    hw->is_asus = (strstr(vendor, "ASUS") != NULL);
    snprintf(hw->model, sizeof(hw->model), "%s", prod[0] ? prod : "Unknown laptop");

    FILE *f = fopen("/proc/cpuinfo", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "model name", 10) == 0) {
                char *colon = strchr(line, ':');
                if (colon) {
                    colon++;
                    while (*colon == ' ' || *colon == '\t')
                        colon++;
                    colon[strcspn(colon, "\r\n")] = '\0';
                    snprintf(hw->cpu, sizeof(hw->cpu), "%s", colon);
                }
                break;
            }
        }
        fclose(f);
    }
    if (!hw->cpu[0])
        snprintf(hw->cpu, sizeof(hw->cpu), "Unknown CPU");

    hw->has_asusctl    = ut_have_cmd("asusctl");
    hw->has_armoury    = ut_path_exists("/sys/class/firmware-attributes/asus-armoury");
    hw->has_nvidia_smi = ut_have_cmd("nvidia-smi");
    hw->has_kbd_led    = ut_path_exists("/sys/class/leds/asus::kbd_backlight/brightness");

    int min_khz = ut_read_int("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_min_freq");
    int max_khz = ut_read_int("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
    hw->cpu_mhz_min = min_khz > 0 ? min_khz / 1000 : 400;
    hw->cpu_mhz_max = max_khz > 0 ? max_khz / 1000 : 5500;
    hw->cpu_mhz_limit = hw->cpu_mhz_max;

    fan_hwmon_read(hw);
    hw->fan_cpu_stock = hw->fan_cpu;
    hw->fan_gpu_stock = hw->fan_gpu;

    hw->ppt_spl  = ppt_from_node("ppt_pl1_spl");
    hw->ppt_sppt = ppt_from_node("ppt_pl2_sppt");
    hw->ppt_fppt = ppt_from_node("ppt_fppt");
    int v = nbwmi_read("nv_dynamic_boost");
    hw->nv_boost = (v >= 5 && v <= 90) ? v : 0;
    v = nbwmi_read("nv_temp_target");
    hw->nv_temp = (v >= 70 && v <= 95) ? v : 0;
    hw->panel_od = (nbwmi_read("panel_od") == 1);
    hw->cpu_boost = (ut_read_int("/sys/devices/system/cpu/cpufreq/boost") != 0);

    read_profile(hw);
    read_epp(hw);
    read_kbd(hw);

    /* display */
    const display_ops_t *d = display_get();
    if (d) {
        hw->hz_cur = d->current_hz();
        hw->hz_count = d->modes_hz(hw->hz_modes, HZ_MAX_MODES);
    }

    ut_log("hw init: %s", hw->model);
}

/* ---- fast poll -------------------------------------------------------- */

/* amd-pstate re-negotiates per-core ceilings with the firmware and they
 * move on their own (battery, thermals, CPPC re-evaluation — observed
 * 2401↔5386 MHz within seconds, per core). Take the widest window
 * across policies so one clamped core (often cpu0, the only one we
 * used to read) does not cap the display and the staging clamps. */
static int cpu_present_count(void)
{
    char buf[64];
    if (ut_read_file("/sys/devices/system/cpu/present", buf, sizeof(buf)) != 0)
        return 1;
    int lo, hi;
    if (sscanf(buf, "%d-%d", &lo, &hi) == 2)
        return hi - lo + 1;
    return 1;
}

static void cpu_limits_sweep(hw_state_t *hw)
{
    int n = cpu_present_count();
    int min_khz = 0, max_khz = 0, lim_khz = 0;
    for (int i = 0; i < n; i++) {
        char p[96];
        int v;
        snprintf(p, sizeof p,
                 "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_min_freq", i);
        v = ut_read_int(p);
        if (v > 0 && (min_khz == 0 || v < min_khz))
            min_khz = v;
        snprintf(p, sizeof p,
                 "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
        v = ut_read_int(p);
        if (v > max_khz)
            max_khz = v;
        snprintf(p, sizeof p,
                 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", i);
        v = ut_read_int(p);
        if (v > lim_khz)
            lim_khz = v;
    }
    if (min_khz > 0)
        hw->cpu_mhz_min = min_khz / 1000;
    if (max_khz > 0)
        hw->cpu_mhz_max = max_khz / 1000;
    if (lim_khz > 0)
        hw->cpu_mhz_limit = lim_khz / 1000;
}

void hw_refresh_fast(hw_state_t *hw)
{
    char p[320], q[320];

    cpu_limits_sweep(hw);

    if (hw_hwmon_path("k10temp", p, sizeof(p)) == 0 &&
        ut_path_join(q, sizeof(q), p, "temp1_input") == 0) {
        int t = ut_read_int(q);
        if (t > 0)
            hw->cpu_temp = t / 1000;
    }

    int cur = ut_read_int("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    if (cur > 0)
        hw->cpu_mhz_cur = cur / 1000;

    if (hw->has_nvidia_smi)
        hw->gpu_temp = gpu_temp_cached();

    char bat[256];
    if (power_supply_find("Battery", bat, sizeof(bat)) == 0) {
        if (ut_path_join(p, sizeof(p), bat, "capacity") == 0) {
            int pct = ut_read_int(p);
            if (pct >= 0)
                hw->bat_pct = pct;
        }
        char st[16] = {0};
        if (ut_path_join(p, sizeof(p), bat, "status") == 0)
            ut_read_file(p, st, sizeof(st));
        if (st[0])
            snprintf(hw->bat_status, sizeof(hw->bat_status), "%s", st);
        read_bat_power(hw, bat);
    }

    char ac[256];
    if (power_supply_find("Mains", ac, sizeof(ac)) == 0 &&
        ut_path_join(p, sizeof(p), ac, "online") == 0)
        hw->ac_online = (ut_read_int(p) == 1);

    if (hw_hwmon_path("asus", p, sizeof(p)) == 0) {
        hw->has_fan_rpm = true;
        int r1 = -1, r2 = -1;
        if (ut_path_join(q, sizeof(q), p, "fan1_input") == 0)
            r1 = ut_read_int(q);
        if (ut_path_join(q, sizeof(q), p, "fan2_input") == 0)
            r2 = ut_read_int(q);
        hw->rpm_cpu = r1 > 0 ? r1 : -1;
        hw->rpm_gpu = r2 > 0 ? r2 : -1;
    } else {
        hw->has_fan_rpm = false;
    }
}

/* ---- full snapshot ---------------------------------------------------- */

void hw_refresh_live(hw_state_t *hw)
{
    hw_refresh_fast(hw);

    fan_hwmon_read(hw);

    hw->ppt_spl  = ppt_from_node("ppt_pl1_spl");
    hw->ppt_sppt = ppt_from_node("ppt_pl2_sppt");
    hw->ppt_fppt = ppt_from_node("ppt_fppt");
    int v = nbwmi_read("nv_dynamic_boost");
    if (v >= 5 && v <= 90)
        hw->nv_boost = v;
    v = nbwmi_read("nv_temp_target");
    if (v >= 70 && v <= 95)
        hw->nv_temp = v;
    v = nbwmi_read("panel_od");
    if (v >= 0)
        hw->panel_od = (v == 1);
    v = ut_read_int("/sys/devices/system/cpu/cpufreq/boost");
    if (v >= 0)
        hw->cpu_boost = (v != 0);

    char bat[256];
    if (power_supply_find("Battery", bat, sizeof(bat)) == 0) {
        char p[340];
        if (ut_path_join(p, sizeof(p), bat, "charge_control_end_threshold") == 0) {
            int lim = ut_read_int(p);
            if (lim >= 20 && lim <= 100)
                hw->bat_limit = lim;
        }
    }

    read_profile(hw);
    read_epp(hw);
    read_kbd(hw);

    const display_ops_t *d = display_get();
    if (d) {
        hw->hz_cur = d->current_hz();
        hw->hz_count = d->modes_hz(hw->hz_modes, HZ_MAX_MODES);
    }
}
