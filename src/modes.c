#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "modes.h"
#include "cmds.h"
#include "settings.h"
#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

const char *modes_path(char *out, size_t n)
{
    return settings_modes_file(out, n);
}

static const struct { const char *name, *steps; } default_modes[] = {
    { "turbo",      "profile performance, ppt P80, fan cool, hz max" },
    { "performance","profile performance, fan stock, hz max" },
    { "balanced",   "profile balanced, fan stock" },
    { "quiet",      "profile quiet, fan stock, epp balance_power" },
    { "silent",     "profile quiet, ppt Q45, fan silent, hz 60, epp power" },
};

void modes_write_defaults(void)
{
    char path[500];
    modes_path(path, sizeof(path));
    if (ut_path_exists(path))
        return;

    char dir[460];
    settings_dir(dir, sizeof(dir));
    ut_mkdir_p(dir);

    FILE *f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "# ctron modes — single-command setting bundles.\n");
    fprintf(f, "# Applied with: ctron --mode <name>  (also shown in TUI Settings).\n");
    for (size_t i = 0; i < sizeof(default_modes) / sizeof(default_modes[0]); i++) {
        fprintf(f, "\n[%s]\nsteps = %s\n", default_modes[i].name, default_modes[i].steps);
    }
    fclose(f);
}

int modes_load(mode_def_t *arr, int max)
{
    modes_write_defaults(); /* seeds the file on the very first call */

    char path[500];
    modes_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;

    int n = 0;
    char line[512], section[MODE_NAME_MAX] = {0};
    while (fgets(line, sizeof(line), f) && n < max) {
        char *s = ut_trim(line);
        if (*s == '#' || *s == ';' || !*s)
            continue;
        if (*s == '[') {
            char *end = strchr(s, ']');
            if (end) {
                *end = '\0';
                snprintf(section, sizeof(section), "%s", s + 1);
            }
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq || !section[0])
            continue;
        *eq = '\0';
        char *key = ut_trim(s);
        char *val = ut_trim(eq + 1);
        if (strcasecmp(key, "steps") != 0)
            continue;

        /* new entry (or fill the section found earlier) */
        int idx = mode_find(arr, n, section);
        if (idx < 0 && n < max) {
            idx = n++;
            memset(&arr[idx], 0, sizeof(arr[idx]));
            snprintf(arr[idx].name, MODE_NAME_MAX, "%s", section);
        }
        if (idx >= 0)
            snprintf(arr[idx].steps, MODE_STEPS_MAX, "%s", val);
    }
    fclose(f);
    return n;
}

int modes_save(const mode_def_t *arr, int n)
{
    char path[500];
    modes_path(path, sizeof(path));
    char dir[460];
    settings_dir(dir, sizeof(dir));
    ut_mkdir_p(dir);

    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fprintf(f, "# ctron modes — single-command setting bundles.\n");
    fprintf(f, "# Applied with: ctron --mode <name>  (also shown in TUI Settings).\n");
    for (int i = 0; i < n; i++) {
        if (!arr[i].name[0])
            continue;
        fprintf(f, "\n[%s]\nsteps = %s\n", arr[i].name, arr[i].steps);
    }
    fclose(f);
    return 0;
}

int mode_find(const mode_def_t *arr, int n, const char *name)
{
    for (int i = 0; i < n; i++)
        if (!strcasecmp(arr[i].name, name))
            return i;
    return -1;
}

int mode_apply(hw_state_t *hw, const char *steps, char *err, size_t errn)
{
    char buf[MODE_STEPS_MAX];
    snprintf(buf, sizeof(buf), "%s", steps ? steps : "");

    char *save = NULL;
    for (char *tok = strtok_r(buf, ",", &save); tok;
         tok = strtok_r(NULL, ",", &save)) {
        char *step = ut_trim(tok);
        if (!*step)
            continue;

        /* split "key [rest]" */
        char *sp = strchr(step, ' ');
        char key[32];
        if (sp) {
            size_t kn = (size_t)(sp - step);
            if (kn >= sizeof(key))
                kn = sizeof(key) - 1;
            memcpy(key, step, kn);
            key[kn] = '\0';
        } else {
            snprintf(key, sizeof(key), "%s", step);
        }
        const char *val = sp ? ut_trim(sp + 1) : "";

        char serr[128];
        if (cmd_run(hw, key, val, serr, sizeof(serr)) != 0) {
            if (err && errn)
                snprintf(err, errn, "%s", serr[0] ? serr : "step failed");
            ut_log("mode step FAILED: %s", step);
            return -1;
        }
    }
    return 0;
}
