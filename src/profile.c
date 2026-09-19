#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "profile.h"
#include "cmds.h"
#include "settings.h"
#include "util.h"

#include <ctype.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

/* ---- names ------------------------------------------------------------ */

/* Sanitize to [A-Za-z0-9._-], collapse runs, trim, cap length. */
static void sanitize_name(const char *in, char *out, size_t n)
{
    size_t w = 0;
    int prev_dash = 0;
    for (const char *p = in; *p && w + 1 < n; p++) {
        if (isalnum((unsigned char)*p) || *p == '.' || *p == '_') {
            out[w++] = *p;
            prev_dash = 0;
        } else if (!prev_dash && w > 0) {
            out[w++] = '-';
            prev_dash = 1;
        }
    }
    while (w && (out[w - 1] == '-'))
        w--;
    if (w == 0 && n > 0)
        out[w++] = 'p';
    out[w] = '\0';
}

void profile_gen_name(char *out, size_t n)
{
    static const char *tags[] = {
        "game", "eco", "cool", "fast", "calm", "work", "play", "trip"
    };
    srand((unsigned)(time(NULL) ^ getpid()));
    char buf[32];
    snprintf(buf, sizeof(buf), "%s-%03d", tags[rand() % 8], rand() % 1000);
    sanitize_name(buf, out, n);
}

/* ---- paths ------------------------------------------------------------ */

static void profile_path(const char *name, char *out, size_t n)
{
    char safe[PROFILE_NAME_MAX];
    sanitize_name(name, safe, sizeof(safe));
    char dir[460];
    settings_profiles_dir(dir, sizeof(dir));
    snprintf(out, n, "%s/%s.ctr", dir, safe);
}

/* ---- list ------------------------------------------------------------- */

int profile_list(char list[][PROFILE_NAME_MAX], int max)
{
    char dir[460];
    settings_profiles_dir(dir, sizeof(dir));
    ut_mkdir_p(dir);

    char pattern[520];
    snprintf(pattern, sizeof(pattern), "%s/*.ctr", dir);

    glob_t g;
    int count = 0;
    if (glob(pattern, 0, NULL, &g) == 0) {
        for (size_t i = 0; i < g.gl_pathc && count < max; i++) {
            const char *slash = strrchr(g.gl_pathv[i], '/');
            const char *fname = slash ? slash + 1 : g.gl_pathv[i];
            char base[PROFILE_NAME_MAX] = {0};
            snprintf(base, sizeof(base), "%s", fname);
            char *dot = strrchr(base, '.');
            if (dot)
                *dot = '\0';
            if (base[0])
                snprintf(list[count++], PROFILE_NAME_MAX, "%s", base);
        }
        globfree(&g);
    }
    return count;
}

/* ---- export ----------------------------------------------------------- */

