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

/* ---- cached device paths --------------------------------------------------
 * sysfs device paths are stable while running, except across
 * suspend/resume when hwmon / power-supply indices renumber. An empty
 * cache entry triggers a probe; a failed read on a cached path drops
 * it so the next poll re-probes. This keeps the 250 ms hot path free
 * of the four discovery globs it used to run per tick. */
static int cached_hwmon(char *cache, const char *want, char *out, size_t n)
{
    if (!cache[0] && hw_hwmon_path(want, cache, 256) != 0)
        return -1;
    snprintf(out, n, "%s", cache);
    return 0;
}

static int cached_supply(char *cache, const char *type, char *out, size_t n)
{
    if (!cache[0] && power_supply_find(type, cache, 256) != 0)
        return -1;
    snprintf(out, n, "%s", cache);
    return 0;
}

int hw_path_fan_curve(hw_state_t *hw, char *out, size_t n)
{
    return cached_hwmon(hw->paths.fan_curve, "asus_custom_fan_curve",
                        out, n);
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
    if (hw_path_fan_curve(hw, base, sizeof(base)) != 0) {
        hw->has_fan_curve = false;
        if (hw->fan_cpu.n == 0) {
            fan_default(&hw->fan_cpu);
            fan_default(&hw->fan_gpu);
        }
        return;
    }
    hw->has_fan_curve = true;
    /* staged edits are the user's intent: a live refresh must not
     * clobber them with the hwmon table; the enable switches still
     * track the hardware */
    if (!hw->fan_staged) {
        fan_read_one(base, "pwm1", &hw->fan_cpu);
        fan_read_one(base, "pwm2", &hw->fan_gpu);
    }
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

static void cpu_limits_sweep(hw_state_t *hw);

void hw_init(hw_state_t *hw)
{
    memset(hw, 0, sizeof(*hw));
    hw->cpu_temp = -1;
    hw->gpu_temp = -1;
    hw->rpm_cpu = -1;
    hw->rpm_gpu = -1;
    hw->asusd_ac = -2;   /* unknown until asusd.ron is parsed */
    hw->asusd_bat = -2;

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

    /* PPT write interface (probe, no sudo): official armoury attribute
     * first; on FA608PP it is ENODEV and the legacy nb-wmi nodes are
     * accepted-but-ignored, so ryzenadj (field-verified 2026-10-08) is
     * the working fallback when installed */
    hw->ppt_mode = HW_PPT_LEGACY;
    {
        char probe[32];
        if (hw_armoury_read("ppt_pl1_spl", probe, sizeof(probe)) == 0 &&
            probe[0])
            hw->ppt_mode = HW_PPT_ARMOURY;
        else if (ut_have_cmd("ryzenadj"))
            hw->ppt_mode = HW_PPT_RYZENADJ;
    }

    int min_khz = ut_read_int("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_min_freq");
    int max_khz = ut_read_int("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
    hw->cpu_mhz_min = min_khz > 0 ? min_khz / 1000 : 400;
    hw->cpu_mhz_max = max_khz > 0 ? max_khz / 1000 : 5500;
    hw->cpu_mhz_limit = hw->cpu_mhz_max;
    /* all-cores sweep also fills cpu_n / cpu_mhz_core, which the CLI
     * flag path needs before any hw_refresh_* call */
    cpu_limits_sweep(hw);
    hw_topology_build(hw);

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

    /* GPU hardware clock ceiling — read once, it is a fixed property */
    if (hw->has_nvidia_smi) {
        char out[64] = {0};
        if (ut_exec("nvidia-smi --query-gpu=clocks.max.graphics "
                    "--format=csv,noheader,nounits", out, sizeof(out)) == 0) {
            int m = atoi(out);
            if (m > 0 && m < 8192)
                hw->gpu_mhz_max = m;
        }
    }

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
/* Parse a CPU list like "0-31" or "0-15,32-47" into ids (capped at
 * max). Returns the count, or 0 when nothing parsed. Pure — tested. */
int hw_cpu_list_parse(const char *s, int *ids, int max)
{
    if (!s || !ids || max <= 0)
        return 0;
    int n = 0;
    while (*s && n < max) {
        int lo, hi;
        if (sscanf(s, "%d-%d", &lo, &hi) == 2 && lo <= hi) {
            for (int i = lo; i <= hi && n < max; i++)
                ids[n++] = i;
        } else if (sscanf(s, "%d", &lo) == 1) {
            ids[n++] = lo;
        } else {
            break;
        }
        while (*s && *s != ',')
            s++;
        if (*s == ',')
            s++;
    }
    return n;
}

static int cpu_present_ids(int *ids, int max)
{
    char buf[128];
    if (ut_read_file("/sys/devices/system/cpu/present", buf, sizeof(buf)) != 0)
        return 0;
    return hw_cpu_list_parse(buf, ids, max);
}

static void cpu_limits_sweep(hw_state_t *hw)
{
    int ids[HW_CPU_MAX];
    int n = cpu_present_ids(ids, HW_CPU_MAX);
    int min_khz = 0, max_khz = 0, lim_khz = 0;
    /* publish the id list for the write layer and the CORE LIMITS
     * editor; ids beyond the array are dropped (bounded kernels only) */
    int kept = 0;
    for (int c = 0; c < n; c++) {
        if (ids[c] >= 0 && ids[c] < HW_CPU_MAX)
            hw->cpu_ids[kept++] = ids[c];
    }
    hw->cpu_n = kept > 0 ? kept : 1;
    for (int c = 0; c < kept; c++) {
        char p[96];
        int v;
        int i = hw->cpu_ids[c];
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
        if (i >= 0 && i < HW_CPU_MAX && v > 0)
            hw->cpu_mhz_core[i] = v / 1000;
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

bool hw_cpu_present(const hw_state_t *hw, int id)
{
    for (int c = 0; c < hw->cpu_n; c++)
        if (hw->cpu_ids[c] == id)
            return true;
    return false;
}

/* ---- physical topology (cores from SMT siblings, CCDs from L3) ----- */

int hw_topology_group(const int *ids, const char *const *sib,
                      const char *const *l3, int n,
                      int *core_cpu, int *core_sib, int *core_ccd,
                      int *ccd_n_out, bool *odd_out)
{
    *odd_out = false;
    *ccd_n_out = 1;
    if (n <= 0) {
        *odd_out = true;
        return 0;
    }

    /* L3 groups: identical shared_cpu_list strings = same domain; NULL
     * and "" read the same (unknown L3 -> one cluster of its own) */
    int grp[HW_CPU_MAX];
    int ccd_n = 0;
    for (int i = 0; i < n; i++) {
        grp[i] = -1;
        const char *li = l3[i] ? l3[i] : "";
        for (int j = 0; j < i; j++) {
            const char *lj = l3[j] ? l3[j] : "";
            if (strcmp(li, lj) == 0) {
                grp[i] = grp[j];
                break;
            }
        }
        if (grp[i] < 0)
            grp[i] = ccd_n++;
    }
    *ccd_n_out = ccd_n > 0 ? ccd_n : 1;

    /* cores: the present members of each thread_siblings_list; keyed by
     * the lowest present id (canonical rep), >2 present threads = odd */
    int core_n = 0;
    for (int i = 0; i < n; i++) {
        int pair[2];
        int m = 0;
        bool over = false;

        int tmp[8];
        int t = 0;
        const char *s = sib[i] ? sib[i] : "";
        if (s[0])
            t = hw_cpu_list_parse(s, tmp, 8);
        for (int k = 0; k < t && !over; k++) {
            bool present = false;
            for (int j = 0; j < n; j++) {
                if (ids[j] == tmp[k]) {
                    present = true;
                    break;
                }
            }
            if (!present)
                continue; /* sibling offline / not in the list */
            if (m >= 2) {
                over = true;
                break;
            }
            pair[m++] = tmp[k];
        }
        if (!over) {
            bool self = false;
            for (int k = 0; k < m; k++)
                if (pair[k] == ids[i])
                    self = true;
            if (!self) {
                if (m >= 2)
                    over = true;
                else
                    pair[m++] = ids[i];
            }
        }
        if (over) {
            *odd_out = true;
            return 0;
        }

        if (m == 2 && pair[0] > pair[1]) {
            int sw = pair[0];
            pair[0] = pair[1];
            pair[1] = sw;
        }

        /* canonical rep already recorded? then this position rides it */
        int found = -1;
        for (int k = 0; k < core_n; k++)
            if (core_cpu[k] == pair[0]) {
                found = k;
                break;
            }
        if (found < 0) {
            core_cpu[core_n] = pair[0];
            core_sib[core_n] = m > 1 ? pair[1] : -1;
            core_ccd[core_n] = grp[i];
            core_n++;
        }
    }
    return core_n;
}

void hw_topology_build(hw_state_t *hw)
{
    static char sib_buf[HW_CPU_MAX][48];
    static char l3_buf[HW_CPU_MAX][48];
    const char *sibp[HW_CPU_MAX];
    const char *l3p[HW_CPU_MAX];

    for (int c = 0; c < hw->cpu_n && c < HW_CPU_MAX; c++) {
        int id = hw->cpu_ids[c];
        char p[96];
        bool have_sib = false;

        sib_buf[id][0] = '\0';
        l3_buf[id][0] = '\0';

        snprintf(p, sizeof p,
                 "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list",
                 id);
        if (ut_read_file(p, sib_buf[id], sizeof sib_buf[id]) == 0)
            have_sib = true;

        /* L3 = the cache index whose level is 3 (indexes are not
         * guaranteed contiguous; probe a window) */
        for (int idx = 0; idx < 16; idx++) {
            char lb[8];
            snprintf(p, sizeof p,
                     "/sys/devices/system/cpu/cpu%d/cache/index%d/level",
                     id, idx);
            if (ut_read_file(p, lb, sizeof lb) != 0)
                continue;
            if (atoi(lb) == 3) {
                snprintf(p, sizeof p,
                         "/sys/devices/system/cpu/cpu%d/cache/index%d/"
                         "shared_cpu_list", id, idx);
                ut_read_file(p, l3_buf[id], sizeof l3_buf[id]);
                break;
            }
        }
        sibp[c] = sib_buf[id];
        l3p[c] = l3_buf[id];
        if (!have_sib) {
            /* no topology sysfs: honest fallback to the per-cpu grid */
            hw->topo_odd = true;
            hw->core_n = 0;
            hw->ccd_n = 1;
            return;
        }
    }

    hw->core_n = hw_topology_group(hw->cpu_ids, sibp, l3p, hw->cpu_n,
                                   hw->core_cpu, hw->core_sib, hw->core_ccd,
                                   &hw->ccd_n, &hw->topo_odd);
}

void hw_refresh_fast(hw_state_t *hw)
{
    char p[320], q[320];

    cpu_limits_sweep(hw);

    if (cached_hwmon(hw->paths.k10temp, "k10temp", p, sizeof(p)) == 0 &&
        ut_path_join(q, sizeof(q), p, "temp1_input") == 0) {
        int t = ut_read_int(q);
        if (t > 0)
            hw->cpu_temp = t / 1000;
        else
            hw->paths.k10temp[0] = '\0';   /* path gone: re-probe */
    }

    int cur = ut_read_int("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    if (cur > 0)
        hw->cpu_mhz_cur = cur / 1000;

    if (hw->has_nvidia_smi)
        hw->gpu_temp = gpu_temp_cached();

    char bat[256];
    if (cached_supply(hw->paths.battery, "Battery", bat, sizeof(bat)) == 0) {
        if (ut_path_join(p, sizeof(p), bat, "capacity") == 0) {
            int pct = ut_read_int(p);
            if (pct >= 0)
                hw->bat_pct = pct;
            else
                hw->paths.battery[0] = '\0';
        }
        char st[16] = {0};
        if (ut_path_join(p, sizeof(p), bat, "status") == 0)
            ut_read_file(p, st, sizeof(st));
        if (st[0])
            snprintf(hw->bat_status, sizeof(hw->bat_status), "%s", st);
        read_bat_power(hw, bat);
    }

    char ac[256];
    if (cached_supply(hw->paths.mains, "Mains", ac, sizeof(ac)) == 0 &&
        ut_path_join(p, sizeof(p), ac, "online") == 0) {
        int on = ut_read_int(p);
        if (on >= 0)
            hw->ac_online = (on == 1);
        else
            hw->paths.mains[0] = '\0';
    }

    if (cached_hwmon(hw->paths.fan_rpm, "asus", p, sizeof(p)) == 0) {
        hw->has_fan_rpm = true;
        int r1 = -1, r2 = -1;
        if (ut_path_join(q, sizeof(q), p, "fan1_input") == 0)
            r1 = ut_read_int(q);
        if (ut_path_join(q, sizeof(q), p, "fan2_input") == 0)
            r2 = ut_read_int(q);
        hw->rpm_cpu = r1 > 0 ? r1 : -1;
        hw->rpm_gpu = r2 > 0 ? r2 : -1;
        if (r1 < 0 && r2 < 0)
            hw->paths.fan_rpm[0] = '\0';
    } else {
        hw->has_fan_rpm = false;
    }
}

/* ---- full snapshot ---------------------------------------------------- */

/* ---- asusd power-source profile takeover ---------------------------------
 * On every AC/battery event the daemon re-applies its chosen profile
 * (observed fighting the KDE shortcut on USB-C PD renegotiations).
 * Live daemon state is the authoritative source: asusd.ron flushes
 * asynchronously, so a file read races an immediate re-read after a
 * write (that stale file value is what kept the TUI row showing the
 * old setting right after an apply). Names only — asusd's numeric
 * enum differs from ours, never map by value. */

int hw_asusd_auto_flag(const char *prop)
{
    char out[64] = {0};
    char cmd[200];
    snprintf(cmd, sizeof(cmd),
             "busctl get-property xyz.ljones.Asusd /xyz/ljones "
             "xyz.ljones.Platform %s", prop);
    if (ut_exec_raw(cmd, out, sizeof(out)) != 0)
        return -2;
    if (strstr(out, "true"))
        return 1;
    if (strstr(out, "false"))
        return 0;
    return -2;
}

static int asusd_profile_from_get_out(const char *out, int ac)
{
    const char *key = ac ? "AC profile" : "Battery profile";
    const char *p = strstr(out, key);
    if (!p)
        return -2;
    p += strlen(key);
    while (*p == ' ')
        p++;
    const char *end = p;
    while (*end && *end != '\n' && *end != '\r')
        end++;
    char name[24];
    size_t n = (size_t)(end - p);
    if (n >= sizeof(name))
        n = sizeof(name) - 1;
    memcpy(name, p, n);
    name[n] = '\0';
    return hw_profile_from_name(name);
}

int hw_asusd_auto_profile(int ac)
{
    char out[512] = {0};
    /* raw: the default exec capture truncates at the first newline,
     * and the AC/Battery lines are on later lines */
    if (ut_exec_raw("asusctl profile get", out, sizeof(out)) != 0)
        return -2;
    return asusd_profile_from_get_out(out, ac);
}

/* Pure: parse the asusd.ron body for the power-source profile takeover.
 * *ac and *bat: -1 auto-switching off, 0..2 the hw_profile_t, -2 unknown.
 * Order matters: the change_ keys contain the profile keys as
 * substrings, so they must match first. */
void hw_asusd_ron_parse(const char *text, int *ac, int *bat)
{
    *ac = -1;
    *bat = -1;
    if (!text)
        return;
    char buf[4096];
    snprintf(buf, sizeof(buf), "%s", text);

    bool ac_change = false, bat_change = false;
    int ac_prof = -2, bat_prof = -2;
    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl)
            *nl = '\0';
        /* order matters: the change_ keys contain the profile keys */
        if (strstr(line, "change_platform_profile_on_ac:"))
            ac_change = strstr(line, "true") != NULL;
        else if (strstr(line, "change_platform_profile_on_battery:"))
            bat_change = strstr(line, "true") != NULL;
        else if (strstr(line, "platform_profile_on_ac:")) {
            char *v = strstr(line, "platform_profile_on_ac:");
            v += strlen("platform_profile_on_ac:");
            while (*v == ' ' || *v == '\t')
                v++;
            char *comma = strchr(v, ',');
            if (comma)
                *comma = '\0';
            ac_prof = hw_profile_from_name(v);
        }
        else if (strstr(line, "platform_profile_on_battery:")) {
            char *v = strstr(line, "platform_profile_on_battery:");
            v += strlen("platform_profile_on_battery:");
            while (*v == ' ' || *v == '\t')
                v++;
            char *comma = strchr(v, ',');
            if (comma)
                *comma = '\0';
            bat_prof = hw_profile_from_name(v);
        }
        line = nl ? nl + 1 : NULL;
    }
    *ac = ac_change ? (ac_prof >= 0 ? ac_prof : -2) : -1;
    *bat = bat_change ? (bat_prof >= 0 ? bat_prof : -2) : -1;
}

void hw_asusd_auto_read(hw_state_t *hw)
{
    hw->asusd_ac = -2;
    hw->asusd_bat = -2;

    int fac = hw_asusd_auto_flag("ChangePlatformProfileOnAc");
    int fbat = hw_asusd_auto_flag("ChangePlatformProfileOnBattery");
    int pac = -2, pbat = -2;
    if (fac == 1 || fbat == 1) {
        char out[512] = {0};
        if (ut_exec_raw("asusctl profile get", out, sizeof(out)) == 0) {
            if (fac == 1)
                pac = asusd_profile_from_get_out(out, 1);
            if (fbat == 1)
                pbat = asusd_profile_from_get_out(out, 0);
        }
    }
    if ((fac == 0 || pac >= 0) && (fbat == 0 || pbat >= 0)) {
        hw->asusd_ac = fac == 0 ? -1 : pac;
        hw->asusd_bat = fbat == 0 ? -1 : pbat;
        return;                     /* daemon answered: authoritative */
    }
    if (fac != -2 || fbat != -2)
        return;                     /* partial answer: stay unknown */

    /* no daemon: fall back to the config file (values may be stale
     * right after a write — asusd flushes asynchronously). The file is
     * multi-line: read it whole, a first-line read saw only "(" and
     * reported every takeover as off. */
    char buf[4096];
    if (ut_read_file_all("/etc/asusd/asusd.ron", buf, sizeof(buf)) != 0)
        return;
    hw_asusd_ron_parse(buf, &hw->asusd_ac, &hw->asusd_bat);
}

int hw_asusd_enforced(const hw_state_t *hw)
{
    int v = hw->ac_online ? hw->asusd_ac : hw->asusd_bat;
    return v >= 0 ? v : -1;
}

void hw_refresh_live(hw_state_t *hw)
{
    hw_refresh_fast(hw);

    fan_hwmon_read(hw);
    hw_asusd_auto_read(hw);

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
    if (cached_supply(hw->paths.battery, "Battery", bat, sizeof(bat)) == 0) {
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
