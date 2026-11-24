#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "profile.h"
#include "cmds.h"
#include "control.h"
#include "settings.h"
#include "util.h"

#include <ctype.h>
#include <glob.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

/* ---- names ------------------------------------------------------------ */

void profile_sanitize_name(const char *in, char *out, size_t n)
{
    size_t w = 0;
    int prev_dash = 0;
    for (const char *p = in; *p && w + 1 < n; p++) {
        if (isalnum((unsigned char)*p) || *p == '.' || *p == '_') {
            out[w++] = *p;
            prev_dash = 0;
        } else if (*p == '/') {
            if (w == 0 || out[w - 1] == '/')
                continue;
            out[w++] = '/';
            prev_dash = 0;
        } else if (!prev_dash && w > 0 && out[w - 1] != '/') {
            out[w++] = '-';
            prev_dash = 1;
        }
    }
    while (w && (out[w - 1] == '-' || out[w - 1] == '/'))
        w--;
    for (size_t i = 0; i < w; ) {
        size_t j = i;
        while (j < w && out[j] != '/')
            j++;
        size_t len = j - i;
        if ((len == 1 && out[i] == '.') ||
            (len == 2 && out[i] == '.' && out[i + 1] == '.')) {
            out[i] = '-';
            if (len == 2)
                out[i + 1] = '-';
        }
        i = j + 1;
    }
    while (w && (out[w - 1] == '-' || out[w - 1] == '/'))
        w--;
    if (w == 0 && n > 0)
        out[w++] = 'p';
    out[w] = '\0';
}

