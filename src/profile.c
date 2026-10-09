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

/* Sanitize to [A-Za-z0-9._-] plus '/' as a group separator: runs of
 * '-' collapse, leading/trailing '-' and '/' trim, "." and ".."
 * segments are rejected (mapped to '-'), empty result falls back to
 * "p". A "oyun/turbo" name maps to profiles/oyun/turbo.ctr. */
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
                continue; /* no leading/double separators */
            out[w++] = '/';
            prev_dash = 0;
        } else if (!prev_dash && w > 0 && out[w - 1] != '/') {
            out[w++] = '-';
            prev_dash = 1;
        }
    }
    while (w && (out[w - 1] == '-' || out[w - 1] == '/'))
        w--;
    /* path safety: "." / ".." segments must never survive */
    for (size_t i = 0; i < w; ) {
        size_t j = i;
        while (j < w && out[j] != '/')
            j++;
        size_t len = j - i;
        if ((len == 1 && out[i] == '.') || (len == 2 && out[i] == '.' && out[i + 1] == '.')) {
            out[i] = '-';
            if (len == 2)
                out[i + 1] = '-';
        }
        i = j + 1;
    }
    /* the segment fix can leave trailing dashes (".." -> "--") */
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
        const char *rel = g.gl_pathv[i] + dirlen + 1; /* skip dir and slash */
        const char *dot = strrchr(rel, '.');
        size_t len = dot ? (size_t)(dot - rel) : strlen(rel);
        if (len == 0 || len >= PROFILE_NAME_MAX)
            continue;
        /* only one grouping level: entries look like "group/name" */
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

/* ---- export ----------------------------------------------------------- */

void profile_capture_line(const hw_state_t *hw, char *out, size_t n)
{
    snprintf(out, n,
             "%s · epp %s · %d MHz · %d Hz · bat %d · kbd %s · fans %s/%s +curves",
             hw_profile_name(hw->profile), hw_epp_name(hw->epp),
             hw->cpu_mhz_limit, hw->hz_cur,
             hw->bat_limit > 0 ? hw->bat_limit : 0,
             hw_kbd_name(hw->kbd),
             hw->fan_cpu_on ? "on" : "off", hw->fan_gpu_on ? "on" : "off");
}

int profile_export(const char *name, const hw_state_t *hw)
{
    char safe[PROFILE_NAME_MAX];
    sanitize_name(name, safe, sizeof(safe));

    char path[520];
    profile_path(safe, path, sizeof(path));
    mkdir_parent(path);
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;

    char ft[128], fp[128], gt[128], gp[128];
    fan_to_csv(&hw->fan_cpu, ft, sizeof(ft), fp, sizeof(fp));
    fan_to_csv(&hw->fan_gpu, gt, sizeof(gt), gp, sizeof(gp));

    char saved[24] = "";
    time_t now = time(NULL);
    struct tm tm_buf;
    if (localtime_r(&now, &tm_buf))
        strftime(saved, sizeof(saved), "%Y-%m-%d %H:%M", &tm_buf);

    fprintf(f, "# ctron snapshot\n");
    fprintf(f, "# saved: %s\n", saved[0] ? saved : "unknown");
    fprintf(f, "# device: %s\n", hw->model);
    fprintf(f, "[perf]\n");
    fprintf(f, "profile = %s\n", hw_profile_name(hw->profile));
    fprintf(f, "epp = %s\n", hw_epp_name(hw->epp));
    fprintf(f, "freq = %d\n", hw->cpu_mhz_limit);
    /* per-core limits ride along when any core is capped below the
     * aggregate; import replays them after the all-cores line. Real
     * kernel cpu ids — the list may have gaps on some machines. */
    for (int c = 0; c < hw->cpu_n && c < HW_CPU_MAX; c++) {
        int id = hw->cpu_ids[c];
        if (hw->cpu_mhz_core[id] > 0 && hw->cpu_mhz_core[id] < hw->cpu_mhz_limit)
            fprintf(f, "freq core %d %d\n", id, hw->cpu_mhz_core[id]);
    }
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
    char freq_replay[4][64];
    int n_freq = 0;
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
        if (!strncasecmp(key, "fan-curve", 9)) {
            /* export writes "fan-curve cpu = temps pwms": after the
             * '=' split the fan side lives in the KEY and only the
             * two csv lists are in val. (The old branch matched the
             * bare key "fan-curve" — which the '=' split can never
             * produce — so snapshot curves never applied at all.) */
            const char *side = key + 9;
            while (*side == ' ' || *side == '\t')
                side++;
            char t[96] = {0}, p[96] = {0};
            if (sscanf(val, "%95s %95s", t, p) != 2)
                continue;
            if (!strncasecmp(side, "cpu", 3)) {
                snprintf(fc_t, sizeof(fc_t), "%s", t);
                snprintf(fc_p, sizeof(fc_p), "%s", p);
            } else if (!strncasecmp(side, "gpu", 3)) {
                snprintf(fg_t, sizeof(fg_t), "%s", t);
                snprintf(fg_p, sizeof(fg_p), "%s", p);
            }
            continue;
        }

        char serr[128];
        int step_rc = cmd_run(hw, key, val, serr, sizeof(serr));
        if (step_rc != 0) {
            ut_log("profile '%s': step '%s' failed (%s)", name, key, serr);
            failed++;
        }
        /* collect freq lines for the late re-assert below — but only
         * ones that SUCCEEDED: a re-assert cannot heal a driver clamp
         * (the honest-fail rounds already exhausted), it only guards
         * against the post-verify re-pin, so replaying a failure just
         * burns another ~8 s of wait rounds */
        if (!strcasecmp(key, "freq") && step_rc == 0 && n_freq < 4)
            snprintf(freq_replay[n_freq++], sizeof(freq_replay[0]), "%s", val);
    }
    fclose(f);

    /* Quiet re-baselines scaling_max to the base clock and amd-pstate
     * does not follow cpuinfo back up when the profile switch re-opens
     * it; intermediate steps (EPP during the transition window) can
     * also re-pin it AFTER the freq line already verified. The last
     * word belongs to freq: once every other step has settled, wait
     * out the transition window and re-assert. */
    if (n_freq > 0) {
        ut_progress("freq re-assert after the profile-switch window");
        usleep(1500000);
        for (int i = 0; i < n_freq; i++) {
            char serr[128];
            if (cmd_run(hw, "freq", freq_replay[i], serr, sizeof(serr)) == 0)
                ut_log("profile '%s': freq re-asserted (%s)", name, freq_replay[i]);
        }
    }

    if (fc_t[0] && fc_p[0])
        fan_from_csv(&hw->fan_cpu, fc_t, fc_p);
    if (fg_t[0] && fg_p[0])
        fan_from_csv(&hw->fan_gpu, fg_t, fg_p);
    hw->fan_cpu_on = fan_cpu_on;
    hw->fan_gpu_on = fan_gpu_on;
    hw->fan_staged = true; /* applied curves are not written to the EC yet */

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
    ut_log("snapshot renamed: %s -> %s", old_name, safe);
    return 0;
}

