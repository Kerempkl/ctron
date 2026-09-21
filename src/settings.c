#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "settings.h"
#include "util.h"
#include "modes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

prefs_t g_prefs = {
    .poll_ms   = 250,
    .write_pref = PREF_ASUSCTL_FIRST,
    .theme     = 0,
    .gpu_temp  = true,
    .swap_left  = false,
    .telem_top  = false,
    .left_pct   = 33,
    .split_pct  = 55,
    .telem_h    = 5,
};

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

const char *settings_modes_file(char *out, size_t n)
{
    char dir[400];
    settings_dir(dir, sizeof(dir));
    snprintf(out, n, "%s/modes.ini", dir);
    return out;
}

static void ensure_dirs(void)
{
    char dir[400], prof[420];
    settings_dir(dir, sizeof(dir));
    ut_mkdir_p(dir);
    snprintf(prof, sizeof(prof), "%s/profiles", dir);
    ut_mkdir_p(prof);
    modes_write_defaults(); /* seeds modes.ini on first run */
}

/* ---- load / save ------------------------------------------------------ */

int settings_load(hw_state_t *hw)
{
    char path[500];
    settings_file(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    char ct[128] = {0}, cp[128] = {0}, gt[128] = {0}, gp[128] = {0};
    char line[256];
    while (fgets(line, sizeof(line), f)) {
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
            int v = atoi(val);
            if (v >= 100)
                hw->cpu_mhz_limit = v;
        } else if (!strcmp(key, "fan_cpu_on")) {
            hw->fan_cpu_on = (atoi(val) != 0);
        } else if (!strcmp(key, "fan_gpu_on")) {
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

    if (ct[0] && cp[0])
        fan_from_csv(&hw->fan_cpu, ct, cp);
    if (gt[0] && gp[0])
        fan_from_csv(&hw->fan_gpu, gt, gp);
    return 0;
}

int settings_save(const hw_state_t *hw)
{
    ensure_dirs();
    char path[500];
    settings_file(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;

    fprintf(f, "# ctron settings\n");
    fprintf(f, "poll_ms = %d\n", g_prefs.poll_ms);
    fprintf(f, "write_pref = %d\n", g_prefs.write_pref);
    fprintf(f, "theme = %d\n", g_prefs.theme);
    fprintf(f, "gpu_temp = %d\n", g_prefs.gpu_temp ? 1 : 0);
    fprintf(f, "swap_left = %d\n", g_prefs.swap_left ? 1 : 0);
    fprintf(f, "telem_top = %d\n", g_prefs.telem_top ? 1 : 0);
    fprintf(f, "left_pct = %d\n", g_prefs.left_pct);
    fprintf(f, "split_pct = %d\n", g_prefs.split_pct);
    fprintf(f, "telem_h = %d\n", g_prefs.telem_h);
    fprintf(f, "cpu_mhz_limit = %d\n", hw->cpu_mhz_limit);
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
    fclose(f);
    return 0;
}
