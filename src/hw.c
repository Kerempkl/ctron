#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hw.h"
#include "util.h"
#include "settings.h"
#include "display/display.h"

#include <ctype.h>
#include <dirent.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/statvfs.h>
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
    return hw_hwmon_path_nth(want, 0, out, n);
}

int hw_hwmon_path_nth(const char *want, int nth, char *out, size_t n)
{
    glob_t g;
    if (glob("/sys/class/hwmon/hwmon*", 0, NULL, &g) != 0)
        return -1;
    int rc = -1, seen = 0;
    for (size_t i = 0; i < g.gl_pathc; i++) {
        char np[300], name[64] = {0};
        snprintf(np, sizeof(np), "%s/name", g.gl_pathv[i]);
        if (ut_read_file(np, name, sizeof(name)) != 0)
            continue;
        if (!strcmp(name, want)) {
            if (seen++ == nth) {
                snprintf(out, n, "%s", g.gl_pathv[i]);
                rc = 0;
                break;
            }
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

/* same, for the nth device of a name (two RAM modules, two NVMe) */
static int cached_hwmon_nth(char *cache, const char *want, int nth,
                            char *out, size_t n)
{
    if (!cache[0] && hw_hwmon_path_nth(want, nth, cache, 256) != 0)
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

int hw_fan_hwmon_read_live(hw_state_t *hw, fan_curve_t *cpu, fan_curve_t *gpu)
{
    char base[256];
    if (hw_path_fan_curve(hw, base, sizeof(base)) != 0)
        return -1;
    if (cpu)
        fan_read_one(base, "pwm1", cpu);
    if (gpu)
        fan_read_one(base, "pwm2", gpu);
    return 0;
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
        hw_fan_hwmon_read_live(hw, &hw->fan_cpu, &hw->fan_gpu);
    }
    char p[300];
    snprintf(p, sizeof(p), "%s/pwm1_enable", base);
    hw->fan_cpu_on = fan_raw_is_on(ut_read_int(p));
    snprintf(p, sizeof(p), "%s/pwm2_enable", base);
    hw->fan_gpu_on = fan_raw_is_on(ut_read_int(p));
}


/* ---- GPU temperature (cached) ---------------------------------------- */

int hw_gpu_temp_allowed(int pref_on, const char *runtime_status)
{
    if (!pref_on)
        return 0;
    if (runtime_status &&
        (!strcmp(runtime_status, "suspended") ||
         !strcmp(runtime_status, "suspending")))
        return 0;
    return 1;
}

/* PCI display function of vendor 0x10de. Cached; an empty path means
 * there is no runtime file and the query may proceed. */
static int gpu_runtime_status(char *out, size_t n)
{
    static char path[320];
    static int looked = 0;
    DIR *d;
    struct dirent *de;

    if (!looked) {
        looked = 1;
        d = opendir("/sys/bus/pci/devices");
        if (d) {
            while ((de = readdir(d)) != NULL) {
                char vp[360], cp[360], vb[16], cb[16];
                if (de->d_name[0] == '.')
                    continue;
                snprintf(vp, sizeof(vp),
                         "/sys/bus/pci/devices/%s/vendor", de->d_name);
                snprintf(cp, sizeof(cp),
                         "/sys/bus/pci/devices/%s/class", de->d_name);
                if (ut_read_file(vp, vb, sizeof(vb)) != 0)
                    continue;
                if (strcmp(vb, "0x10de") != 0)
                    continue;
                if (ut_read_file(cp, cb, sizeof(cb)) != 0)
                    continue;
                if (strncmp(cb, "0x03", 4) != 0)
                    continue;
                snprintf(path, sizeof(path),
                         "/sys/bus/pci/devices/%s/power/runtime_status",
                         de->d_name);
                break;
            }
            closedir(d);
        }
    }
    if (!path[0])
        return -1;
    return ut_read_file(path, out, n);
}

/* One extended nvidia-smi query per 2 s: temperature, power draw,
 * graphics clock, utilization, VRAM. Fields the driver reports as
 * "[N/A]" stay -1; the whole thing goes dark when the temp toggle is
 * off or the dGPU is suspended (same rule as the old temp-only cache,
 * so nvidia-smi never wakes a sleeping GPU). */
static void gpu_query_cached(hw_state_t *hw)
{
    static int s_temp = -1, s_mw = -1, s_mhz = -1, s_util = -1;
    static int s_vu = -1, s_vt = -1;
    static time_t last = 0;
    time_t now = time(NULL);
    char runtime[32];
    const char *st = NULL;

    if (gpu_runtime_status(runtime, sizeof(runtime)) == 0)
        st = runtime;
    if (!hw_gpu_temp_allowed(g_prefs.gpu_temp, st)) {
        s_temp = s_mw = s_mhz = s_util = s_vu = s_vt = -1;
        last = now;
    } else if (now - last >= 2) {
        char out[192] = {0};
        const char *q[] = {
            "nvidia-smi",
            "--query-gpu=temperature.gpu,power.draw,"
            "clocks.current.graphics,utilization.gpu,memory.used,memory.total",
            "--format=csv,noheader,nounits", NULL
        };
        if (ut_exec_argv(q, out, sizeof(out)) == 0) {
            int t, mw, mhz, util, vu, vt;
            hw_parse_gpu_csv(out, &t, &mw, &mhz, &util, &vu, &vt);
            s_temp = t; s_mw = mw; s_mhz = mhz;
            s_util = util; s_vu = vu; s_vt = vt;
        } else {
            s_temp = s_mw = s_mhz = s_util = s_vu = s_vt = -1;
        }
        last = now;
    }
    hw->gpu_temp = s_temp;
    hw->gpu_mw = s_mw;
    hw->gpu_mhz = s_mhz;
    hw->gpu_util = s_util;
    hw->gpu_vram_used_mb = s_vu;
    hw->gpu_vram_total_mb = s_vt;
}

/* ---- telemetry source parsers (pure, unit-tested) -------------------- */

int hw_parse_meminfo_kb(const char *text, const char *key, long *out_kb)
{
    if (!text || !key || !out_kb)
        return -1;
    size_t klen = strlen(key);
    const char *p = text;
    while (p && *p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == ':') {
            *out_kb = strtol(p + klen + 1, NULL, 10);
            return 0;
        }
        p = strchr(p, '\n');
        if (p)
            p++;
    }
    return -1;
}

int hw_parse_gpu_csv(const char *line, int *temp, int *mw, int *mhz,
                     int *util, int *vram_used, int *vram_total)
{
    if (temp) *temp = -1;
    if (mw) *mw = -1;
    if (mhz) *mhz = -1;
    if (util) *util = -1;
    if (vram_used) *vram_used = -1;
    if (vram_total) *vram_total = -1;
    if (!line)
        return -1;
    double f[6] = {0};
    int got[6] = {0};
    const char *p = line;
    for (int i = 0; i < 6; i++) {
        char *end = NULL;
        double v = strtod(p, &end);
        if (end != p) {
            f[i] = v;
            got[i] = 1;
        }
        const char *c = strchr(p, ',');
        if (!c)
            break;
        p = c + 1;
    }
    if (got[0] && f[0] > 0 && f[0] < 130)
        *temp = (int)f[0];
    if (got[1] && f[1] >= 0)
        *mw = (int)(f[1] * 1000.0 + 0.5);
    if (got[2] && f[2] >= 0)
        *mhz = (int)f[2];
    if (got[3] && f[3] >= 0)
        *util = (int)f[3];
    if (got[4] && f[4] >= 0)
        *vram_used = (int)f[4];
    if (got[5] && f[5] >= 0)
        *vram_total = (int)f[5];
    return 0;
}

/* Whole-disk lines only: partitions (nvme0n1p2, mmcblk0p1, sda1) and
 * loop/ram/dm/zram devices are excluded so bytes are not counted
 * twice (partition totals roll up into their disk). */
static bool diskstats_whole(const char *name)
{
    if (!strncmp(name, "loop", 4) || !strncmp(name, "ram", 3) ||
        !strncmp(name, "dm-", 3) || !strncmp(name, "zram", 4))
        return false;
    if (!strncmp(name, "nvme", 4) || !strncmp(name, "mmcblk", 6))
        return strchr(name, 'p') == NULL;
    if (name[0] == 's' && name[1] == 'd') {
        for (const char *p = name + 2; *p; p++)
            if (!isalpha((unsigned char)*p))
                return false; /* sda1 & friends are partitions */
        return true;
    }
    return false;
}

int hw_parse_diskstats(const char *text, long long *rd_sectors,
                       long long *wr_sectors)
{
    if (rd_sectors)
        *rd_sectors = 0;
    if (wr_sectors)
        *wr_sectors = 0;
    if (!text || !rd_sectors || !wr_sectors)
        return -1;
    const char *p = text;
    while (p && *p) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        char line[512];
        if (len > 0 && len < sizeof line) {
            memcpy(line, p, len);
            line[len] = '\0';
            long long maj, min;
            char name[64];
            unsigned long long d1, d2, rds, d3, d4, d5, wrs, d6;
            /* fields: major minor name reads rd_merged rd_sectors rd_ms
             *         writes wr_merged wr_sectors wr_ms ... */
            if (sscanf(line, "%lld %lld %63s %llu %llu %llu %llu %llu %llu %llu %llu",
                       &maj, &min, name, &d1, &d2, &rds, &d3,
                       &d4, &d5, &wrs, &d6) == 11 &&
                diskstats_whole(name)) {
                *rd_sectors += (long long)rds;
                *wr_sectors += (long long)wrs;
            }
        }
        p = e ? e + 1 : NULL;
    }
    return 0;
}

/* ---- telemetry extras: cheap procfs/sysfs reads, path-cached -------- */

static int hwmon_read_int(const char *base, const char *leaf, long *out)
{
    char p[320];
    if (!base || ut_path_join(p, sizeof(p), base, leaf) != 0)
        return -1;
    int v = ut_read_int(p);
    if (v < 0)
        return -1;
    if (out)
        *out = v;
    return 0;
}

/* USB-C PD source ports (ucsi power supplies, type "USB"): the
 * voltage/current contract each port reports — the input watts when
 * a PD adapter feeds the machine, 0 when nothing is attached. */
static void read_pd_ports(hw_state_t *hw)
{
    hw->pd_n = 0;
    hw->pd_in_mw = 0;
    glob_t g;
    if (glob("/sys/class/power_supply/*", GLOB_NOSORT, NULL, &g) != 0)
        return;
    char p[340], tbuf[32];
    for (size_t i = 0; i < g.gl_pathc && hw->pd_n < 4; i++) {
        if (ut_path_join(p, sizeof p, g.gl_pathv[i], "type") != 0)
            continue;
        if (ut_read_file(p, tbuf, sizeof tbuf) != 0 ||
            strncmp(tbuf, "USB", 3) != 0)
            continue;
        hw->pd_n++;
        int v = -1, a = -1;
        if (ut_path_join(p, sizeof p, g.gl_pathv[i], "voltage_now") == 0)
            v = ut_read_int(p);
        if (ut_path_join(p, sizeof p, g.gl_pathv[i], "current_now") == 0)
            a = ut_read_int(p);
        if (v > 0 && a > 0) /* µV * µA = 1e-12 W -> mW */
            hw->pd_in_mw += (int)((long long)v * a / 1000000000LL);
    }
    globfree(&g);
}

static void read_telem_extras(hw_state_t *hw)
{
    char base[256], buf[16384];
    long v;

    hw->load1 = -1;
    if (ut_read_file("/proc/loadavg", buf, 32) == 0)
        hw->load1 = strtod(buf, NULL);

    hw->mem_total_mb = hw->mem_avail_mb = hw->mem_used_mb = -1;
    hw->swap_total_mb = hw->swap_used_mb = -1;
    if (ut_read_file_all("/proc/meminfo", buf, sizeof buf) == 0) {
        long kb;
        if (hw_parse_meminfo_kb(buf, "MemTotal", &kb) == 0)
            hw->mem_total_mb = (int)(kb / 1024);
        if (hw_parse_meminfo_kb(buf, "MemAvailable", &kb) == 0)
            hw->mem_avail_mb = (int)(kb / 1024);
        if (hw->mem_total_mb > 0 && hw->mem_avail_mb >= 0)
            hw->mem_used_mb = hw->mem_total_mb - hw->mem_avail_mb;
        if (hw_parse_meminfo_kb(buf, "SwapTotal", &kb) == 0)
            hw->swap_total_mb = (int)(kb / 1024);
        if (hw_parse_meminfo_kb(buf, "SwapFree", &kb) == 0 &&
            hw->swap_total_mb >= 0)
            hw->swap_used_mb = hw->swap_total_mb - (int)(kb / 1024);
    }

    hw->igpu_temp = hw->igpu_mhz = hw->igpu_mw = -1;
    hw->igpu_vddgfx_mv = hw->igpu_vddnb_mv = -1;
    int ig_ok = 0;
    if (cached_hwmon(hw->paths.amdgpu, "amdgpu", base, sizeof base) == 0) {
        if (hwmon_read_int(base, "temp1_input", &v) == 0 && v > 0 && v < 150000) {
            hw->igpu_temp = (int)(v / 1000);
            ig_ok = 1;
        }
        if (hwmon_read_int(base, "freq1_input", &v) == 0 && v > 0)
            hw->igpu_mhz = (int)(v / 1000000);
        if (hwmon_read_int(base, "power1_input", &v) == 0 && v >= 0)
            hw->igpu_mw = (int)(v / 1000);
        if (hwmon_read_int(base, "in0_input", &v) == 0 && v > 0)
            hw->igpu_vddgfx_mv = (int)v;
        if (hwmon_read_int(base, "in1_input", &v) == 0 && v > 0)
            hw->igpu_vddnb_mv = (int)v;
        if (!ig_ok && hw->igpu_mhz < 0 && hw->igpu_mw < 0)
            hw->paths.amdgpu[0] = '\0'; /* path gone: re-probe */
    }

    hw->ram_temp_n = 0;
    for (int i = 0; i < 2; i++) {
        char *cache = i == 0 ? hw->paths.ram0 : hw->paths.ram1;
        if (cached_hwmon_nth(cache, "spd5118", i, base, sizeof base) == 0 &&
            hwmon_read_int(base, "temp1_input", &v) == 0 && v > 0 && v < 150000)
            hw->ram_temp[hw->ram_temp_n++] = (int)(v / 1000);
        else
            cache[0] = '\0';
    }

    hw->nvme_temp_n = 0;
    for (int i = 0; i < 2; i++) {
        char *cache = i == 0 ? hw->paths.nvme0 : hw->paths.nvme1;
        if (cached_hwmon_nth(cache, "nvme", i, base, sizeof base) == 0 &&
            hwmon_read_int(base, "temp1_input", &v) == 0 && v > 0 && v < 150000)
            hw->nvme_temp[hw->nvme_temp_n++] = (int)(v / 1000);
        else
            cache[0] = '\0';
    }

    hw->disk_rd_sectors = hw->disk_wr_sectors = -1;
    if (ut_read_file_all("/proc/diskstats", buf, sizeof buf) == 0) {
        long long rd, wr;
        hw_parse_diskstats(buf, &rd, &wr);
        hw->disk_rd_sectors = rd;
        hw->disk_wr_sectors = wr;
    }

    hw->fs_used_mb = hw->fs_total_mb = -1;
    struct statvfs st;
    if (statvfs("/", &st) == 0 && st.f_frsize > 0 && st.f_blocks > 0) {
        hw->fs_total_mb =
            (int)((long long)st.f_blocks * st.f_frsize / (1024 * 1024));
        hw->fs_used_mb = (int)((long long)(st.f_blocks - st.f_bfree) *
                               st.f_frsize / (1024 * 1024));
    }

    read_pd_ports(hw);
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

static void cpu_limits_sweep(hw_state_t *hw, bool full);

void hw_init(hw_state_t *hw)
{
    memset(hw, 0, sizeof(*hw));
    hw->cpu_temp = -1;
    hw->gpu_temp = -1;
    hw->rpm_cpu = -1;
    hw->rpm_gpu = -1;
    hw->asusd_ac = -2;   /* unknown until asusd.ron is parsed */
    hw->asusd_bat = -2;
    hw->load1 = -1;
    hw->mem_total_mb = hw->mem_avail_mb = hw->mem_used_mb = -1;
    hw->swap_total_mb = hw->swap_used_mb = -1;
    hw->igpu_temp = hw->igpu_mhz = hw->igpu_mw = -1;
    hw->gpu_mw = hw->gpu_mhz = hw->gpu_util = -1;
    hw->gpu_vram_used_mb = hw->gpu_vram_total_mb = -1;
    hw->disk_rd_sectors = hw->disk_wr_sectors = -1;
    hw->fs_used_mb = hw->fs_total_mb = -1;

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
    cpu_limits_sweep(hw, true);
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
        const char *q[] = { "nvidia-smi", "--query-gpu=clocks.max.graphics",
                            "--format=csv,noheader,nounits", NULL };
        if (ut_exec_argv(q, out, sizeof(out)) == 0) {
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

/* The static column of the sweep (`present` + cpuinfo_min/max_freq)
 * is effectively constant while running — cpuinfo_max moves only on
 * profile transitions — and re-reading all of it every 250 ms poll
 * was the bulk of the fast path (48 of ~69 reads). full=true re-reads
 * it (init, hw_refresh_live, and every >=5 s in the fast poll); the
 * per-tick dynamic pass touches only scaling_max_freq. */
#define SWEEP_STATIC_MS 5000
static long s_sweep_last_full_ms;

static long hw_mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void cpu_limits_sweep(hw_state_t *hw, bool full)
{
    if (full) {
        int ids[HW_CPU_MAX];
        int n = cpu_present_ids(ids, HW_CPU_MAX);
        /* publish the id list for the write layer and the CORE LIMITS
         * editor; ids beyond the array are dropped (bounded kernels
         * only) */
        int kept = 0;
        for (int c = 0; c < n; c++) {
            if (ids[c] >= 0 && ids[c] < HW_CPU_MAX)
                hw->cpu_ids[kept++] = ids[c];
        }
        hw->cpu_n = kept > 0 ? kept : 1;
        s_sweep_last_full_ms = hw_mono_ms();
    }
    int min_khz = 0, max_khz = 0, lim_khz = 0;
    for (int c = 0; c < hw->cpu_n; c++) {
        char p[96];
        int v;
        int i = hw->cpu_ids[c];
        if (full) {
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
        }
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
    if (max_khz > 0 && max_khz / 1000 > hw->cpu_mhz_max)
        hw->cpu_mhz_max = max_khz / 1000; /* raise-only: quiet pins the
            cpuinfo ceiling at the base clock, and a snapshot taken
            there must never clamp later writes (snapshot apply under
            quiet silently rewrote 5386 as 2401) */
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

    /* dynamic-only pass between full sweeps (see SWEEP_STATIC_MS) */
    cpu_limits_sweep(hw, hw_mono_ms() - s_sweep_last_full_ms >=
                            SWEEP_STATIC_MS);

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
        gpu_query_cached(hw);

    char bat[256];
    hw->bat_v_mv = hw->bat_a_ma = -1;
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
        if (ut_path_join(p, sizeof(p), bat, "voltage_now") == 0) {
            int uv = ut_read_int(p);
            if (uv > 0)
                hw->bat_v_mv = uv / 1000;
        }
        if (ut_path_join(p, sizeof(p), bat, "current_now") == 0) {
            int ua = ut_read_int(p);
            if (ua > 0)
                hw->bat_a_ma = ua / 1000;
        }
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

    read_telem_extras(hw);
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
    /* live snapshots follow user actions / view switches: always a
     * full sweep, never the throttled dynamic-only pass */
    s_sweep_last_full_ms = 0;
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
    hw->bat_health = -1;
    if (cached_supply(hw->paths.battery, "Battery", bat, sizeof(bat)) == 0) {
        char p[340];
        if (ut_path_join(p, sizeof(p), bat, "charge_control_end_threshold") == 0) {
            int lim = ut_read_int(p);
            if (lim >= 20 && lim <= 100)
                hw->bat_limit = lim;
        }
        /* wear: full charge as a share of design (charge_* for this
         * battery; energy_* is the same idea for watt-hour packs) */
        int full = -1, design = -1;
        if (ut_path_join(p, sizeof(p), bat, "charge_full") == 0)
            full = ut_read_int(p);
        if (ut_path_join(p, sizeof(p), bat, "charge_full_design") == 0)
            design = ut_read_int(p);
        if (full <= 0 || design <= 0) {
            if (ut_path_join(p, sizeof(p), bat, "energy_full") == 0)
                full = ut_read_int(p);
            if (ut_path_join(p, sizeof(p), bat, "energy_full_design") == 0)
                design = ut_read_int(p);
        }
        if (full > 0 && design > 0)
            hw->bat_health = (int)((long long)full * 100 / design);
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