int profile_set_note(const char *name, const char *note)
{
    char path[520];
    profile_path(name, path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    /* rewrite byte-preserving: drop any old "# note:" line, insert the
     * new one after the leading '#'-comment header block */
    char out[4096] = "";
    size_t off = 0;
    char line[512];
    bool inserted = !(note && note[0]);
    while (fgets(line, sizeof(line), f)) {
        char *s = ut_trim(line);
        if (!strncmp(s, "# note:", 7))
            continue; /* drop the old note */
        if (!inserted && s[0] && s[0] != '#') {
            off += (size_t)snprintf(out + off, sizeof(out) - off,
                                    "# note: %s\n", note);
            inserted = true;
        }
        off += (size_t)snprintf(out + off, sizeof(out) - off, "%s\n", s);
    }
    fclose(f);

    f = fopen(path, "w");
    if (!f)
        return -1;
    fputs(out, f);
    fclose(f);
    ut_log("snapshot note: %s %s", name, note && note[0] ? "set" : "cleared");
    return 0;
}

int profile_summary(const char *name, char *out, size_t n)
{
    profile_meta_t m;
    if (profile_meta_parse(name, &m) != 0)
        return -1;

    char head[96] = "";
    if (m.note[0] || m.saved[0]) {
        char date[16] = "";
        if (m.saved[0]) /* "2026-10-09 21:14" -> "10-09 21:14" */
            snprintf(date, sizeof(date), "%.5s %.5s", m.saved + 5, m.saved + 11);
        snprintf(head, sizeof(head), "%s%s%s · ", date,
                 date[0] && m.note[0] ? " " : "", m.note);
    }
    char hz[12], bat[12];
    if (m.hz > 0)
        snprintf(hz, sizeof(hz), "%d", m.hz);
    else
        snprintf(hz, sizeof(hz), "-");
    if (m.bat > 0)
        snprintf(bat, sizeof(bat), "%d", m.bat);
    else
        snprintf(bat, sizeof(bat), "-");
    snprintf(out, n, "%s%s | %sHz | bat %s%% | kbd %s", head,
             m.profile[0] ? m.profile : "?", hz, bat,
             m.kbd[0] ? m.kbd : "-");
    return 0;
}

/* ---- metadata parse + live diff --------------------------------------- */

int profile_meta_parse(const char *name, profile_meta_t *m)
{
    char path[520];
    profile_path(name, path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    memset(m, 0, sizeof(*m));
    m->fan_cpu_on = m->fan_gpu_on = -1;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *s = ut_trim(line);
        if (!strncmp(s, "# saved:", 8))
            snprintf(m->saved, sizeof(m->saved), "%.*s",
                     (int)sizeof(m->saved) - 1, ut_trim(s + 8));
        else if (!strncmp(s, "# note:", 7))
            snprintf(m->note, sizeof(m->note), "%.*s",
                     (int)sizeof(m->note) - 1, ut_trim(s + 7));
        else if (!strncasecmp(s, "freq core", 9)) {
            continue; /* per-core lines: not part of the aggregate diff */
        }
        else if (!strncasecmp(s, "fan-curve", 9)) {
            /* export format: "fan-curve cpu = temps pwms" */
            char which[8] = {0}, t[48] = {0}, p[48] = {0};
            if (sscanf(s + 9, " %7s = %47s %47s", which, t, p) != 3)
                continue;
            if (!strcasecmp(which, "cpu")) {
                snprintf(m->fan_cpu_t, sizeof(m->fan_cpu_t), "%s", t);
                snprintf(m->fan_cpu_p, sizeof(m->fan_cpu_p), "%s", p);
            } else if (!strcasecmp(which, "gpu")) {
                snprintf(m->fan_gpu_t, sizeof(m->fan_gpu_t), "%s", t);
                snprintf(m->fan_gpu_p, sizeof(m->fan_gpu_p), "%s", p);
            }
        }
        else {
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
            else if (!strcasecmp(key, "freq"))
                m->freq = atoi(val);
            else if (!strcasecmp(key, "hz"))
                m->hz = atoi(val);
            else if (!strcasecmp(key, "battery"))
                m->bat = atoi(val);
            else if (!strcasecmp(key, "kbd"))
                snprintf(m->kbd, sizeof(m->kbd), "%s", val);
            else if (!strcasecmp(key, "fan_cpu"))
                m->fan_cpu_on = atoi(val);
            else if (!strcasecmp(key, "fan_gpu"))
                m->fan_gpu_on = atoi(val);
        }
    }
    fclose(f);
    return 0;
}

static int curve_differs(const char *t, const char *p, const fan_curve_t *live)
{
    fan_curve_t fc;
    if (!t[0] || !p[0])
        return 0; /* absent in the snapshot: never a diff */
    if (fan_from_csv(&fc, t, p) < 0)
        return 1; /* unparsable: honest "differs" */
    if (fc.n != live->n)
        return 1;
    for (int i = 0; i < fc.n && i < FAN_POINTS; i++)
        if (fc.temp_c[i] != live->temp_c[i] || fc.pwm[i] != live->pwm[i])
            return 1;
    return 0;
}

void profile_meta_diff(const hw_state_t *hw, const profile_meta_t *m,
                       char *out, size_t n)
{
    char labels[128] = "";
    size_t off = 0;
    int ndiff = 0;

#define ADD_LABEL(lbl)                                                     \
    do {                                                                   \
        off += (size_t)snprintf(labels + off, sizeof(labels) - off, "%s%s", \
                                ndiff ? " · " : "", lbl);                  \
        ndiff++;                                                           \
    } while (0)

    if (m->profile[0] && strcasecmp(m->profile, hw_profile_name(hw->profile)))
        ADD_LABEL("profile");
    if (m->epp[0] && strcasecmp(m->epp, hw_epp_name(hw->epp)))
        ADD_LABEL("epp");
    if (m->freq > 0 && m->freq != hw->cpu_mhz_limit)
        ADD_LABEL("freq");
    if (m->hz > 0 && m->hz != hw->hz_cur)
        ADD_LABEL("hz");
    if (m->bat > 0 && m->bat != hw->bat_limit)
        ADD_LABEL("battery");
    if (m->kbd[0] && strcasecmp(m->kbd, hw_kbd_name(hw->kbd)))
        ADD_LABEL("kbd");
    if (m->fan_cpu_on >= 0 && m->fan_cpu_on != (int)hw->fan_cpu_on)
        ADD_LABEL("fan-cpu");
    if (m->fan_gpu_on >= 0 && m->fan_gpu_on != (int)hw->fan_gpu_on)
        ADD_LABEL("fan-gpu");
    if (curve_differs(m->fan_cpu_t, m->fan_cpu_p, &hw->fan_cpu))
        ADD_LABEL("curve-cpu");
    if (curve_differs(m->fan_gpu_t, m->fan_gpu_p, &hw->fan_gpu))
        ADD_LABEL("curve-gpu");

#undef ADD_LABEL

    if (ndiff == 0)
        snprintf(out, n, "≡ live");
    else
        snprintf(out, n, "Δ %s", labels);
}
