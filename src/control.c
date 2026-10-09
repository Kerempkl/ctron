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
#include <unistd.h>

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
 * cpu0 alone left 31 cores clamped at base on the FA608PP.
 *
 * Iterates hw->cpu_ids, the kernel's real present list: numbering is
 * NOT contiguous on every machine (SMT off or offlined cores give
 * lists like "0-15,32-47"), and a scan-and-break-at-first-gap would
 * skip everything after the hole. */
static int cpufreq_write_all(const hw_state_t *hw, const char *leaf,
                             const char *val)
{
    int rc = -1;
    for (int c = 0; c < hw->cpu_n && c < HW_CPU_MAX; c++) {
        char path[96];
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/%s",
                 hw->cpu_ids[c], leaf);
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
    int rc = cpufreq_write_all(hw, "energy_performance_preference",
                               hw_epp_name(e));
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
    /* NO pre-clamp to hw->cpu_mhz_max: under Quiet that snapshot holds
     * the base clock (2401 here) and would silently rewrite the
     * requested 5386 as 2401 — "verified" at the wrong value. Real
     * limits are enforced by the round loop below (waits for the live
     * cpuinfo ceiling) and by the hardware itself; callers validate
     * the range (cmds/UI). If this write opens a wider ceiling than
     * ever seen, remember it. */
    char val[24];
    snprintf(val, sizeof(val), "%d", mhz * 1000);

    /* Quiet pins the cpuinfo ceiling at the base clock; after a
     * profile switch amd-pstate (a) re-opens cpuinfo over ~1-3 s and
     * (b) ASYNCHRONOUSLY re-pins scaling_max to the base clock ~2-4 s
     * after the switch, clobbering writes that landed earlier (live
     * repro: snapshot "profile performance, freq 5386" from Quiet
     * verified mid-apply and still ended at 2401). So a write is only
     * accepted after it survives verification on two consecutive
     * rounds — and a collapse triggers a re-write. Rounds sleep 0.9 s;
     * the ceiling check reads cpuinfo live instead of trusting the
     * init snapshot. */
    int wrote_at_ms = 0, waited_ms = 0;
    int bad = 1, back_khz = 0;
    int ok_streak = 0;
    int rc = 0;
    for (int round = 0; round < 8; round++) {
        if (round > 0) {
            usleep(900000);
            waited_ms += 900;
        }
        if (ok_streak == 0) {
            int info_khz = 0;
            for (int c = 0; c < hw->cpu_n && c < HW_CPU_MAX; c++) {
                char ip[96];
                snprintf(ip, sizeof(ip),
                         "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq",
                         hw->cpu_ids[c]);
                int iv = ut_read_int(ip);
                if (iv > info_khz)
                    info_khz = iv;
            }
            if (info_khz > 0 && mhz * 1000 - info_khz > 2000)
                continue; /* ceiling not open yet: writing would clamp */

            rc = cpufreq_write_all(hw, "scaling_max_freq", val);
            if (rc != 0)
                break;
            wrote_at_ms = waited_ms;
        }
        bad = 0;
        back_khz = 0;
        for (int c = 0; c < hw->cpu_n && c < HW_CPU_MAX; c++) {
            char rp[96];
            int id = hw->cpu_ids[c];
            snprintf(rp, sizeof(rp),
                     "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", id);
            int rv = ut_read_int(rp);
            if (rv <= 0 || rv / 1000 - mhz > 2 || mhz - rv / 1000 > 2)
                bad++;
            if (rv > back_khz)
                back_khz = rv;
        }
        if (bad == 0) {
            ok_streak++;
            if (ok_streak >= 2)
                break; /* stable across consecutive rounds */
        } else {
            ok_streak = 0;
        }
    }

    if (rc == 0 && bad == 0) {
        hw->cpu_mhz_limit = mhz;
        if (mhz > hw->cpu_mhz_max)
            hw->cpu_mhz_max = mhz; /* the write proved a wider ceiling */
        for (int c = 0; c < hw->cpu_n && c < HW_CPU_MAX; c++)
            hw->cpu_mhz_core[hw->cpu_ids[c]] = mhz;
        if (waited_ms > 0)
            ut_log("cpu max: %d MHz (verified, after a %d.%d s profile-switch window)",
                   mhz, wrote_at_ms / 1000, (wrote_at_ms % 1000) / 100);
        else
            ut_log("cpu max: %d MHz (verified)", mhz);
    } else if (rc == 0) {
        /* rule 4: the driver clamped the request without failing */
        ut_log("cpu max: %d MHz requested, kernel kept %d MHz on %d/%d cpus (driver clamp)",
               mhz, back_khz / 1000, bad, hw->cpu_n);
        if (back_khz > 0)
            hw->cpu_mhz_limit = back_khz / 1000;
        return -1;
    } else {
        ut_log("cpu max: FAILED (needs root; no passwordless sudo)");
    }
    return rc;
}