int profile_export(const char *name, const hw_state_t *hw)
{
    char safe[PROFILE_NAME_MAX];
    sanitize_name(name, safe, sizeof(safe));

    char dir[460];
    settings_profiles_dir(dir, sizeof(dir));
    ut_mkdir_p(dir);

    char path[520];
    profile_path(safe, path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;

    char ft[128], fp[128], gt[128], gp[128];
    fan_to_csv(&hw->fan_cpu, ft, sizeof(ft), fp, sizeof(fp));
    fan_to_csv(&hw->fan_gpu, gt, sizeof(gt), gp, sizeof(gp));

    fprintf(f, "# ctron profile\n");
    fprintf(f, "# device: %s\n", hw->model);
    fprintf(f, "[perf]\n");
    fprintf(f, "profile = %s\n", hw_profile_name(hw->profile));
    fprintf(f, "epp = %s\n", hw_epp_name(hw->epp));
    fprintf(f, "freq = %d\n", hw->cpu_mhz_limit);
    fprintf(f, "[power]\n");
    if (hw->hz_cur > 0)
        fprintf(f, "hz = %d\n", hw->hz_cur);
    fprintf(f, "battery = %d\n", hw->bat_limit > 0 ? hw->bat_limit : 80);
    fprintf(f, "fan_cpu = %d\n", hw->fan_cpu_on ? 1 : 0);
    fprintf(f, "fan_gpu = %d\n", hw->fan_gpu_on ? 1 : 0);
    fprintf(f, "fan-curve cpu = %s %s\n", ft, fp);
    fprintf(f, "fan-curve gpu = %s %s\n", gt, gp);
    fprintf(f, "[light]\n");
    fprintf(f, "kbd = %s\n", hw_kbd_name(hw->kbd));
    fclose(f);

    ut_log("profile exported: %s", safe);
    return 0;
}

/* ---- import ----------------------------------------------------------- */

int profile_import(const char *name, hw_state_t *hw, char *err, size_t errn)
{
    char path[520];
    profile_path(name, path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) {
        if (err && errn)
            snprintf(err, errn, "no such profile");
        return -1;
    }

    char line[512];
    bool fan_cpu_on = true, fan_gpu_on = true;
    char fc_t[128] = {0}, fc_p[128] = {0}, fg_t[128] = {0}, fg_p[128] = {0};
    int failed = 0;

    while (fgets(line, sizeof(line), f)) {
        char *s = ut_trim(line);
        if (*s == '#' || *s == ';' || *s == '[' || !*s)
            continue;
        char *eq = strchr(s, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *key = ut_trim(s);
        char *val = ut_trim(eq + 1);

        /* fan enable flags ride along with the curves */
        if (!strcasecmp(key, "fan_cpu")) {
            fan_cpu_on = (atoi(val) != 0);
            continue;
        }
        if (!strcasecmp(key, "fan_gpu")) {
            fan_gpu_on = (atoi(val) != 0);
            continue;
        }
        if (!strcasecmp(key, "fan-curve")) {
            char which[8] = {0}, t[96] = {0}, p[96] = {0};
            if (sscanf(val, "%7s %95s %95s", which, t, p) != 3)
                continue;
            if (!strcasecmp(which, "cpu")) {
                snprintf(fc_t, sizeof(fc_t), "%s", t);
                snprintf(fc_p, sizeof(fc_p), "%s", p);
            } else {
                snprintf(fg_t, sizeof(fg_t), "%s", t);
                snprintf(fg_p, sizeof(fg_p), "%s", p);
            }
            continue;
        }

        char serr[128];
        if (cmd_run(hw, key, val, serr, sizeof(serr)) != 0) {
            ut_log("profile '%s': step '%s' failed (%s)", name, key, serr);
            failed++;
        }
    }
    fclose(f);

    if (fc_t[0] && fc_p[0])
        fan_from_csv(&hw->fan_cpu, fc_t, fc_p);
    if (fg_t[0] && fg_p[0])
        fan_from_csv(&hw->fan_gpu, fg_t, fg_p);
    hw->fan_cpu_on = fan_cpu_on;
    hw->fan_gpu_on = fan_gpu_on;

    if (failed && err && errn)
        snprintf(err, errn, "%d step(s) failed (see log)", failed);
    ut_log("profile applied: %s", name);
    return failed ? -1 : 0;
}

/* ---- delete / summary -------------------------------------------------- */

int profile_delete(const char *name)
{
    char path[520];
    profile_path(name, path, sizeof(path));
    if (unlink(path) == 0) {
        ut_log("profile deleted: %s", name);
        return 0;
    }
    return -1;
}

int profile_summary(const char *name, char *out, size_t n)
{
    char path[520];
    profile_path(name, path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    char prof[24] = "?", hz[16] = "-", bat[16] = "-", kbd[16] = "-";
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *key = ut_trim(line);
        char *val = ut_trim(eq + 1);
        if (!strcasecmp(key, "profile"))
            snprintf(prof, sizeof(prof), "%s", val);
        else if (!strcasecmp(key, "hz"))
            snprintf(hz, sizeof(hz), "%s", val);
        else if (!strcasecmp(key, "battery"))
            snprintf(bat, sizeof(bat), "%s", val);
        else if (!strcasecmp(key, "kbd"))
            snprintf(kbd, sizeof(kbd), "%s", val);
    }
    fclose(f);
    snprintf(out, n, "%s | %sHz | bat %s%% | kbd %s", prof, hz, bat, kbd);
    return 0;
}
