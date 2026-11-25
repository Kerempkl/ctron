#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "settings.h"
#include "util.h"
#include "modes.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

int ut_sudo_fallback_allowed(void)
{
    return g_prefs.write_pref != PREF_NO_SUDO;
}

prefs_t g_prefs = {
    .poll_ms          = 250,
    .write_pref       = PREF_ASUSCTL_FIRST,
    .theme            = 0,
    .gpu_temp         = true,
    .startup_view     = 0,
    .startup_snapshot = "",
    .swap_left        = false,
    .telem_top        = false,
    .left_pct         = 33,
    .split_pct        = 55,
    .telem_h          = 5,
};

/* ---- boot_id & startup_snapshot helpers ------------------------------- */

int settings_boot_id(char *out, size_t n)
{
    if (!out || n < 2)
        return -1;
    out[0] = '\0';
    if (ut_read_file("/proc/sys/kernel/random/boot_id", out, n) != 0)
        return -1;
    char *t = ut_trim(out);
    if (!t[0]) {
        out[0] = '\0';
        return -1;
    }
    if (t != out)
        memmove(out, t, strlen(t) + 1);
    return 0;
}

static bool snap_name_valid(const char *s)
{
    if (!s || !s[0])
        return false;
    size_t len = strlen(s);
    if (len >= SETTINGS_SNAP_MAX)
        return false;
    if (s[0] == '/' || s[len - 1] == '/')
        return false;
    const char *seg = s;
    for (size_t i = 0; i <= len; i++) {
        char c = s[i];
        if (c == '/' || c == '\0') {
            size_t slen = (size_t)(&s[i] - seg);
            if (slen == 0)
                return false;
            if ((slen == 1 && seg[0] == '.') ||
                (slen == 2 && seg[0] == '.' && seg[1] == '.'))
                return false;
            seg = &s[i + 1];
            continue;
        }
        if (!(isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-'))
            return false;
    }
    return true;
}

void settings_set_startup_snapshot(const char *name)
{
    if (!name || !name[0] || !snap_name_valid(name)) {
        g_prefs.startup_snapshot[0] = '\0';
        return;
    }
    snprintf(g_prefs.startup_snapshot, sizeof(g_prefs.startup_snapshot),
             "%s", name);
}

static void settings_patch_startup_snapshot(void)
{
    char path[500], tmp[520];
    settings_file(path, sizeof(path));
    FILE *in = fopen(path, "r");
    if (!in)
        return;

    FILE *out = ut_atomic_begin(path, tmp, sizeof(tmp));
    if (!out) {
        fclose(in);
        return;
    }

    char line[256];
    bool replaced = false;
    int r;
    while ((r = ut_fgets_full(line, sizeof(line), in)) != 0) {
        if (r < 0) {
            /* refuse non-round-trip rewrite (A5 contract) */
            fclose(in);
            fclose(out);
            unlink(tmp);
            return;
        }
        char copy[256];
        snprintf(copy, sizeof(copy), "%s", line);
        char *eq = strchr(copy, '=');
        if (eq) {
            *eq = '\0';
            if (!strcmp(ut_trim(copy), "startup_snapshot")) {
                if (!replaced) {
                    fprintf(out, "startup_snapshot = %s\n",
                            g_prefs.startup_snapshot);
                    replaced = true;
                }
                continue;
            }
        }
        size_t l = strlen(line);
        fputs(line, out);
        if (l == 0 || line[l - 1] != '\n')
            fputc('\n', out);
    }
    fclose(in);

    if (!replaced)
        fprintf(out, "startup_snapshot = %s\n", g_prefs.startup_snapshot);
    if (ut_atomic_commit(out, tmp, path) != 0)
        ut_log("settings: cannot update startup_snapshot in %s", path);
}

void settings_on_snapshot_renamed(const char *old_name, const char *new_name)
{
    if (!g_prefs.startup_snapshot[0] || !old_name || !new_name)
        return;
    if (strcmp(g_prefs.startup_snapshot, old_name) != 0)
        return;
    settings_set_startup_snapshot(new_name);
    settings_patch_startup_snapshot();
    ut_log("startup snapshot updated: %s -> %s",
           old_name, g_prefs.startup_snapshot[0] ? g_prefs.startup_snapshot : "off");
}

void settings_on_snapshot_deleted(const char *name)
{
    if (!g_prefs.startup_snapshot[0] || !name)
        return;
    if (strcmp(g_prefs.startup_snapshot, name) != 0)
        return;
    g_prefs.startup_snapshot[0] = '\0';
    settings_patch_startup_snapshot();
    ut_log("startup snapshot cleared (deleted '%s')", name);
}

/* ---- paths ------------------------------------------------------------ */

void settings_dir(char *out, size_t n)
{
    const char *env = getenv("CTRON_CONFIG");
    if (env && env[0]) {
        snprintf(out, n, "%s", env);
        return;
    }
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) {
        snprintf(out, n, "%s/ctron", xdg);
        return;
    }
    const char *home = getenv("HOME");
    if (!home)
        home = "/tmp";
    snprintf(out, n, "%s/.config/ctron", home);
}

const char *settings_file(char *out, size_t n)
{
    char dir[400];
    settings_dir(dir, sizeof(dir));
    snprintf(out, n, "%s/settings.ini", dir);
    return out;
}

const char *settings_profiles_dir(char *out, size_t n)
{
    char dir[400];
    settings_dir(dir, sizeof(dir));
    snprintf(out, n, "%s/profiles", dir);
    return out;
}

const char *settings_fancurves_dir(char *out, size_t n)
{
    char dir[400];
    settings_dir(dir, sizeof(dir));
    snprintf(out, n, "%s/fan_curves", dir);
    return out;
}

const char *settings_modes_file(char *out, size_t n)
{
    char dir[400];
    settings_dir(dir, sizeof(dir));
    snprintf(out, n, "%s/modes.ini", dir);
    return out;
}

static void ensure_dirs(void)
{
    char dir[400], prof[420], fcdir[420];
    settings_dir(dir, sizeof(dir));
    ut_mkdir_p(dir);
    snprintf(prof, sizeof(prof), "%s/profiles", dir);
    ut_mkdir_p(prof);
    snprintf(fcdir, sizeof(fcdir), "%s/fan_curves", dir);
    ut_mkdir_p(fcdir);
    modes_write_defaults(); /* seeds modes.ini on first run */
}

/* ---- load / save ------------------------------------------------------ */

int settings_load(hw_state_t *hw)
{
    g_prefs.startup_snapshot[0] = '\0';
    char path[500];
    settings_file(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    char ct[128] = {0}, cp[128] = {0}, gt[128] = {0}, gp[128] = {0};
    char saved_boot[SETTINGS_BOOT_ID_MAX] = {0};
    int last_spl = -1, last_sppt = -1, last_fppt = -1;
    int last_ppt_off = -1, last_gpu = -1;
    char line[256];
    int r;
    while ((r = ut_fgets_full(line, sizeof(line), f)) != 0) {
        if (r < 0) {
            ut_log("settings: overlong line skipped in %s", path);
            continue;
        }
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *key = ut_trim(line);
        char *val = ut_trim(eq + 1);

        if (!strcmp(key, "poll_ms")) {
            g_prefs.poll_ms = ut_clamp_i(atoi(val), 100, 2000);
        } else if (!strcmp(key, "write_pref")) {
            g_prefs.write_pref = ut_clamp_i(atoi(val), 0, 2);
        } else if (!strcmp(key, "theme")) {
            g_prefs.theme = ut_clamp_i(atoi(val), 0, 4);
        } else if (!strcmp(key, "gpu_temp")) {
            g_prefs.gpu_temp = (atoi(val) != 0);
        } else if (!strcmp(key, "startup_view")) {
            g_prefs.startup_view = ut_clamp_i(atoi(val), 0, 4);
        } else if (!strcmp(key, "startup_snapshot")) {
            settings_set_startup_snapshot(val);
        } else if (!strcmp(key, "swap_left")) {
            g_prefs.swap_left = (atoi(val) != 0);
        } else if (!strcmp(key, "telem_top")) {
            g_prefs.telem_top = (atoi(val) != 0);
        } else if (!strcmp(key, "left_pct")) {
            g_prefs.left_pct = ut_clamp_i(atoi(val), 25, 50);
        } else if (!strcmp(key, "split_pct")) {
            g_prefs.split_pct = ut_clamp_i(atoi(val), 25, 75);
        } else if (!strcmp(key, "telem_h")) {
            g_prefs.telem_h = ut_clamp_i(atoi(val), 3, 10);
        } else if (!strcmp(key, "cpu_mhz_limit")) {
            /* Live cpufreq read from hw_init is authoritative; only load
             * from settings.ini when no live limit was read (e.g. tests). */
            if (hw->cpu_mhz_limit <= 0) {
                int v = atoi(val);
                if (v >= 100 && v <= 10000)
                    hw->cpu_mhz_limit = v;
            }
        } else if (!strcmp(key, "last_boot_id")) {
            snprintf(saved_boot, sizeof(saved_boot), "%s", val);
        } else if (!strcmp(key, "last_ppt_spl")) {
            int v = atoi(val);
            if (v >= 1 && v <= 500)
                last_spl = v;
        } else if (!strcmp(key, "last_ppt_sppt")) {
            int v = atoi(val);
            if (v >= 1 && v <= 500)
                last_sppt = v;
        } else if (!strcmp(key, "last_ppt_fppt")) {
            int v = atoi(val);
            if (v >= 1 && v <= 500)
                last_fppt = v;
        } else if (!strcmp(key, "last_ppt_off")) {
            last_ppt_off = (atoi(val) != 0) ? 1 : 0;
        } else if (!strcmp(key, "last_gpu_clock")) {
            int v = atoi(val);
            if (v == 0 || (v >= 200 && v <= 8192))
                last_gpu = v;
        } else if (!strcmp(key, "fan_cpu_on")) {
            if (!hw->has_fan_curve)
                hw->fan_cpu_on = (atoi(val) != 0);
        } else if (!strcmp(key, "fan_gpu_on")) {
            if (!hw->has_fan_curve)
                hw->fan_gpu_on = (atoi(val) != 0);
        } else if (!strcmp(key, "fan_cpu_t")) {
            snprintf(ct, sizeof(ct), "%s", val);
        } else if (!strcmp(key, "fan_cpu_p")) {
            snprintf(cp, sizeof(cp), "%s", val);
        } else if (!strcmp(key, "fan_gpu_t")) {
            snprintf(gt, sizeof(gt), "%s", val);
        } else if (!strcmp(key, "fan_gpu_p")) {
            snprintf(gp, sizeof(gp), "%s", val);
        }
    }
    fclose(f);

    /* hwmon fan curves read during hw_init are authoritative at startup.
     * Only populate from settings.ini when no hwmon curve device exists
     * (e.g. unit tests or non-ASUS hardware). Never mark fan_staged at init. */
    if (!hw->has_fan_curve) {
        if (ct[0] && cp[0])
            fan_from_csv(&hw->fan_cpu, ct, cp);
        if (gt[0] && gp[0])
            fan_from_csv(&hw->fan_gpu, gt, gp);
    }

    /* Restore same-boot memory for write-only PPT and NVIDIA GPU clock lock
     * ONLY when the kernel boot_id matches. After a reboot the SMU/driver
     * resets both, so mismatched boot_id entries are silently ignored. */
    char cur_boot[SETTINGS_BOOT_ID_MAX] = {0};
    if (saved_boot[0] && settings_boot_id(cur_boot, sizeof(cur_boot)) == 0 &&
        !strcmp(saved_boot, cur_boot)) {
        if (last_ppt_off == 1) {
            hw->ppt_off = true;
            hw->ppt_set_valid = true;
            hw->ppt_set_off = true;
            if (last_spl > 0 && last_sppt > 0 && last_fppt > 0) {
                hw->ppt_set_spl = last_spl;
                hw->ppt_set_sppt = last_sppt;
                hw->ppt_set_fppt = last_fppt;
            }
        } else if (last_spl > 0 && last_sppt > 0 && last_fppt > 0) {
            hw->ppt_set_valid = true;
            hw->ppt_set_off = false;
            hw->ppt_set_spl = last_spl;
            hw->ppt_set_sppt = last_sppt;
            hw->ppt_set_fppt = last_fppt;
        }
        if (last_gpu >= 200 && hw->gpu_clock_lock <= 0) {
            hw->gpu_clock_lock = last_gpu;
            hw->gpu_clock_from_mem = true;
        }
    }
    return 0;
}

int settings_save(const hw_state_t *hw)
{
    ensure_dirs();
    char path[500], tmp[520];
    settings_file(path, sizeof(path));
    FILE *f = ut_atomic_begin(path, tmp, sizeof(tmp));
    if (!f)
        return -1;

    fprintf(f, "# ctron settings\n");
    fprintf(f, "poll_ms = %d\n", g_prefs.poll_ms);
    fprintf(f, "write_pref = %d\n", g_prefs.write_pref);
    fprintf(f, "theme = %d\n", g_prefs.theme);
    fprintf(f, "gpu_temp = %d\n", g_prefs.gpu_temp ? 1 : 0);
    fprintf(f, "startup_view = %d\n", ut_clamp_i(g_prefs.startup_view, 0, 4));
    fprintf(f, "startup_snapshot = %s\n", g_prefs.startup_snapshot);
    fprintf(f, "cpu_mhz_limit = %d\n", hw->cpu_mhz_limit);
    {
        char cur_boot[SETTINGS_BOOT_ID_MAX] = {0};
        if (settings_boot_id(cur_boot, sizeof(cur_boot)) == 0 && cur_boot[0]) {
            fprintf(f, "last_boot_id = %s\n", cur_boot);
            if (hw->ppt_set_valid) {
                fprintf(f, "last_ppt_spl = %d\n", hw->ppt_set_spl);
                fprintf(f, "last_ppt_sppt = %d\n", hw->ppt_set_sppt);
                fprintf(f, "last_ppt_fppt = %d\n", hw->ppt_set_fppt);
                fprintf(f, "last_ppt_off = %d\n", hw->ppt_set_off ? 1 : 0);
            } else if (hw->ppt_off) {
                fprintf(f, "last_ppt_off = 1\n");
            }
            if (hw->gpu_clock_lock > 0)
                fprintf(f, "last_gpu_clock = %d\n", hw->gpu_clock_lock);
        }
    }
    fprintf(f, "fan_cpu_on = %d\n", hw->fan_cpu_on ? 1 : 0);
    fprintf(f, "fan_gpu_on = %d\n", hw->fan_gpu_on ? 1 : 0);
    {
        char t[128], p[128];
        fan_to_csv(&hw->fan_cpu, t, sizeof(t), p, sizeof(p));
        fprintf(f, "fan_cpu_t = %s\n", t);
        fprintf(f, "fan_cpu_p = %s\n", p);
        fan_to_csv(&hw->fan_gpu, t, sizeof(t), p, sizeof(p));
        fprintf(f, "fan_gpu_t = %s\n", t);
        fprintf(f, "fan_gpu_p = %s\n", p);
    }
    if (ut_atomic_commit(f, tmp, path) != 0) {
        ut_log("settings: cannot save %s", path);
        return -1;
    }
    return 0;
}