int ctrl_set_cpu_max_mhz_core(hw_state_t *hw, int cpu, int mhz)
{
    /* `cpu` is a kernel cpu number; it must be in the present list */
    if (cpu < 0 || cpu >= HW_CPU_MAX || !hw_cpu_present(hw, cpu))
        return -1;
    /* no pre-clamp to hw->cpu_mhz_max either — see the all-cores
     * variant: under Quiet that snapshot is the base clock and would
     * silently rewrite the request; the read-back verify below is the
     * honest limit */

    char path[96], val[24];
    snprintf(path, sizeof(path),
             "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", cpu);
    snprintf(val, sizeof(val), "%d", mhz * 1000);
    int rc = ut_priv_write(path, val);
    if (rc != 0) {
        ut_log("cpu %d max: FAILED (needs root; no passwordless sudo)", cpu);
        return rc;
    }
    hw->cpu_mhz_core[cpu] = mhz;
    if (mhz > hw->cpu_mhz_limit)
        hw->cpu_mhz_limit = mhz;

    /* rule 4: verify by reading back */
    int back = ut_read_int(path) / 1000;
    if (back == mhz) {
        ut_log("cpu %d max: %d MHz (verified)", cpu, mhz);
    } else {
        ut_log("cpu %d max: %d MHz (VERIFY FAILED: reads %d)", cpu, mhz, back);
        rc = -1;
    }
    return rc;
}

/* live active platform profile; -2 unreadable */
static int active_profile_read(void)
{
    char b[32];
    if (ut_read_file("/sys/firmware/acpi/platform_profile", b, sizeof(b)) != 0)
        return -2;
    return hw_profile_from_name(b);
}

/* asusd power-source profile takeover. ac/bat: -2 leave alone,
 * -1 stop auto-switching, 0..2 the hw_profile_t to enforce. Values
 * travel as NAMES (asusd's numeric enum differs from ours); the
 * change-flags go over D-Bus (busctl) because asusctl has no flag
 * for them. No daemon restart — property writes persist to asusd.ron.
 * Verification reads the daemon's live state: the ron file flushes
 * asynchronously and races an immediate re-read. */