static void sanitize_name(const char *in, char *out, size_t n)
{
    profile_sanitize_name(in, out, n);
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

static void mkdir_parent(const char *path)
{
    char buf[520];
    snprintf(buf, sizeof(buf), "%s", path);
    char *slash = strrchr(buf, '/');
    if (!slash || slash == buf)
        return;
    *slash = '\0';
    ut_mkdir_p(buf);
}

/* Remove an empty 1-level group subdirectory after deleting/moving the
 * last snapshot inside it (never removes the root profiles/ dir). */
static void cleanup_empty_group_dir(const char *ctr_path)
{
    char root[460], parent[520];
    settings_profiles_dir(root, sizeof(root));
    snprintf(parent, sizeof(parent), "%s", ctr_path);
    char *slash = strrchr(parent, '/');
    if (!slash || slash == parent)
        return;
    *slash = '\0';
    if (strcmp(parent, root) != 0)
        (void)rmdir(parent);
}

static void profile_path(const char *name, char *out, size_t n)
{
    char safe[PROFILE_NAME_MAX];
    sanitize_name(name, safe, sizeof(safe));
    char dir[460];
    settings_profiles_dir(dir, sizeof(dir));
    snprintf(out, n, "%s/%s.ctr", dir, safe);
}

/* ---- list ------------------------------------------------------------- */

static void list_from_glob(char list[][PROFILE_NAME_MAX], int *count, int max,
                           const char *pattern, size_t dirlen)
{
    glob_t g;
    if (glob(pattern, 0, NULL, &g) != 0)
        return;
    for (size_t i = 0; i < g.gl_pathc && *count < max; i++) {
        const char *rel = g.gl_pathv[i] + dirlen + 1;
        const char *dot = strrchr(rel, '.');
        size_t len = dot ? (size_t)(dot - rel) : strlen(rel);
        if (len == 0 || len >= PROFILE_NAME_MAX)
            continue;
        if (strchr(rel, '/') && strchr(strchr(rel, '/') + 1, '/'))
            continue;
        snprintf(list[(*count)++], PROFILE_NAME_MAX, "%.*s", (int)len, rel);
    }
    globfree(&g);
}

static int name_cmp(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

int profile_list(char list[][PROFILE_NAME_MAX], int max)
{
    char dir[460];
    settings_profiles_dir(dir, sizeof(dir));
    ut_mkdir_p(dir);

    char pattern[560];
    int count = 0;
    size_t dirlen = strlen(dir);
    snprintf(pattern, sizeof(pattern), "%s/*.ctr", dir);
    list_from_glob(list, &count, max, pattern, dirlen);
    snprintf(pattern, sizeof(pattern), "%s/*/*.ctr", dir);
    list_from_glob(list, &count, max, pattern, dirlen);
    qsort(list, (size_t)count, PROFILE_NAME_MAX, name_cmp);
    return count;
}

/* ---- model & serialization -------------------------------------------- */

void profile_meta_init(profile_meta_t *m)
{
    memset(m, 0, sizeof(*m));
    m->cpu_boost = -1;
    m->ppt_off = -1;
    m->gpu_clock = -1;
    m->panel_od = -1;
    m->fan_cpu_on = -1;
    m->fan_gpu_on = -1;
}

void profile_meta_from_hw(const hw_state_t *hw, profile_meta_t *m)
{
    profile_meta_init(m);
    snprintf(m->device, sizeof(m->device), "%s", hw->model);
    snprintf(m->profile, sizeof(m->profile), "%s", hw_profile_name(hw->profile));
    snprintf(m->epp, sizeof(m->epp), "%s", hw_epp_name(hw->epp));
    m->cpu_boost = hw->cpu_boost ? 1 : 0;
    m->freq = hw->cpu_mhz_limit > 0 ? hw->cpu_mhz_limit : 0;
    for (int c = 0; c < hw->cpu_n && c < HW_CPU_MAX; c++) {
        int id = hw->cpu_ids[c];
        if (hw->cpu_mhz_core[id] > 0 && hw->cpu_mhz_limit > 0 &&
            hw->cpu_mhz_core[id] < hw->cpu_mhz_limit &&
            m->core_n < HW_CPU_MAX) {
            m->core_ids[m->core_n] = id;
            m->core_mhz[m->core_n] = hw->cpu_mhz_core[id];
            m->core_n++;
        }
    }
    if (hw->ppt_off) {
        m->ppt_off = 1;
    } else if (hw->ppt_spl > 0 && hw->ppt_sppt > 0 && hw->ppt_fppt > 0) {
        m->ppt_spl = hw->ppt_spl;
        m->ppt_sppt = hw->ppt_sppt;
        m->ppt_fppt = hw->ppt_fppt;
    }
    if (hw->gpu_clock_lock > 0)
        m->gpu_clock = hw->gpu_clock_lock;
    m->panel_od = hw->panel_od ? 1 : 0;
    m->hz = hw->hz_cur > 0 ? hw->hz_cur : 0;
    m->bat = hw->bat_limit > 0 ? hw->bat_limit : 80;
    snprintf(m->kbd, sizeof(m->kbd), "%s", hw_kbd_name(hw->kbd));
    m->fan_cpu_on = hw->fan_cpu_on ? 1 : 0;
    m->fan_gpu_on = hw->fan_gpu_on ? 1 : 0;
    fan_to_csv(&hw->fan_cpu, m->fan_cpu_t, sizeof(m->fan_cpu_t),
               m->fan_cpu_p, sizeof(m->fan_cpu_p));
    fan_to_csv(&hw->fan_gpu, m->fan_gpu_t, sizeof(m->fan_gpu_t),
               m->fan_gpu_p, sizeof(m->fan_gpu_p));
}

int profile_save_meta(const char *name, const profile_meta_t *m)
{
    char safe[PROFILE_NAME_MAX];
    sanitize_name(name, safe, sizeof(safe));

    char path[520];
    profile_path(safe, path, sizeof(path));
    mkdir_parent(path);
    char tmp[544];
    FILE *f = ut_atomic_begin(path, tmp, sizeof(tmp));
    if (!f)
        return -1;

    char saved[24] = "";
    time_t now = time(NULL);
    struct tm tm_buf;
    if (localtime_r(&now, &tm_buf))
        strftime(saved, sizeof(saved), "%Y-%m-%d %H:%M", &tm_buf);

    fprintf(f, "# ctron snapshot\n");
    fprintf(f, "# saved: %s\n", saved[0] ? saved : (m->saved[0] ? m->saved : "unknown"));
    if (m->device[0])
        fprintf(f, "# device: %s\n", m->device);
    if (m->note[0])
        fprintf(f, "# note: %s\n", m->note);

    fprintf(f, "[perf]\n");
    if (m->profile[0])
        fprintf(f, "profile = %s\n", m->profile);
    if (m->epp[0])
        fprintf(f, "epp = %s\n", m->epp);
    if (m->cpu_boost >= 0)
        fprintf(f, "cpu-boost = %s\n", m->cpu_boost ? "on" : "off");
    if (m->freq > 0)
        fprintf(f, "freq = %d\n", m->freq);
    for (int i = 0; i < m->core_n && i < HW_CPU_MAX; i++) {
        if (m->core_mhz[i] > 0)
            fprintf(f, "freq = core %d %d\n", m->core_ids[i], m->core_mhz[i]);
    }

    fprintf(f, "[power]\n");
    if (m->ppt_off == 1)
        fprintf(f, "ppt = off\n");
    else if (m->ppt_spl > 0 && m->ppt_sppt > 0 && m->ppt_fppt > 0)
        fprintf(f, "ppt = %d,%d,%d\n", m->ppt_spl, m->ppt_sppt, m->ppt_fppt);
    if (m->gpu_clock == 0)
        fprintf(f, "gpu-clock = reset\n");
    else if (m->gpu_clock > 0)
        fprintf(f, "gpu-clock = %d\n", m->gpu_clock);
    if (m->panel_od >= 0)
        fprintf(f, "panel-od = %s\n", m->panel_od ? "on" : "off");
    if (m->hz > 0)
        fprintf(f, "hz = %d\n", m->hz);
    if (m->bat > 0)
        fprintf(f, "battery = %d\n", m->bat);
    if (m->fan_cpu_on >= 0)
        fprintf(f, "fan_cpu = %d\n", m->fan_cpu_on ? 1 : 0);
    if (m->fan_gpu_on >= 0)
        fprintf(f, "fan_gpu = %d\n", m->fan_gpu_on ? 1 : 0);
    if (m->fan_cpu_t[0] && m->fan_cpu_p[0])
        fprintf(f, "fan-curve cpu = %s %s\n", m->fan_cpu_t, m->fan_cpu_p);
    if (m->fan_gpu_t[0] && m->fan_gpu_p[0])
        fprintf(f, "fan-curve gpu = %s %s\n", m->fan_gpu_t, m->fan_gpu_p);

    fprintf(f, "[light]\n");
    if (m->kbd[0])
        fprintf(f, "kbd = %s\n", m->kbd);

    if (ut_atomic_commit(f, tmp, path) != 0)
        return -1;
    ut_log("snapshot saved: %s", safe);
    return 0;
}

/* ---- export ----------------------------------------------------------- */

void profile_capture_line(const hw_state_t *hw, char *out, size_t n)
{
    char hz[16], fq[16];
    if (hw->hz_cur > 0) snprintf(hz, sizeof(hz), "%d Hz", hw->hz_cur);
    else snprintf(hz, sizeof(hz), "-- Hz");
    if (hw->cpu_mhz_limit > 0) snprintf(fq, sizeof(fq), "%d MHz", hw->cpu_mhz_limit);
    else snprintf(fq, sizeof(fq), "-- MHz");

    int w = snprintf(out, n,
                     "%s · epp %s · %s · boost %s · %s · od %s · bat %d%% · kbd %s · fans %s/%s",
                     hw_profile_name(hw->profile), hw_epp_name(hw->epp),
                     fq, hw->cpu_boost ? "on" : "off",
                     hz, hw->panel_od ? "on" : "off",
                     hw->bat_limit > 0 ? hw->bat_limit : 80,
                     hw_kbd_name(hw->kbd),
                     hw->fan_cpu_on ? "on" : "off", hw->fan_gpu_on ? "on" : "off");
    if (w < 0 || (size_t)w >= n)
        return;
    size_t off = (size_t)w;
    if (hw->gpu_clock_lock > 0 && off < n) {
        w = snprintf(out + off, n - off, " · gpu %d", hw->gpu_clock_lock);
        if (w > 0 && (size_t)w < n - off)
            off += (size_t)w;
    }
    if (hw->ppt_off && off < n) {
        snprintf(out + off, n - off, " · ppt off");
    } else if (hw->ppt_spl > 0 && hw->ppt_sppt > 0 && hw->ppt_fppt > 0 && off < n) {
        snprintf(out + off, n - off, " · ppt %d/%d/%d",
                 hw->ppt_spl, hw->ppt_sppt, hw->ppt_fppt);
    }
}

int profile_export(const char *name, const hw_state_t *hw)
{
    char safe[PROFILE_NAME_MAX];
    sanitize_name(name, safe, sizeof(safe));

    profile_meta_t m, prev;
    profile_meta_from_hw(hw, &m);
    /* preserve an existing note when overwriting/updating a snapshot */
    if (profile_meta_parse(safe, &prev) == 0 && prev.note[0])
        snprintf(m.note, sizeof(m.note), "%s", prev.note);

    /* backward-compat with test_core: export always writes freq line even
     * when synthetic test structs leave cpu_mhz_limit == 0 */
    if (m.freq == 0 && hw->cpu_mhz_limit == 0) {
        char path[520];
        profile_path(safe, path, sizeof(path));
        mkdir_parent(path);
        char tmp[544];
        FILE *f = ut_atomic_begin(path, tmp, sizeof(tmp));
        if (!f)
            return -1;
        char saved[24] = "";
        time_t now = time(NULL);
        struct tm tm_buf;
        if (localtime_r(&now, &tm_buf))
            strftime(saved, sizeof(saved), "%Y-%m-%d %H:%M", &tm_buf);
        fprintf(f, "# ctron snapshot\n# saved: %s\n# device: %s\n",
                saved[0] ? saved : "unknown", hw->model);
        if (m.note[0])
            fprintf(f, "# note: %s\n", m.note);
        fprintf(f, "[perf]\nprofile = %s\nepp = %s\ncpu-boost = %s\nfreq = 0\n",
                m.profile, m.epp, m.cpu_boost ? "on" : "off");
        fprintf(f, "[power]\n");
        if (m.ppt_off == 1)
            fprintf(f, "ppt = off\n");
        else if (m.ppt_spl > 0 && m.ppt_sppt > 0 && m.ppt_fppt > 0)
            fprintf(f, "ppt = %d,%d,%d\n", m.ppt_spl, m.ppt_sppt, m.ppt_fppt);
        if (m.gpu_clock > 0)
            fprintf(f, "gpu-clock = %d\n", m.gpu_clock);
        fprintf(f, "panel-od = %s\n", m.panel_od ? "on" : "off");
        if (m.hz > 0)
            fprintf(f, "hz = %d\n", m.hz);
        fprintf(f, "battery = %d\nfan_cpu = %d\nfan_gpu = %d\n",
                m.bat, m.fan_cpu_on ? 1 : 0, m.fan_gpu_on ? 1 : 0);
        fprintf(f, "fan-curve cpu = %s %s\nfan-curve gpu = %s %s\n",
                m.fan_cpu_t, m.fan_cpu_p, m.fan_gpu_t, m.fan_gpu_p);
        fprintf(f, "[light]\nkbd = %s\n", m.kbd);
        if (ut_atomic_commit(f, tmp, path) != 0)
            return -1;
        ut_log("snapshot exported: %s", safe);
        return 0;
    }

    return profile_save_meta(safe, &m);
}

/* ---- import ----------------------------------------------------------- */

int profile_import(const char *name, hw_state_t *hw, char *err, size_t errn)
{
    char path[520];
    profile_path(name, path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) {
        if (err && errn)
            snprintf(err, errn, "no such snapshot");
        return -1;
    }

    char line[512];
    int fan_cpu_on = -1, fan_gpu_on = -1;
    bool has_curves = false;
    char fc_t[128] = {0}, fc_p[128] = {0}, fg_t[128] = {0}, fg_p[128] = {0};
    char freq_replay[HW_CPU_MAX + 1][64];
    int n_freq = 0;
    int failed = 0;

    int r;
    while ((r = ut_fgets_full(line, sizeof(line), f)) != 0) {
        if (r < 0) {
            ut_log("snapshot '%s': overlong line skipped", name);
            continue;
        }
        char *s = ut_trim(line);
        if (*s == '#' || *s == ';' || *s == '[' || !*s)
            continue;
        const char *key, *val;
        char *eq = strchr(s, '=');
        if (!eq) {
            if (!strncasecmp(s, "freq core ", 10)) {
                key = "freq";
                val = ut_trim(s + 5);
            } else {
                continue;
            }
        } else {
            *eq = '\0';
            key = ut_trim(s);
            val = ut_trim(eq + 1);
        }

        if (!strcasecmp(key, "fan_cpu")) {
            fan_cpu_on = (atoi(val) != 0) ? 1 : 0;
            continue;
        }
        if (!strcasecmp(key, "fan_gpu")) {
            fan_gpu_on = (atoi(val) != 0) ? 1 : 0;
            continue;
        }
        if (!strncasecmp(key, "fan-curve", 9)) {
            const char *side = key + 9;
            while (*side == ' ' || *side == '\t')
                side++;
            char t[96] = {0}, p[96] = {0};
            if (sscanf(val, "%95s %95s", t, p) != 2)
                continue;
            if (!strncasecmp(side, "cpu", 3)) {
                snprintf(fc_t, sizeof(fc_t), "%s", t);
                snprintf(fc_p, sizeof(fc_p), "%s", p);
                has_curves = true;
            } else if (!strncasecmp(side, "gpu", 3)) {
                snprintf(fg_t, sizeof(fg_t), "%s", t);
                snprintf(fg_p, sizeof(fg_p), "%s", p);
                has_curves = true;
            }
            continue;
        }

        /* avoid needless sudo nvidia-smi -rgc if GPU is already unlocked */
        if (!strcasecmp(key, "gpu-clock") &&
            (!strcasecmp(val, "reset") || !strcasecmp(val, "off")) &&
            hw->gpu_clock_lock == 0) {
            continue;
        }

        char serr[128];
        int step_rc = cmd_run(hw, key, val, serr, sizeof(serr));
        if (step_rc != 0) {
            ut_log("snapshot '%s': step '%s' failed (%s)", name, key, serr);
            failed++;
        }
        if (!strcasecmp(key, "freq") && step_rc == 0 && n_freq < HW_CPU_MAX + 1)
            snprintf(freq_replay[n_freq++], sizeof(freq_replay[0]), "%s", val);
    }
    fclose(f);

    if (n_freq > 0) {
        ut_progress("freq re-assert after the profile-switch window");
        usleep(1500000);
        for (int i = 0; i < n_freq; i++) {
            char serr[128];
            if (cmd_run(hw, "freq", freq_replay[i], serr, sizeof(serr)) == 0)
                ut_log("snapshot '%s': freq re-asserted (%s)", name, freq_replay[i]);
        }
    }

    if (fc_t[0] && fc_p[0])
        fan_from_csv(&hw->fan_cpu, fc_t, fc_p);
    if (fg_t[0] && fg_p[0])
        fan_from_csv(&hw->fan_gpu, fg_t, fg_p);
    if (fan_cpu_on >= 0)
        hw->fan_cpu_on = (fan_cpu_on != 0);
    if (fan_gpu_on >= 0)
        hw->fan_gpu_on = (fan_gpu_on != 0);

    if (has_curves) {
        /* caller (APPLY_SNAPSHOT / CLI snapshot apply) runs ctrl_fan_write */
        hw->fan_staged = true;
    } else if (fan_cpu_on >= 0 || fan_gpu_on >= 0) {
        /* enable/disable flags without curve points: apply directly */
        if (ctrl_fan_set_enabled(hw, hw->fan_cpu_on, hw->fan_gpu_on) != 0)
            failed++;
    }

    if (failed && err && errn)
        snprintf(err, errn, "%d step(s) failed (see log)", failed);
    ut_log("snapshot applied: %s", name);
    return failed ? -1 : 0;
}

int profile_dry_run(const char *name, const hw_state_t *hw, FILE *out,
                    char *err, size_t errn)
{
    char path[520];
    profile_path(name, path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) {
        if (err && errn)
            snprintf(err, errn, "no such snapshot");
        return -1;
    }

    char line[512];
    int failed = 0;
    bool has_curves = false;
    int r;
    while ((r = ut_fgets_full(line, sizeof(line), f)) != 0) {
        if (r < 0)
            continue;
        char *s = ut_trim(line);
        if (*s == '#' || *s == ';' || *s == '[' || !*s)
            continue;
        const char *key, *val;
        char *eq = strchr(s, '=');
        if (!eq) {
            if (!strncasecmp(s, "freq core ", 10)) {
                key = "freq";
                val = ut_trim(s + 5);
            } else {
                continue;
            }
        } else {
            *eq = '\0';
            key = ut_trim(s);
            val = ut_trim(eq + 1);
        }
        if (!strcasecmp(key, "fan_cpu") || !strcasecmp(key, "fan_gpu")) {
            if (out)
                fprintf(out, "dry-run: %s = %s\n", key, atoi(val) ? "on" : "off");
            continue;
        }
        if (!strncasecmp(key, "fan-curve", 9)) {
            const char *side = key + 9;
            while (*side == ' ' || *side == '\t')
                side++;
            char t[96] = {0}, p[96] = {0};
            fan_curve_t fc;
            if (sscanf(val, "%95s %95s", t, p) != 2 ||
                fan_from_csv(&fc, t, p) < 0) {
                if (err && errn && !failed)
                    snprintf(err, errn, "fan-curve %s: bad csv", side);
                failed++;
                continue;
            }
            has_curves = true;
            if (out)
                fprintf(out, "dry-run: fan-curve %s = %s %s\n", side, t, p);
            continue;
        }
        char serr[128];
        if (cmd_validate(hw, key, val, serr, sizeof(serr)) != 0) {
            if (err && errn && !failed)
                snprintf(err, errn, "%s: %s", key, serr[0] ? serr : "invalid");
            failed++;
            continue;
        }
        if (out)
            fprintf(out, "dry-run: %s = %s\n", key, val);
    }
    fclose(f);
    if (has_curves && out)
        fprintf(out, "dry-run: fan-write\n");
    return failed ? -1 : 0;
}

/* ---- delete / rename / note -------------------------------------------- */

int profile_delete(const char *name)
{
    char path[520];
    profile_path(name, path, sizeof(path));
    if (unlink(path) == 0) {
        cleanup_empty_group_dir(path);
        ut_log("snapshot deleted: %s", name);
        return 0;
    }
    return -1;
}

int profile_rename(const char *old_name, const char *new_name)
{
    char oldp[520], newp[520], safe[PROFILE_NAME_MAX];
    profile_path(old_name, oldp, sizeof(oldp));
    sanitize_name(new_name, safe, sizeof(safe));
    profile_path(safe, newp, sizeof(newp));
    if (ut_path_exists(newp)) {
        ut_log("snapshot rename: '%s' already exists", safe);
        return -1;
    }
    mkdir_parent(newp);
    if (rename(oldp, newp) != 0)
        return -1;
    cleanup_empty_group_dir(oldp);
    ut_log("snapshot renamed: %s -> %s", old_name, safe);
    return 0;
}

static bool note_append(char *out, size_t n, size_t *off,
                        const char *fmt, ...)
{
    if (*off > n - 2)
        *off = n - 2;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(out + *off, n - *off, fmt, ap);
    va_end(ap);
    bool fit = (w >= 0 && (size_t)w < n - *off);
    if (w > 0)
        *off += (size_t)w;
    if (*off > n - 2)
        *off = n - 2;
    return fit;
}

int profile_set_note(const char *name, const char *note)
{
    char path[520];
    profile_path(name, path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    char out[4096] = "";
    size_t off = 0;
    char line[512];
    bool inserted = !(note && note[0]);
    bool lost = false;
    int r;
    while ((r = ut_fgets_full(line, sizeof(line), f)) != 0) {
        if (r < 0) {
            lost = true;
            break;
        }
        char *s = ut_trim(line);
        if (!strncmp(s, "# note:", 7))
            continue;
        if (!inserted && s[0] && s[0] != '#') {
            if (!note_append(out, sizeof(out), &off, "# note: %s\n", note))
                lost = true;
            inserted = true;
        }
        if (!note_append(out, sizeof(out), &off, "%s\n", s))
            lost = true;
        if (lost)
            break;
    }
    fclose(f);
    if (lost) {
        ut_log("snapshot note: %s not saved (file would not round-trip)", name);
        return -1;
    }

    char tmp[544];
    f = ut_atomic_begin(path, tmp, sizeof(tmp));
    if (!f)
        return -1;
    fputs(out, f);
    if (ut_atomic_commit(f, tmp, path) != 0)
        return -1;
    ut_log("snapshot note: %s %s", name, note && note[0] ? "set" : "cleared");
    return 0;
}

/* ---- summary / metadata parse / live diff ------------------------------ */

void profile_meta_summary(const profile_meta_t *m, char *out, size_t n)
{
    char head[96] = "";
    if (m->note[0] || m->saved[0]) {
        char date[16] = "";
        if (strlen(m->saved) >= 16)
            snprintf(date, sizeof(date), "%.5s %.5s", m->saved + 5, m->saved + 11);
        snprintf(head, sizeof(head), "%s%s%s · ", date,
                 date[0] && m->note[0] ? " " : "", m->note);
    }
    char hz[16], bat[16], ppt[28] = "";
    if (m->hz > 0) snprintf(hz, sizeof(hz), "%dHz", m->hz);
    else snprintf(hz, sizeof(hz), "-");
    if (m->bat > 0) snprintf(bat, sizeof(bat), "bat %d%%", m->bat);
    else snprintf(bat, sizeof(bat), "bat -");
    if (m->ppt_off == 1)
        snprintf(ppt, sizeof(ppt), " · ppt off");
    else if (m->ppt_spl > 0)
        snprintf(ppt, sizeof(ppt), " · %d/%d/%dW",
                 m->ppt_spl, m->ppt_sppt, m->ppt_fppt);

    snprintf(out, n, "%s%s · %s · %s · kbd %s%s", head,
             m->profile[0] ? m->profile : "-", hz, bat,
             m->kbd[0] ? m->kbd : "-", ppt);
}

int profile_summary(const char *name, char *out, size_t n)
{
    profile_meta_t m;
    if (profile_meta_parse(name, &m) != 0)
        return -1;
    profile_meta_summary(&m, out, n);
    return 0;
}

static void record_core_override(profile_meta_t *m, int id, int mhz)
{
    if (id < 0 || id >= HW_CPU_MAX || mhz <= 0)
        return;
    for (int i = 0; i < m->core_n; i++) {
        if (m->core_ids[i] == id) {
            m->core_mhz[i] = mhz;
            return;
        }
    }
    if (m->core_n < HW_CPU_MAX) {
        m->core_ids[m->core_n] = id;
        m->core_mhz[m->core_n] = mhz;
        m->core_n++;
    }
}

int profile_meta_parse(const char *name, profile_meta_t *m)
{
    char path[520];
    profile_path(name, path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    profile_meta_init(m);
    char line[512];
    int r;
    while ((r = ut_fgets_full(line, sizeof(line), f)) != 0) {
        if (r < 0) {
            ut_log("snapshot '%s': overlong line skipped", name);
            continue;
        }
        char *s = ut_trim(line);
        if (!strncmp(s, "# saved:", 8)) {
            snprintf(m->saved, sizeof(m->saved), "%.*s",
                     (int)sizeof(m->saved) - 1, ut_trim(s + 8));
        } else if (!strncmp(s, "# device:", 9)) {
            snprintf(m->device, sizeof(m->device), "%.*s",
                     (int)sizeof(m->device) - 1, ut_trim(s + 9));
        } else if (!strncmp(s, "# note:", 7)) {
            snprintf(m->note, sizeof(m->note), "%.*s",
                     (int)sizeof(m->note) - 1, ut_trim(s + 7));
        } else if (!strncasecmp(s, "freq core ", 10)) {
            int cid = -1, cmhz = 0;
            if (sscanf(s + 10, "%d %d", &cid, &cmhz) == 2)
                record_core_override(m, cid, cmhz);
        } else if (!strncasecmp(s, "fan-curve", 9)) {
            char which[8] = {0}, t[64] = {0}, p[64] = {0};
            if (sscanf(s + 9, " %7s = %63s %63s", which, t, p) != 3)
                continue;
            if (!strcasecmp(which, "cpu")) {
                snprintf(m->fan_cpu_t, sizeof(m->fan_cpu_t), "%s", t);
                snprintf(m->fan_cpu_p, sizeof(m->fan_cpu_p), "%s", p);
            } else if (!strcasecmp(which, "gpu")) {
                snprintf(m->fan_gpu_t, sizeof(m->fan_gpu_t), "%s", t);
                snprintf(m->fan_gpu_p, sizeof(m->fan_gpu_p), "%s", p);
            }
        } else {
            char *eq = strchr(s, '=');
            if (!eq)
                continue;
            *eq = '\0';
            char *key = ut_trim(s);
            char *val = ut_trim(eq + 1);
            if (!strcasecmp(key, "profile"))
                snprintf(m->profile, sizeof(m->profile), "%s", val);
            else if (!strcasecmp(key, "epp"))
                snprintf(m->epp, sizeof(m->epp), "%s", val);
            else if (!strcasecmp(key, "freq")) {
                int cid = -1, cmhz = 0;
                if (!strncasecmp(val, "core ", 5) &&
                    sscanf(val + 5, "%d %d", &cid, &cmhz) == 2)
                    record_core_override(m, cid, cmhz);
                else if (strncasecmp(val, "core", 4) != 0 &&
                         strncasecmp(val, "ccd", 3) != 0)
                    m->freq = atoi(val);
            } else if (!strcasecmp(key, "hz"))
                m->hz = atoi(val);
            else if (!strcasecmp(key, "battery"))
                m->bat = atoi(val);
            else if (!strcasecmp(key, "kbd"))
                snprintf(m->kbd, sizeof(m->kbd), "%s", val);
            else if (!strcasecmp(key, "panel-od"))
                m->panel_od = cmd_parse_bool(val);
            else if (!strcasecmp(key, "cpu-boost"))
                m->cpu_boost = cmd_parse_bool(val);
            else if (!strcasecmp(key, "gpu-clock")) {
                if (!strcasecmp(val, "reset") || !strcasecmp(val, "off"))
                    m->gpu_clock = 0;
                else {
                    int gc = atoi(val);
                    if (gc > 0)
                        m->gpu_clock = gc;
                }
            } else if (!strcasecmp(key, "ppt")) {
                int a, b, c;
                if (!strcasecmp(val, "off")) {
                    m->ppt_off = 1;
                } else if (!strcasecmp(val, "Q45")) {
                    m->ppt_spl = 45; m->ppt_sppt = 55; m->ppt_fppt = 55;
                } else if (!strcasecmp(val, "B60")) {
                    m->ppt_spl = 60; m->ppt_sppt = 75; m->ppt_fppt = 75;
                } else if (!strcasecmp(val, "P80")) {
                    m->ppt_spl = 80; m->ppt_sppt = 80; m->ppt_fppt = 80;
                } else if (sscanf(val, "%d,%d,%d", &a, &b, &c) == 3 &&
                           a > 0 && b > 0 && c > 0) {
                    m->ppt_spl = a; m->ppt_sppt = b; m->ppt_fppt = c;
                }
            } else if (!strcasecmp(key, "fan_cpu"))
                m->fan_cpu_on = atoi(val) ? 1 : 0;
            else if (!strcasecmp(key, "fan_gpu"))
                m->fan_gpu_on = atoi(val) ? 1 : 0;
        }
    }
    fclose(f);
    return 0;
}

static int curve_differs(const char *t, const char *p, const fan_curve_t *live)
{
    fan_curve_t fc;
    if (!t[0] || !p[0])
        return 0;
    if (fan_from_csv(&fc, t, p) < 0)
        return 1;
    if (fc.n != live->n)
        return 1;
    for (int i = 0; i < fc.n && i < FAN_POINTS; i++)
        if (fc.temp_c[i] != live->temp_c[i] || fc.pwm[i] != live->pwm[i])
            return 1;
    return 0;
}

static bool cores_differ(const hw_state_t *hw, const profile_meta_t *m)
{
    for (int i = 0; i < m->core_n; i++) {
        int id = m->core_ids[i];
        if (id >= 0 && id < HW_CPU_MAX && hw->cpu_mhz_core[id] > 0 &&
            abs(hw->cpu_mhz_core[id] - m->core_mhz[i]) > 2)
            return true;
    }
    /* if snapshot sets an aggregate freq with no per-core caps, check if
     * any live core is currently capped below the snapshot's ceiling */
    if (m->freq > 0 && m->core_n == 0) {
        for (int c = 0; c < hw->cpu_n && c < HW_CPU_MAX; c++) {
            int id = hw->cpu_ids[c];
            if (hw->cpu_mhz_core[id] > 0 &&
                hw->cpu_mhz_core[id] + 2 < m->freq)
                return true;
        }
    }
    return false;
}

static int diff_collect(const hw_state_t *hw, const profile_meta_t *m,
                        char *labels, size_t lcap)
{
    size_t off = 0;
    int ndiff = 0;
    if (labels && lcap > 0)
        labels[0] = '\0';

#define ADD_LABEL(lbl)                                                       \
    do {                                                                     \
        if (labels && lcap > off) {                                          \
            int wl = snprintf(labels + off, lcap - off, "%s%s",              \
                              ndiff ? " · " : "", lbl);                      \
            if (wl > 0 && (size_t)wl < lcap - off)                           \
                off += (size_t)wl;                                           \
        }                                                                    \
        ndiff++;                                                             \
    } while (0)

    if (m->profile[0] && strcasecmp(m->profile, hw_profile_name(hw->profile)))
        ADD_LABEL("profile");
    if (m->epp[0] && strcasecmp(m->epp, hw_epp_name(hw->epp)))
        ADD_LABEL("epp");
    if (m->freq > 0 && hw->cpu_mhz_limit > 0 && abs(m->freq - hw->cpu_mhz_limit) > 2)
        ADD_LABEL("freq");
    if (cores_differ(hw, m))
        ADD_LABEL("cores");
    if (m->hz > 0 && hw->hz_cur > 0 && m->hz != hw->hz_cur)
        ADD_LABEL("hz");
    if (m->bat > 0 && hw->bat_limit > 0 && m->bat != hw->bat_limit)
        ADD_LABEL("battery");
    if (m->kbd[0] && strcasecmp(m->kbd, hw_kbd_name(hw->kbd)))
        ADD_LABEL("kbd");
    if (m->panel_od >= 0 && m->panel_od != (int)hw->panel_od)
        ADD_LABEL("panel-od");
    if (m->cpu_boost >= 0 && m->cpu_boost != (int)hw->cpu_boost)
        ADD_LABEL("cpu-boost");
    if (m->gpu_clock >= 0 && m->gpu_clock != hw->gpu_clock_lock)
        ADD_LABEL("gpu-clock");
    if (m->ppt_off == 1 && !hw->ppt_off)
        ADD_LABEL("ppt");
    else if (m->ppt_spl > 0 && hw->ppt_spl > 0 &&
             (m->ppt_spl != hw->ppt_spl || m->ppt_sppt != hw->ppt_sppt ||
              m->ppt_fppt != hw->ppt_fppt))
        ADD_LABEL("ppt");
    if (m->fan_cpu_on >= 0 && m->fan_cpu_on != (int)hw->fan_cpu_on)
        ADD_LABEL("fan-cpu");
    if (m->fan_gpu_on >= 0 && m->fan_gpu_on != (int)hw->fan_gpu_on)
        ADD_LABEL("fan-gpu");
    if (curve_differs(m->fan_cpu_t, m->fan_cpu_p, &hw->fan_cpu))
        ADD_LABEL("curve-cpu");
    if (curve_differs(m->fan_gpu_t, m->fan_gpu_p, &hw->fan_gpu))
        ADD_LABEL("curve-gpu");

#undef ADD_LABEL
    return ndiff;
}

int profile_meta_diff_count(const hw_state_t *hw, const profile_meta_t *m)
{
    return diff_collect(hw, m, NULL, 0);
}

void profile_meta_diff(const hw_state_t *hw, const profile_meta_t *m,
                       char *out, size_t n)
{
    char labels[160];
    int ndiff = diff_collect(hw, m, labels, sizeof(labels));
    if (ndiff == 0)
        snprintf(out, n, "≡ live");
    else
        snprintf(out, n, "Δ %s", labels);
}
