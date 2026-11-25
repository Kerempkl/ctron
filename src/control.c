#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "control.h"
#include "daeboard.h"
#include "util.h"
#include "settings.h"
#include "display/display.h"

#include <errno.h>
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
 * All policies take the SAME value here, so a single `sudo -n tee`
 * with every path as an argument replaces N sequential sudo
 * invocations — 32 popens cost ~4 s per write and a snapshot apply
 * writes several times (epp + freq rounds + the re-assert), which is
 * where a 20 s apply came from. Paths are built explicitly from
 * hw->cpu_ids (the real present list — numbering is not contiguous on
 * every machine), not from a cpu* glob, so every tee argument matches
 * the path-scoped sudoers rule exactly. The whole invocation is a pure
 * argv vector — the value rides the pipe, no shell parses anything
 * (the old printf|sudo pipeline was the last privileged write through
 * /bin/sh, and its 4 KB command string refused machines beyond a few
 * dozen cpus). */
static int cpufreq_write_all(const hw_state_t *hw, const char *leaf,
                             const char *val)
{
    if (!hw || hw->cpu_n <= 0)
        return -1;

    char pathv[HW_CPU_MAX][96];
    int ncpus = hw->cpu_n < HW_CPU_MAX ? hw->cpu_n : HW_CPU_MAX;
    for (int c = 0; c < ncpus; c++)
        snprintf(pathv[c], sizeof pathv[c],
                 "/sys/devices/system/cpu/cpu%d/cpufreq/%s",
                 hw->cpu_ids[c], leaf);

    /* Direct sysfs first (zero forks when running as root or when
     * systemd-tmpfiles.d / udev grants group write access); stop on the
     * first denied node and fall back to a single batched sudo tee. */
    bool direct_ok = true;
    int first_err = 0;
    for (int c = 0; c < ncpus; c++) {
        if (ut_write_file(pathv[c], val) != 0) {
            first_err = errno;
            direct_ok = false;
            break;
        }
    }
    if (direct_ok)
        return 0;

    if (!ut_sudo_fallback_allowed()) {
        if (first_err == EACCES || first_err == EPERM)
            ut_priv_denied_mark();
        return -1;
    }

    /* "timeout -k1 3 sudo -n tee p1 p2 ... pn" — one invocation,
     * explicit paths as argv words, value fed through the pipe */
    const char *av[HW_CPU_MAX + 7];
    int n = 0;

    av[n++] = "timeout";
    av[n++] = "-k1";
    av[n++] = "3";
    av[n++] = "sudo";
    av[n++] = "-n";
    av[n++] = "tee";
    for (int c = 0; c < ncpus; c++)
        av[n++] = pathv[c];
    av[n] = NULL;
    int rc = ut_write_via_argv(av, val);
    if (rc != 0) {
        /* fallback for a sudoers rule that rejects multi-arg tee */
        rc = -1;
        for (int c = 0; c < ncpus; c++) {
            if (ut_priv_write(pathv[c], val) == 0)
                rc = 0;
        }
    }
    if (rc != 0 && (first_err == EACCES || first_err == EPERM))
        ut_priv_denied_mark();
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
        const char *av[] = { "asusctl", "profile", "set", name, NULL };
        rc = ut_exec_argv(av, NULL, 0);
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
    } else if (!ut_sudo_fallback_allowed()) {
        ut_log("epp: FAILED (disabled by write_pref=no sudo)");
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
    int writes_done = 0; /* storm cap: full 32-cpu writes are not free */
    int rc = 0;
    for (int round = 0; round < 8; round++) {
        if (round > 0) {
            ut_progress("CPU ceiling: waiting for the %d MHz window "
                        "(round %d/8)", mhz, round + 1);
            ut_progress_pct(round * 100 / 8);
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
            if (writes_done >= 3)
                continue; /* storm cap: verify-only from here on */

            rc = cpufreq_write_all(hw, "scaling_max_freq", val);
            if (rc != 0)
                break;
            writes_done++;
            ut_progress("CPU ceiling: wrote %d MHz, verifying", mhz);
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
    } else if (!ut_sudo_fallback_allowed()) {
        ut_log("cpu max: FAILED (disabled by write_pref=no sudo)");
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
        if (!ut_sudo_fallback_allowed())
            ut_log("cpu %d max: FAILED (disabled by write_pref=no sudo)", cpu);
        else
            ut_log("cpu %d max: FAILED (needs root; no passwordless sudo)", cpu);
        return rc;
    }
    hw->cpu_mhz_core[cpu] = mhz;
    if (mhz > hw->cpu_mhz_limit)
        hw->cpu_mhz_limit = mhz;

    /* rule 4: verify by reading back (±2 MHz matches the all-cores check
     * so kHz rounding does not produce false failures) */
    int back_khz = ut_read_int(path);
    int back = back_khz > 0 ? back_khz / 1000 : -1;
    if (back > 0 && back - mhz <= 2 && mhz - back <= 2) {
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

        const char *bav[] = {
            "busctl", "set-property", "xyz.ljones.Asusd", "/xyz/ljones",
            "xyz.ljones.Platform", flag, "b", mode >= 0 ? "true" : "false",
            NULL
        };
        if (ut_exec_argv(bav, NULL, 0) != 0) {
            ut_log("asusd %s auto-profile: busctl FAILED", which);
            rc = -1;
            continue;
        }
        if (mode >= 0) {
            const char *aav[] = {
                "asusctl", "profile", "set", opt,
                hw_profile_name((hw_profile_t)mode), NULL
            };
            if (ut_exec_argv(aav, NULL, 0) != 0) {
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
            const char *rav[] = {
                "asusctl", "profile", "set",
                hw_profile_name((hw_profile_t)prev_active), NULL
            };
            if (ut_exec_argv(rav, NULL, 0) == 0) {
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

/* Pure argv builders (no shell): the three integer words point into
 * bufs, which the caller supplies; the NULL-terminated vectors are
 * exec'd verbatim by ut_exec_argv under its 3 s timeout. */
void ctrl_ppt_ryzen_argv(const char *path, int spl, int sppt, int fppt,
                         const char *av[10], char bufs[3][16])
{
    snprintf(bufs[0], sizeof bufs[0], "%d", spl * 1000);
    snprintf(bufs[1], sizeof bufs[1], "%d", sppt * 1000);
    snprintf(bufs[2], sizeof bufs[2], "%d", fppt * 1000);
    av[0] = "sudo";
    av[1] = "-n";
    av[2] = path && path[0] ? path : "ryzenadj";
    av[3] = "-a";
    av[4] = bufs[0];
    av[5] = "-c";
    av[6] = bufs[1];
    av[7] = "-b";
    av[8] = bufs[2];
    av[9] = NULL;
}

void ctrl_ppt_wrapper_argv(const char *path, int spl, int sppt, int fppt,
                           const char *av[7], char bufs[3][16])
{
    snprintf(bufs[0], sizeof bufs[0], "%d", spl * 1000);
    snprintf(bufs[1], sizeof bufs[1], "%d", sppt * 1000);
    snprintf(bufs[2], sizeof bufs[2], "%d", fppt * 1000);
    av[0] = "sudo";
    av[1] = "-n";
    av[2] = path && path[0] ? path : CTRON_PPT_WRAPPER;
    av[3] = bufs[0];
    av[4] = bufs[1];
    av[5] = bufs[2];
    av[6] = NULL;
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
        if (!ut_sudo_fallback_allowed()) {
            ut_priv_denied_mark();
            return -1;
        }
        /* SECURITY.md §4.3: the three-integers wrapper is tried first
         * when installed and executable — the sudoers surface then
         * shrinks to exactly three watt values instead of every
         * ryzenadj flag. Missing wrapper or a denied sudo rule falls
         * through to the direct candidates below. */
        if (access(CTRON_PPT_WRAPPER, X_OK) == 0) {
            const char *av[7];
            char bufs[3][16];
            ctrl_ppt_wrapper_argv(CTRON_PPT_WRAPPER, spl, sppt, fppt,
                                  av, bufs);
            if (ut_exec_argv(av, NULL, 0) == 0)
                return 0;
        }
        static const char *const paths[] = {
            "/usr/bin/ryzenadj", "/usr/sbin/ryzenadj", "ryzenadj",
        };
        for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
            const char *av[10];
            char bufs[3][16];
            ctrl_ppt_ryzen_argv(paths[i], spl, sppt, fppt, av, bufs);
            if (ut_exec_argv(av, NULL, 0) == 0)
                return 0;
        }
        ut_priv_denied_mark();
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

bool ctrl_ppt_limits(const hw_state_t *hw,
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
    int fw_max_n = 0;
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
        if (ut_read_file(path, buf, sizeof(buf)) == 0 && atoi(buf) > 0) {
            *req[i].max = atoi(buf);
            fw_max_n++;
        }
    }
    return fw_max_n == 3;
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
        hw->ppt_set_valid = true;
        hw->ppt_set_spl = spl;
        hw->ppt_set_sppt = sppt;
        hw->ppt_set_fppt = fppt;
        hw->ppt_set_off = false;
        ppt_log_ok(hw, spl, sppt, fppt);
    } else if (!ut_sudo_fallback_allowed()) {
        ut_log("ppt: FAILED (disabled by write_pref=no sudo)");
    } else {
        ut_log("ppt: FAILED (needs the sudoers rule for the write path — see --doctor)");
    }
    return rc;
}

int ctrl_ppt_off(hw_state_t *hw)
{
    int smin, smax, pmin, pmax, fmin, fmax;
    if (!ctrl_ppt_limits(hw, &smin, &smax, &pmin, &pmax, &fmin, &fmax)) {
        ut_log("ppt: cannot remove limits — BIOS exposes no safe maxima "
               "(set SPL/SPPT/FPPT manually)");
        return -1;
    }

    if (!hw->ppt_off) {
        hw->ppt_saved_spl  = hw->ppt_spl  > 0 ? hw->ppt_spl  : (hw->ppt_set_valid ? hw->ppt_set_spl  : 0);
        hw->ppt_saved_sppt = hw->ppt_sppt > 0 ? hw->ppt_sppt : (hw->ppt_set_valid ? hw->ppt_set_sppt : 0);
        hw->ppt_saved_fppt = hw->ppt_fppt > 0 ? hw->ppt_fppt : (hw->ppt_set_valid ? hw->ppt_set_fppt : 0);
    }

    int rc = ppt_write(hw, smax, pmax, fmax);

    if (rc == 0) {
        hw->ppt_spl = smax;
        hw->ppt_sppt = pmax;
        hw->ppt_fppt = fmax;
        hw->ppt_off = true;
        hw->ppt_set_valid = true;
        hw->ppt_set_spl = smax;
        hw->ppt_set_sppt = pmax;
        hw->ppt_set_fppt = fmax;
        hw->ppt_set_off = true;
        ut_log("ppt: limits removed (%d/%d/%d W maxima — now actually enforced "
               "where the interface works)", smax, pmax, fmax);
    } else if (!ut_sudo_fallback_allowed()) {
        ut_log("ppt: removing limits FAILED (disabled by write_pref=no sudo)");
    } else {
        ut_log("ppt: removing limits FAILED (needs the sudoers rule)");
    }
    return rc;
}

int ctrl_ppt_restore(hw_state_t *hw)
{
    if (hw->ppt_saved_spl <= 0 && hw->ppt_saved_sppt <= 0 && hw->ppt_saved_fppt <= 0) {
        hw->ppt_off = false;
        hw->ppt_set_off = false;
        hw->ppt_set_valid = false;
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
        char val[16];
        snprintf(val, sizeof(val), "%d", pct);
        const char *av[] = { "asusctl", "battery", "limit", val, NULL };
        rc = ut_exec_argv(av, NULL, 0);
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
    const char *av[] = { "asusctl", "battery", "oneshot", NULL };
    int rc = ut_exec_argv(av, NULL, 0);
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
            const char *q[] = { "nvidia-smi",
                                "--query-gpu=clocks.current.graphics",
                                "--format=csv,noheader,nounits", NULL };
            if (ut_exec_argv(q, one, sizeof(one)) == 0 && one[0]) {
                int w = snprintf(samples + off, n - off, "%s%s",
                                 off ? "," : "", one);
                if (w < 0 || (size_t)w >= n - off)
                    break;
                off += (size_t)w;
            }
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
    if (ut_find_in_path("nvidia-smi", out, n) != 0)
        return -1;
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

    if (!ut_sudo_fallback_allowed()) {
        ut_priv_denied_mark();
        ut_log("gpu clock: lock %d FAILED (disabled by write_pref=no sudo)",
               mhz);
        return -1;
    }

    char out[256] = {0};
    char pair[24];
    snprintf(pair, sizeof(pair), "%d,%d", mhz, mhz);
    const char *av[] = { "sudo", "-n", bin, "-lgc", pair, NULL };
    int rc = ut_exec_argv_both(av, out, sizeof(out));
    if (rc != 0 || !strstr(out, "GPU clocks set to")) {
        if (rc != 0 && (strstr(out, "sudo:") || strstr(out, "password")))
            ut_priv_denied_mark();
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
    hw->gpu_clock_from_mem = false;
    ut_log("gpu clock: locked %d MHz (verified: %s)", mhz, samples);
    return 0;
}

int ctrl_gpu_clock_reset(hw_state_t *hw)
{
    char bin[256];
    if (!hw->has_nvidia_smi || nvidia_smi_bin(bin, sizeof(bin)) != 0) {
        ut_log("gpu clock: nvidia-smi is not on PATH");
        return -1;
    }
    if (!ut_sudo_fallback_allowed()) {
        ut_priv_denied_mark();
        ut_log("gpu clock: reset FAILED (disabled by write_pref=no sudo)");
        return -1;
    }
    char out[256] = {0};
    const char *av[] = { "sudo", "-n", bin, "-rgc", NULL };
    int rc = ut_exec_argv_both(av, out, sizeof(out));
    if (rc != 0) {
        if (strstr(out, "sudo:") || strstr(out, "password"))
            ut_priv_denied_mark();
        ut_log("gpu clock: reset FAILED (sudo -n %s) %s",
               bin, out[0] ? out : "no output");
        return -1;
    }
    hw->gpu_clock_lock = 0;
    hw->gpu_clock_from_mem = false;
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
        const char *av[] = { "asusctl", "leds", "set", hw_kbd_name(lvl), NULL };
        rc = ut_exec_argv(av, NULL, 0);
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

int ctrl_aura_supported(const hw_state_t *hw, int effect_idx)
{
    /* Only "static" has a proven mode number (see hw.h), so only a
     * static-only keyboard can be judged; anything else is left to the
     * write + read-back */
    if (hw_aura_static_only(hw) && effect_idx != ctrl_aura_effect_idx("static"))
        return 0;
    return 1;
}

static bool aura_hex_ok(const char *hex)
{
    return hex && strlen(hex) == 6 &&
           strspn(hex, "0123456789abcdefABCDEF") == 6;
}

int ctrl_set_aura_ex(hw_state_t *hw, int effect_idx, const char *hex)
{
    if (effect_idx < 0 || effect_idx >= AURA_EFFECT_COUNT || !aura_hex_ok(hex))
        return -1;

    const char *effect = AURA_EFFECTS[effect_idx];
    bool is_static = (effect_idx == ctrl_aura_effect_idx("static"));

    if (!ctrl_aura_supported(hw, effect_idx)) {
        ut_log("aura: FAILED — this keyboard only has the static effect");
        return -1;
    }

    if (db_up()) {
        if (!is_static) {
            ut_log("aura: %s stays with daeboard macros, not asusctl", effect);
            return -1;
        }
        if (db_set_color(hex) == 0) {
            ut_log("aura: static #%s via daeboard (unverified)", hex);
            return 0;
        }
        ut_log("aura: daeboard color failed");
        return -1;
    }

    if (!hw->has_asusctl) {
        ut_log("aura: asusctl not available");
        return -1;
    }
    const char *av[12];
    int n = 0;
    av[n++] = "asusctl";
    av[n++] = "aura";
    av[n++] = "effect";

    if (!strcmp(effect, "static") || !strcmp(effect, "pulse") ||
        !strcmp(effect, "comet") || !strcmp(effect, "flash")) {
        av[n++] = effect;
        av[n++] = "-c";
        av[n++] = hex;
    } else if (!strcmp(effect, "highlight") || !strcmp(effect, "laser") ||
               !strcmp(effect, "ripple")) {
        av[n++] = effect;
        av[n++] = "-c";
        av[n++] = hex;
        av[n++] = "--speed";
        av[n++] = "med";
    } else if (!strcmp(effect, "breathe") || !strcmp(effect, "stars")) {
        av[n++] = effect;
        av[n++] = "--colour";
        av[n++] = hex;
        av[n++] = "--colour2";
        av[n++] = "000000";
        av[n++] = "--speed";
        av[n++] = "med";
    } else if (!strcmp(effect, "rainbow-cycle") || !strcmp(effect, "rain")) {
        av[n++] = effect;
        av[n++] = "--speed";
        av[n++] = "med";
    } else if (!strcmp(effect, "rainbow-wave")) {
        av[n++] = "rainbow-wave";
        av[n++] = "--direction";
        av[n++] = "right";
        av[n++] = "--speed";
        av[n++] = "med";
    } else {
        av[n++] = effect;
        av[n++] = "-c";
        av[n++] = hex;
    }
    av[n] = NULL;

    int rc = ut_exec_argv(av, NULL, 0);
    if (rc != 0) {
        ut_log("aura: %s #%s FAILED", effect, hex);
        return -1;
    }
    if (!hw->has_aura) {
        ut_log("aura: %s #%s sent (asusd unreadable — unverified)", effect, hex);
        return 0;
    }

    int want = (int)strtol(hex, NULL, 16);
    /* rainbow-cycle / rain / rainbow-wave take no colour argument at all
     * (see the argv above), so the read-back cannot expect ours */
    bool uses_colour = strcmp(effect, "rainbow-cycle") != 0 &&
                       strcmp(effect, "rain") != 0 &&
                       strcmp(effect, "rainbow-wave") != 0;
    int mode = -1, rgb = -1;
    bool read_ok = false, applied = false;
    for (int try = 0; try < 3 && !applied; try++) {
        if (try)
            usleep(60 * 1000);
        read_ok = hw_aura_read_live(&mode, &rgb) == 0;
        applied = read_ok && (is_static ? mode == HW_AURA_MODE_STATIC : mode > 0) &&
                   (!uses_colour || rgb == want);
    }
    if (read_ok) {
        hw->aura_mode = mode;
        hw->aura_rgb = rgb;
    }
    if (applied) {
        ut_log("aura: %s #%s verified", effect, hex);
        return 0;
    }
    if (read_ok) {
        char lab[24];
        ut_log("aura: %s #%s FAILED — LEDs did not change (live: %s #%06x)",
               effect, hex, hw_aura_mode_label(mode, lab, sizeof(lab)), rgb);
    } else {
        ut_log("aura: %s #%s FAILED — asusd did not answer the read-back",
               effect, hex);
    }
    return -1;
}

int ctrl_set_aura(hw_state_t *hw, int effect_idx, int color_idx)
{
    if (color_idx < 0 || color_idx >= AURA_COLOR_COUNT)
        color_idx = 0;
    return ctrl_set_aura_ex(hw, effect_idx, AURA_COLOR_HEX[color_idx]);
}

int ctrl_set_aura_hex(hw_state_t *hw, const char *hex)
{
    return ctrl_set_aura_ex(hw, ctrl_aura_effect_idx("static"), hex);
}


/* ---- fans ------------------------------------------------------------- */

static int fan_curve_base(hw_state_t *hw, char *out, size_t n)
{
    /* same path cache the read layer uses, so reads and writes can
     * never disagree after a renumber */
    return hw_path_fan_curve(hw, out, n);
}

/* asusctl 6.x prints "[ERROR asusctl] ..." or D-Bus error strings while
 * STILL exiting 0 — inspect combined stdout+stderr before claiming ok. */
static int asusctl_exec_ok(const char *const argv[])
{
    char out[384] = {0};
    int rc = ut_exec_argv_both(argv, out, sizeof(out));
    if (rc != 0)
        return -1;
    if (strstr(out, "ERROR") || strstr(out, "Error:") ||
        strstr(out, "DBus.Error") || strstr(out, "Failed to connect"))
        return -1;
    return 0;
}

/* "30c:40,45c:90,..." for asusctl fan-curve --data (always 8 points). */
static void fan_curve_data_str(const fan_curve_t *fc8, char *out, size_t n)
{
    size_t off = 0;
    out[0] = '\0';
    int cnt = (fc8 && fc8->n > 0 && fc8->n <= FAN_POINTS) ? fc8->n : 0;
    for (int i = 0; i < cnt; i++) {
        int w = snprintf(out + off, n - off, "%s%dc:%d",
                         i ? "," : "", fc8->temp_c[i], fc8->pwm[i]);
        if (w < 0 || (size_t)w >= n - off)
            break;
        off += (size_t)w;
    }
}

/* pwmN_enable read-back. Points are not enough: the driver keeps them
 * while the curve is off (enable 2). */
static int fan_enable_ok(const char *base, const char *pwm, bool on)
{
    char p[300];
    snprintf(p, sizeof(p), "%s/%s_enable", base, pwm);
    return ut_read_int(p) == fan_enable_raw(on);
}

/* Rule 4 — verify the writes by reading back from asus_custom_fan_curve
 * hwmon (world-readable even when sysfs writes go through asusctl/asusd).
 * When a fan's custom curve is disabled (`on == false`), `pwmN_enable == 2`
 * is the effective hardware state; when enabled (`on == true`), all 8
 * temperature+PWM points AND `pwmN_enable == 1` must match. */
static void fan_verify_hw8(const char *base,
                           const fan_curve_t *cpu8, bool cpu_on,
                           const fan_curve_t *gpu8, bool gpu_on,
                           int *cpu_ok, int *gpu_ok,
                           int *en_cpu, int *en_gpu)
{
    *en_cpu = fan_enable_ok(base, "pwm1", cpu_on);
    *en_gpu = fan_enable_ok(base, "pwm2", gpu_on);
    *cpu_ok = cpu_on ? 0 : (*en_cpu ? FAN_POINTS : 0);
    *gpu_ok = gpu_on ? 0 : (*en_gpu ? FAN_POINTS : 0);

    char p[300];
    for (int i = 0; i < FAN_POINTS; i++) {
        int t, w;
        if (cpu_on) {
            snprintf(p, sizeof(p), "%s/pwm1_auto_point%d_temp", base, i + 1);
            t = ut_read_int(p);
            snprintf(p, sizeof(p), "%s/pwm1_auto_point%d_pwm", base, i + 1);
            w = ut_read_int(p);
            if (t == cpu8->temp_c[i] && w == cpu8->pwm[i])
                (*cpu_ok)++;
        }
        if (gpu_on) {
            snprintf(p, sizeof(p), "%s/pwm2_auto_point%d_temp", base, i + 1);
            t = ut_read_int(p);
            snprintf(p, sizeof(p), "%s/pwm2_auto_point%d_pwm", base, i + 1);
            w = ut_read_int(p);
            if (t == gpu8->temp_c[i] && w == gpu8->pwm[i])
                (*gpu_ok)++;
        }
    }
}

static int fan_write_asusctl(const hw_state_t *hw,
                             const fan_curve_t *cpu8,
                             const fan_curve_t *gpu8)
{
    const char *prof = hw_profile_name(hw->profile);
    char data[256];
    int ok = 1;

    if (hw->fan_cpu_on) {
        fan_curve_data_str(cpu8, data, sizeof(data));
        const char *av1[] = {
            "asusctl", "fan-curve", "--mod-profile", prof,
            "--fan", "cpu", "--data", data, NULL
        };
        if (asusctl_exec_ok(av1) != 0)
            ok = 0;
    }
    {
        const char *av2[] = {
            "asusctl", "fan-curve", "--mod-profile", prof,
            "--enable-fan-curve", hw->fan_cpu_on ? "true" : "false",
            "--fan", "cpu", NULL
        };
        if (asusctl_exec_ok(av2) != 0)
            ok = 0;
    }

    if (hw->fan_gpu_on) {
        fan_curve_data_str(gpu8, data, sizeof(data));
        const char *av3[] = {
            "asusctl", "fan-curve", "--mod-profile", prof,
            "--fan", "gpu", "--data", data, NULL
        };
        if (asusctl_exec_ok(av3) != 0)
            ok = 0;
    }
    {
        const char *av4[] = {
            "asusctl", "fan-curve", "--mod-profile", prof,
            "--enable-fan-curve", hw->fan_gpu_on ? "true" : "false",
            "--fan", "gpu", NULL
        };
        if (asusctl_exec_ok(av4) != 0)
            ok = 0;
    }
    return ok;
}

static int fan_write_sysfs(hw_state_t *hw, const char *base,
                           const fan_curve_t *cpu8,
                           const fan_curve_t *gpu8)
{
    static bool sysfs_dead_logged;
    char path[300], val[16];

    /* Probe the first writable node immediately: if permission is denied
     * (no udev/sudoers rule for hwmon*), bail in < 80 ms instead of
     * spawning 34 doomed sudo tee invocations (~3.5 s freeze). */
    for (int i = 0; i < FAN_POINTS; i++) {
        if (hw->fan_cpu_on) {
            snprintf(path, sizeof(path), "%s/pwm1_auto_point%d_temp", base, i + 1);
            snprintf(val, sizeof(val), "%d", cpu8->temp_c[i]);
            if (ut_priv_write(path, val) != 0)
                goto denied;
            snprintf(path, sizeof(path), "%s/pwm1_auto_point%d_pwm", base, i + 1);
            snprintf(val, sizeof(val), "%d", cpu8->pwm[i]);
            if (ut_priv_write(path, val) != 0)
                goto denied;
        }
        if (hw->fan_gpu_on) {
            snprintf(path, sizeof(path), "%s/pwm2_auto_point%d_temp", base, i + 1);
            snprintf(val, sizeof(val), "%d", gpu8->temp_c[i]);
            if (ut_priv_write(path, val) != 0)
                goto denied;
            snprintf(path, sizeof(path), "%s/pwm2_auto_point%d_pwm", base, i + 1);
            snprintf(val, sizeof(val), "%d", gpu8->pwm[i]);
            if (ut_priv_write(path, val) != 0)
                goto denied;
        }
    }

    /* Points clear pwmN_enable in the driver; write enable flags last. */
    snprintf(path, sizeof(path), "%s/pwm1_enable", base);
    snprintf(val, sizeof(val), "%d", fan_enable_raw(hw->fan_cpu_on));
    if (ut_priv_write(path, val) != 0)
        goto denied;
    snprintf(path, sizeof(path), "%s/pwm2_enable", base);
    snprintf(val, sizeof(val), "%d", fan_enable_raw(hw->fan_gpu_on));
    if (ut_priv_write(path, val) != 0)
        goto denied;
    return 0;

denied:
    hw->fan_sysfs_dead = true;
    if (!sysfs_dead_logged) {
        sysfs_dead_logged = true;
        ut_log("fan sysfs path unwritable this session — asusctl path only");
    }
    return -1;
}

fan_write_verdict_t fan_write_verdict(int rc, int vok_cpu, int vok_gpu, int a_ok)
{
    if (rc == 0) {
        if (vok_cpu == FAN_POINTS && vok_gpu == FAN_POINTS)
            return FAN_W_VERIFIED;
        if (vok_cpu >= 0)
            return FAN_W_VERIFY_FAIL;
        /* no fan hwmon: the sysfs block never ran — the asusctl
         * outcome is the ONLY thing that happened */
        return a_ok == 1 ? FAN_W_ASUSCTL : FAN_W_FAILED;
    }
    if (a_ok == 1)
        return FAN_W_ASUSCTL;
    return FAN_W_FAILED;
}

int ctrl_fan_write(hw_state_t *hw)
{
    char base[256];
    int rc = -1;
    int enable_bad = 0;
    int vok_cpu = -1, vok_gpu = -1; /* -1: no hwmon, not verified */
    int a_ok = -1;                  /* -1: not attempted */
    bool sysfs_ok = false;

    /* Always target the real live platform_profile so asusctl --mod-profile
     * modifies the active profile rather than a stale cached one. */
    int live_prof = active_profile_read();
    if (live_prof >= 0)
        hw->profile = (hw_profile_t)live_prof;

    fan_curve_t cpu8, gpu8;
    fan_to_hw8(&hw->fan_cpu, &cpu8);
    fan_to_hw8(&hw->fan_gpu, &gpu8);

    bool have_hwmon = (fan_curve_base(hw, base, sizeof(base)) == 0);

    ut_progress("fan curves: writing to the EC");

    /* Rule 2 chain: asusctl first (passwordless D-Bus + daemon persistence),
     * then direct/sudo sysfs fallback when asusctl is absent, disabled by
     * preference, or fails. */
    if (hw->has_asusctl && use_asusctl_first())
        a_ok = fan_write_asusctl(hw, &cpu8, &gpu8);

    if (have_hwmon && !hw->fan_sysfs_dead && a_ok != 1) {
        if (fan_write_sysfs(hw, base, &cpu8, &gpu8) == 0)
            sysfs_ok = true;
    }

    if (hw->has_asusctl && a_ok < 0)
        a_ok = fan_write_asusctl(hw, &cpu8, &gpu8);

    /* Rule 4: whenever asus_custom_fan_curve hwmon exists, read back both
     * enable flags and all 8+8 curve points — whether written via sysfs
     * OR via asusctl/asusd. Retry briefly in case asusd is still flushing. */
    if (have_hwmon) {
        int en_cpu = 0, en_gpu = 0;
        bool wrote_any = (a_ok == 1 || sysfs_ok);
        int tries = wrote_any ? 3 : 1;
        for (int t = 0; t < tries; t++) {
            if (t > 0)
                usleep(50 * 1000);
            fan_verify_hw8(base, &cpu8, hw->fan_cpu_on,
                           &gpu8, hw->fan_gpu_on,
                           &vok_cpu, &vok_gpu, &en_cpu, &en_gpu);
            if (en_cpu && en_gpu &&
                vok_cpu == FAN_POINTS && vok_gpu == FAN_POINTS)
                break;
        }
        if (wrote_any) {
            if (!en_cpu || !en_gpu) {
                rc = -1;
                a_ok = 0; /* asusd claimed ok, but EC enable did not land */
                enable_bad = 1;
                ut_log("fan enable read-back: cpu %s, gpu %s",
                       en_cpu ? "ok" : "FAILED", en_gpu ? "ok" : "FAILED");
            } else {
                rc = 0;
                if (vok_cpu != FAN_POINTS || vok_gpu != FAN_POINTS)
                    a_ok = 0; /* force FAN_W_VERIFY_FAIL instead of unverified ok */
            }
        }
    }

    switch (fan_write_verdict(rc, vok_cpu, vok_gpu, a_ok)) {
    case FAN_W_VERIFIED:
        if (hw->fan_cpu_on)
            hw->fan_cpu = cpu8;
        if (hw->fan_gpu_on)
            hw->fan_gpu = gpu8;
        hw->fan_staged = false;
        rc = 0;
        ut_log("fan curves written (cpu %s, gpu %s) · verified %d/%d + %d/%d pts, enable ok",
               hw->fan_cpu_on ? "on" : "off", hw->fan_gpu_on ? "on" : "off",
               vok_cpu, FAN_POINTS, vok_gpu, FAN_POINTS);
        break;
    case FAN_W_VERIFY_FAIL:
        /* read-back mismatch: keep staging so retry 'w' is not clobbered */
        ut_log("fan curves written (cpu %s, gpu %s) · VERIFY FAILED: cpu %d/%d, gpu %d/%d pts",
               hw->fan_cpu_on ? "on" : "off", hw->fan_gpu_on ? "on" : "off",
               vok_cpu, FAN_POINTS, vok_gpu, FAN_POINTS);
        rc = -1;
        break;
    case FAN_W_ASUSCTL:
        if (hw->fan_cpu_on)
            hw->fan_cpu = cpu8;
        if (hw->fan_gpu_on)
            hw->fan_gpu = gpu8;
        hw->fan_staged = false;
        ut_log("fan curves applied via asusctl/asusd (no fan hwmon; unverified)");
        rc = 0;
        break;
    case FAN_W_FAILED:
    default:
        if (vok_cpu >= 0) {
            if (enable_bad) {
                /* enable read-back already logged its failure */
            } else if (a_ok == 0) {
                ut_log("fan curves: FAILED (asusctl errored and sysfs unwritable)");
            } else {
                ut_log("fan curves: sysfs write FAILED (needs root; no passwordless sudo)");
            }
        } else if (a_ok == 0) {
            ut_log("fan curves: FAILED (no fan hwmon AND asusctl errored)");
        } else {
            ut_log("fan curves: no write path (no fan hwmon, no asusctl) — curve kept staged");
        }
        rc = -1;
        break;
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
    int live_prof = active_profile_read();
    if (live_prof >= 0)
        hw->profile = (hw_profile_t)live_prof;

    fan_preset_t bp = (hw->profile == HW_QUIET)       ? FAN_PRESET_QUIET
                    : (hw->profile == HW_PERFORMANCE) ? FAN_PRESET_PERF
                                                      : FAN_PRESET_BALANCED;
    fan_curve_t ref_cpu, ref_gpu;
    fan_preset_builtin(bp, &ref_cpu, &ref_gpu);

    switch (preset) {
    case 1: /* silent */
        fan_preset_builtin(FAN_PRESET_QUIET, &ref_cpu, &ref_gpu);
        fan_scale(&hw->fan_cpu, &ref_cpu, 85, 100);
        fan_scale(&hw->fan_gpu, &ref_gpu, 85, 100);
        hw->fan_cpu_on = true;
        hw->fan_gpu_on = true;
        break;
    case 2: /* cool */
        fan_scale(&hw->fan_cpu, &ref_cpu, 125, 100);
        fan_scale(&hw->fan_gpu, &ref_gpu, 125, 100);
        hw->fan_cpu.pwm[FAN_POINTS - 1] = 255;
        hw->fan_gpu.pwm[FAN_POINTS - 1] = 255;
        hw->fan_cpu_on = true;
        hw->fan_gpu_on = true;
        break;
    case 3: /* full */
        hw->fan_cpu = ref_cpu;
        hw->fan_gpu = ref_gpu;
        for (int i = 4; i < FAN_POINTS; i++) {
            hw->fan_cpu.pwm[i] = 255;
            hw->fan_gpu.pwm[i] = 255;
        }
        hw->fan_cpu_on = true;
        hw->fan_gpu_on = true;
        break;
    default: /* stock: genuine factory automatic EC control (Custom OFF) */
        hw->fan_cpu = ref_cpu;
        hw->fan_gpu = ref_gpu;
        hw->fan_cpu_on = false;
        hw->fan_gpu_on = false;
        break;
    }
    return ctrl_fan_write(hw);
}