int ctrl_set_asusd_auto(hw_state_t *hw, int ac, int bat)
{
    if (!hw->has_asusctl) {
        ut_log("asusd auto-profile: asusctl not available");
        return -1;
    }
    int rc = 0;
    /* asusctl profile set -a/-b also applies the target profile
     * immediately when running on that power source — the user asked
     * for a future-behavior change, so remember the current mode */
    int prev_active = active_profile_read();

    for (int side = 0; side < 2; side++) {
        int mode = side == 0 ? ac : bat;
        if (mode == -2)
            continue;
        const char *flag = side == 0 ? "ChangePlatformProfileOnAc"
                                     : "ChangePlatformProfileOnBattery";
        const char *opt = side == 0 ? "-a" : "-b";
        const char *which = side == 0 ? "AC" : "battery";
        char cmd[256];

        snprintf(cmd, sizeof(cmd),
                 "busctl set-property xyz.ljones.Asusd /xyz/ljones "
                 "xyz.ljones.Platform %s b %s", flag,
                 mode >= 0 ? "true" : "false");
        if (ut_exec(cmd, NULL, 0) != 0) {
            ut_log("asusd %s auto-profile: busctl FAILED", which);
            rc = -1;
            continue;
        }
        if (mode >= 0) {
            snprintf(cmd, sizeof(cmd), "asusctl profile set %s %s", opt,
                     hw_profile_name((hw_profile_t)mode));
            if (ut_exec(cmd, NULL, 0) != 0) {
                ut_log("asusd %s auto-profile: asusctl FAILED", which);
                rc = -1;
            }
        }
    }

    /* asusctl's side-effect may have moved the active mode; prev_active
     * (read above, after any explicit profile write) is what the user
     * wants the mode to be — put it back if it moved */
    if (prev_active >= 0) {
        int now = active_profile_read();
        if (now >= 0 && now != prev_active) {
            char cmd[128];
            snprintf(cmd, sizeof(cmd), "asusctl profile set %s",
                     hw_profile_name((hw_profile_t)prev_active));
            if (ut_exec(cmd, NULL, 0) == 0) {
                hw->profile = (hw_profile_t)prev_active;
                ut_log("asusd auto-profile: active mode restored to %s",
                       hw_profile_name((hw_profile_t)prev_active));
            } else {
                ut_log("asusd auto-profile: restore to %s FAILED",
                       hw_profile_name((hw_profile_t)prev_active));
            }
        }
    }

    /* rule 4: verify against the daemon's live state */
    for (int side = 0; side < 2; side++) {
        int mode = side == 0 ? ac : bat;
        if (mode == -2)
            continue;
        const char *which = side == 0 ? "AC" : "battery";
        int flag = hw_asusd_auto_flag(side == 0 ? "ChangePlatformProfileOnAc"
                                                : "ChangePlatformProfileOnBattery");
        int prof = flag == 1 ? hw_asusd_auto_profile(side == 0) : -2;
        int got = flag == 0 ? -1 : (flag == 1 && prof >= 0 ? prof : -2);
        if (got != mode) {
            ut_log("asusd %s auto-profile: VERIFY FAILED (daemon reads %d)",
                   which, got);
            rc = -1;
        } else if (side == 0) {
            hw->asusd_ac = got;
        } else {
            hw->asusd_bat = got;
        }
    }
    if (rc == 0)
        ut_log("asusd auto-profile: AC %s · battery %s (verified)",
               hw->asusd_ac == -1 ? "off" : hw_profile_name((hw_profile_t)hw->asusd_ac),
               hw->asusd_bat == -1 ? "off" : hw_profile_name((hw_profile_t)hw->asusd_bat));
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

void ctrl_ppt_ryzen_cmd(const char *path, int spl, int sppt, int fppt,
                        char *out, size_t n)
{
    snprintf(out, n, "sudo -n %s -a %d -c %d -b %d",
             path && path[0] ? path : "ryzenadj",
             spl * 1000, sppt * 1000, fppt * 1000);
}

/* one PPT write through the resolved interface (hw->ppt_mode). Values
 * arrive already clamped and ordered. ryzenadj invocation paths are
 * tried in order: the sudoers rule must match the invoked path
 * LITERALLY (Arch merged-usr keeps /usr/sbin a symlink to bin — same
 * inode, but sudoers matching is by string), then the bare name as a
 * portable last resort (sudo resolves it via its own secure_path). */
static int ppt_write(hw_state_t *hw, int spl, int sppt, int fppt)
{
    switch (hw->ppt_mode) {
    case HW_PPT_ARMOURY: {
        const char *attrs[3] = { "ppt_pl1_spl", "ppt_pl2_sppt", "ppt_pl3_fppt" };
        int vals[3];
        int rc = 0;
        vals[0] = spl; vals[1] = sppt; vals[2] = fppt;
        for (int i = 0; i < 3; i++) {
            char path[384], val[16];
            snprintf(path, sizeof(path), "%s/%s/current_value",
                     ARMOURY_ATTR, attrs[i]);
            snprintf(val, sizeof(val), "%d", vals[i]);
            if (ut_priv_write(path, val) != 0)
                rc = -1;
        }
        return rc;
    }
    case HW_PPT_RYZENADJ: {
        static const char *const paths[] = {
            "/usr/bin/ryzenadj", "/usr/sbin/ryzenadj", "ryzenadj",
        };
        for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
            char cmd[160];
            ctrl_ppt_ryzen_cmd(paths[i], spl, sppt, fppt, cmd, sizeof(cmd));
            if (ut_exec(cmd, NULL, 0) == 0)
                return 0;
        }
        return -1;
    }
    default: {
        int rc = 0;
        if (nbwmi_write("ppt_pl1_spl", spl) != 0)   rc = -1;
        if (nbwmi_write("ppt_pl2_sppt", sppt) != 0) rc = -1;
        if (nbwmi_write("ppt_fppt", fppt) != 0)     rc = -1;
        return rc;
    }
    }
}

static void ppt_log_ok(const hw_state_t *hw, int spl, int sppt, int fppt)
{
    switch (hw->ppt_mode) {
    case HW_PPT_ARMOURY:
        ut_log("ppt: %d/%d/%d W via armoury", spl, sppt, fppt);
        break;
    case HW_PPT_RYZENADJ:
        /* Dragon Range exposes no SMU read-back (monitoring table
         * unsupported): field-verified 2026-10-08, never claimed live.
         * SMU limits are volatile — a reboot, suspend or platform
         * profile change can silently clear them; re-apply after. */
        ut_log("ppt: %d/%d/%d W via ryzenadj (no read-back on this platform "
               "— re-apply after reboot/profile change)", spl, sppt, fppt);
        break;
    default:
        ut_log("ppt: %d/%d/%d W via legacy nb-wmi (unverifiable on this "
               "platform)", spl, sppt, fppt);
    }
}

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
        /* real firmware-attribute file names are min_value/max_value
         * (the earlier "min"/"max" reads never existed → silently fell
         * back to these defaults everywhere) */
        snprintf(path, sizeof(path), "%s/%s/min_value", ARMOURY_ATTR, req[i].attr);
        if (ut_read_file(path, buf, sizeof(buf)) == 0 && atoi(buf) > 0)
            *req[i].min = atoi(buf);
        snprintf(path, sizeof(path), "%s/%s/max_value", ARMOURY_ATTR, req[i].attr);
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

    int rc = ppt_write(hw, spl, sppt, fppt);
    if (rc == 0) {
        hw->ppt_spl = spl;
        hw->ppt_sppt = sppt;
        hw->ppt_fppt = fppt;
        hw->ppt_off = false;
        ppt_log_ok(hw, spl, sppt, fppt);
    } else {
        ut_log("ppt: FAILED (needs the sudoers rule for the write path — see --doctor)");
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
    int rc = ppt_write(hw, smax, pmax, fmax);

    if (rc == 0) {
        hw->ppt_spl = smax;
        hw->ppt_sppt = pmax;
        hw->ppt_fppt = fmax;
        hw->ppt_off = true;
        ut_log("ppt: limits removed (%d/%d/%d W maxima — now actually enforced "
               "where the interface works)", smax, pmax, fmax);
    } else {
        ut_log("ppt: removing limits FAILED (needs the sudoers rule)");
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
        /* rule 4: read the node back. The WMI layer may report stale
         * values right after a write, and on laptops the platform can
         * own the effective limit — a mismatch is reported, not faked. */
        int back = ut_read_int(NB_WMI "/nv_dynamic_boost");
        if (back == watts) {
            hw->nv_boost = watts;
            ut_log("nv dynamic boost: %d W (verified)", watts);
        } else {
            hw->nv_boost = watts;
            ut_log("nv dynamic boost: %d W written (read-back %s — may be "
                   "stale or platform-owned)",
                   watts, back >= 0 ? "mismatch" : "unreadable");
        }
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
        int back = ut_read_int(NB_WMI "/nv_temp_target");
        if (back == celsius) {
            hw->nv_temp = celsius;
            ut_log("nv temp target: %d°C (verified)", celsius);
        } else {
            hw->nv_temp = celsius;
            ut_log("nv temp target: %d°C written (read-back %s — may be "
                   "stale or platform-owned)",
                   celsius, back >= 0 ? "mismatch" : "unreadable");
        }
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

/* ---- GPU clock lock ----------------------------------------------------- */

int ctrl_gpu_clock_ok(int mhz, const char *samples)
{
    int v[8];
    int n = ut_parse_ints(samples, v, 8);

    if (n < 1)
        return 0; /* nothing readable: cannot verify */
    for (int i = 0; i < n; i++)
        if (v[i] > mhz + 2) /* +2: the driver snaps to its clock grid */
            return 0;
    return 1;
}

/* the driver reports success ("GPU clocks set to ...") but exposes no
 * lock flag to read back (applications clocks are deprecated on 615+,
 * the event-reason mask ignores the lock) — so rule 4 here means
 * sampling the current clock: every sample must sit at or below the
 * lock. Two load-race guards: a settle delay first (the current clock
 * can still report the pre-lock boost right after the write), then up
 * to three rounds — one stale high sample under load must not fail an
 * applied lock; a consistent overshoot must. */
static int gpu_clock_verify(int mhz, char *samples, size_t n)
{
    usleep(250000);
    for (int round = 0; round < 3; round++) {
        size_t off = 0;
        samples[0] = '\0';
        for (int i = 0; i < 3; i++) {
            char one[32] = {0};
            if (ut_exec("nvidia-smi --query-gpu=clocks.current.graphics "
                        "--format=csv,noheader,nounits", one, sizeof(one)) == 0 &&
                one[0])
                off += (size_t)snprintf(samples + off, n - off, "%s%s",
                                        off ? "," : "", one);
            usleep(120000);
        }
        if (ctrl_gpu_clock_ok(mhz, samples))
            return 1;
        usleep(250000);
    }
    return 0;
}

/* sudo resets PATH, so the binary has to be an absolute path. It is
 * not /usr/sbin/nvidia-smi on NixOS or Debian. */
static int nvidia_smi_bin(char *out, size_t n)
{
    FILE *p = popen("command -v nvidia-smi 2>/dev/null", "r");
    if (!p)
        return -1;
    if (!fgets(out, (int)n, p)) {
        pclose(p);
        out[0] = '\0';
        return -1;
    }
    pclose(p);
    out[strcspn(out, "\r\n")] = '\0';
    if (out[0] != '/' || strchr(out, '\'') || strchr(out, ' '))
        return -1;
    return 0;
}

int ctrl_gpu_clock_lock(hw_state_t *hw, int mhz)
{
    char bin[256];
    if (!hw->has_nvidia_smi || nvidia_smi_bin(bin, sizeof(bin)) != 0) {
        ut_log("gpu clock: nvidia-smi is not on PATH");
        return -1;
    }
    int cap = hw->gpu_mhz_max > 0 ? hw->gpu_mhz_max : 4096;
    mhz = ut_clamp_i(mhz, 200, cap);

    char cmd[512], out[256] = {0};
    snprintf(cmd, sizeof(cmd), "sudo -n '%s' -lgc %d,%d 2>&1", bin, mhz, mhz);
    int rc = ut_exec_raw(cmd, out, sizeof(out));
    if (rc != 0 || !strstr(out, "GPU clocks set to")) {
        ut_log("gpu clock: lock %d FAILED (sudo -n %s) %s",
               mhz, bin, out[0] ? out : "no output");
        return -1;
    }
    char samples[128];
    if (!gpu_clock_verify(mhz, samples, sizeof(samples))) {
        ut_log("gpu clock: lock %d VERIFY FAILED (samples: %s)", mhz, samples);
        return -1;
    }
    hw->gpu_clock_lock = mhz;
    ut_log("gpu clock: locked %d MHz (verified: %s)", mhz, samples);
    return 0;
}

int ctrl_gpu_clock_reset(hw_state_t *hw)
{
    char bin[256], cmd[512];
    if (!hw->has_nvidia_smi || nvidia_smi_bin(bin, sizeof(bin)) != 0) {
        ut_log("gpu clock: nvidia-smi is not on PATH");
        return -1;
    }
    char out[256] = {0};
    snprintf(cmd, sizeof(cmd), "sudo -n '%s' -rgc 2>&1", bin);
    int rc = ut_exec_raw(cmd, out, sizeof(out));
    if (rc != 0) {
        ut_log("gpu clock: reset FAILED (sudo -n %s) %s",
               bin, out[0] ? out : "no output");
        return -1;
    }
    hw->gpu_clock_lock = 0;
    /* no driver flag marks "unlocked": rc + output is all we get */
    ut_log("gpu clock: lock reset (rc=0, not further verifiable)");
    return 0;
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
    if (rc != 0) {
        /* the leds node takes an integer level, not the name — a
         * name write fails EINVAL and the fallback never worked */
        char val[8];
        snprintf(val, sizeof(val), "%d", (int)lvl);
        rc = ut_priv_write("/sys/class/leds/asus::kbd_backlight/brightness", val);
    }
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

static int fan_curve_base(hw_state_t *hw, char *out, size_t n)
{
    /* same path cache the read layer uses, so reads and writes can
     * never disagree after a renumber */
    return hw_path_fan_curve(hw, out, n);
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

/* the value point i takes when the curve has fewer points in use:
 * padding with the last point (what the write loop sends) */
static void fan_point(const hw_state_t *hw, bool cpu, int i, int *temp, int *pwm)
{
    const fan_curve_t *fc = cpu ? &hw->fan_cpu : &hw->fan_gpu;
    int idx = (fc->n > 0) ? (i < fc->n ? i : fc->n - 1) : 0;
    *temp = (fc->n > 0) ? fc->temp_c[idx] : FAN_TMIN;
    *pwm  = (fc->n > 0) ? fc->pwm[idx] : 0;
}

/* pwmN_enable read-back. Points are not enough: the driver keeps them
 * while the curve is off (enable 2). */
static int fan_enable_ok(const char *base, const char *pwm, bool on)
{
    char p[300];
    snprintf(p, sizeof(p), "%s/%s_enable", base, pwm);
    return ut_read_int(p) == fan_enable_raw(on);
}

/* rule 4 — verify the writes by reading back. The custom-curve hwmon
 * reads back what was written (unlike the nb-wmi PPT cache), so a
 * mismatch is a real failure, not staleness. */
static void fan_verify(const hw_state_t *hw, const char *base,
                       int *cpu_ok, int *gpu_ok)
{
    *cpu_ok = 0;
    *gpu_ok = 0;
    char p[300];
    for (int i = 0; i < FAN_POINTS; i++) {
        int tc, pc, tg, pg, t, w;
        fan_point(hw, true, i, &tc, &pc);
        fan_point(hw, false, i, &tg, &pg);

        snprintf(p, sizeof(p), "%s/pwm1_auto_point%d_temp", base, i + 1);
        t = ut_read_int(p);
        snprintf(p, sizeof(p), "%s/pwm1_auto_point%d_pwm", base, i + 1);
        w = ut_read_int(p);
        if (t == tc && w == pc)
            (*cpu_ok)++;

        snprintf(p, sizeof(p), "%s/pwm2_auto_point%d_temp", base, i + 1);
        t = ut_read_int(p);
        snprintf(p, sizeof(p), "%s/pwm2_auto_point%d_pwm", base, i + 1);
        w = ut_read_int(p);
        if (t == tg && w == pg)
            (*gpu_ok)++;
    }
}

int ctrl_fan_write(hw_state_t *hw)
{
    char base[256];
    int rc = 0;
    int enable_bad = 0;
    int vok_cpu = -1, vok_gpu = -1; /* -1: no hwmon, not verified */

    if (fan_curve_base(hw, base, sizeof(base)) == 0) {
        for (int i = 0; i < FAN_POINTS; i++) {
            char path[300];
            int tc, pc, tg, pg;
            fan_point(hw, true, i, &tc, &pc);
            fan_point(hw, false, i, &tg, &pg);
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
        /* Points clear pwmN_enable in the driver. 1 turns the stored
         * curve on; 2 selects factory auto. 0 is rejected. */
        char path[300], val[8];
        int en_cpu = 0, en_gpu = 0;
        snprintf(path, sizeof(path), "%s/pwm1_enable", base);
        snprintf(val, sizeof(val), "%d", fan_enable_raw(hw->fan_cpu_on));
        if (ut_priv_write(path, val) != 0)
            rc = -1;
        snprintf(path, sizeof(path), "%s/pwm2_enable", base);
        snprintf(val, sizeof(val), "%d", fan_enable_raw(hw->fan_gpu_on));
        if (ut_priv_write(path, val) != 0)
            rc = -1;

        fan_verify(hw, base, &vok_cpu, &vok_gpu);
        en_cpu = fan_enable_ok(base, "pwm1", hw->fan_cpu_on);
        en_gpu = fan_enable_ok(base, "pwm2", hw->fan_gpu_on);
        if (!en_cpu || !en_gpu) {
            rc = -1;
            enable_bad = 1;
            ut_log("fan enable read-back: cpu %s, gpu %s",
                   en_cpu ? "ok" : "FAILED", en_gpu ? "ok" : "FAILED");
        }
    }

    /* asusctl persistence: survive asusd restarts. This is also the
     * effective APPLY path when the sysfs nodes are unwritable (no
     * sudoers rule for the hwmon path): asusd writes the EC itself
     * over D-Bus, passwordless. Capture the results honestly. */
    int a_ok = -1; /* -1: not attempted */
    if (hw->has_asusctl) {
        const char *prof = hw_profile_name(hw->profile);
        char data[256], cmd[512];
        int r1, r2, r3, r4;
        fan_curve_data_str(&hw->fan_cpu, data, sizeof(data));
        snprintf(cmd, sizeof(cmd),
                 "asusctl fan-curve --mod-profile %s --fan cpu --data '%s'", prof, data);
        r1 = ut_exec(cmd, NULL, 0);
        snprintf(cmd, sizeof(cmd),
                 "asusctl fan-curve --mod-profile %s --enable-fan-curve %s --fan cpu",
                 prof, hw->fan_cpu_on ? "true" : "false");
        r2 = ut_exec(cmd, NULL, 0);
        fan_curve_data_str(&hw->fan_gpu, data, sizeof(data));
        snprintf(cmd, sizeof(cmd),
                 "asusctl fan-curve --mod-profile %s --fan gpu --data '%s'", prof, data);
        r3 = ut_exec(cmd, NULL, 0);
        snprintf(cmd, sizeof(cmd),
                 "asusctl fan-curve --mod-profile %s --enable-fan-curve %s --fan gpu",
                 prof, hw->fan_gpu_on ? "true" : "false");
        r4 = ut_exec(cmd, NULL, 0);
        a_ok = (r1 == 0 && r2 == 0 && r3 == 0 && r4 == 0) ? 1 : 0;
    }

    if (rc == 0) {
        if (vok_cpu == FAN_POINTS && vok_gpu == FAN_POINTS) {
            ut_log("fan curves written (cpu %s, gpu %s) · verified %d/%d + %d/%d pts, enable ok",
                   hw->fan_cpu_on ? "on" : "off", hw->fan_gpu_on ? "on" : "off",
                   vok_cpu, FAN_POINTS, vok_gpu, FAN_POINTS);
            hw->fan_staged = false; /* written and verified: not staged */
        } else if (vok_cpu >= 0) {
            /* read-back mismatch: keep the staging so a retry 'w' does
             * not get clobbered by the hwmon table in between */
            ut_log("fan curves written (cpu %s, gpu %s) · VERIFY FAILED: cpu %d/%d, gpu %d/%d pts",
                   hw->fan_cpu_on ? "on" : "off", hw->fan_gpu_on ? "on" : "off",
                   vok_cpu, FAN_POINTS, vok_gpu, FAN_POINTS);
        } else {
            ut_log("fan curves written (cpu %s, gpu %s)",
                   hw->fan_cpu_on ? "on" : "off", hw->fan_gpu_on ? "on" : "off");
            hw->fan_staged = false; /* no hwmon: nothing would clobber */
        }
    } else if (a_ok == 1) {
        /* sysfs unwritable, but asusd applied and stored the curves —
         * the user's intent DID land; claiming failure here lied while
         * asusd silently changed the EC (observed 2026-10-08) */
        hw->fan_staged = false;
        ut_log("fan curves applied via asusctl/asusd (sysfs unwritable; "
               "no read-back — verify by ear/temps)");
        rc = 0;
    } else if (enable_bad) {
        /* the enable read-back already logged its failure above */
    } else if (a_ok == 0) {
        ut_log("fan curves: FAILED (sysfs unwritable AND asusctl errored)");
    } else {
        ut_log("fan curves: sysfs write FAILED (needs root; no passwordless sudo)");
    }
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
