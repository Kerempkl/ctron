#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>

#include "cmds.h"
#include "control.h"
#include "daeboard.h"
#include "hw.h"
#include "modes.h"
#include "profile.h"
#include "settings.h"
#include "ui/ui.h"
#include "util.h"
#include "display/display.h"

#define PROG "ctron"
#define VERSION "2.0.0-deno"

/* ---- output helpers ----------------------------------------------------- */

static void print_version(void)
{
    printf("%s %s — ASUS laptop control center\n", PROG, VERSION);
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [flags] | %s mode <sub> | %s profile <sub>\n\n", prog, prog, prog);
    printf("No arguments opens the fullscreen TUI (needs a terminal).\n\n");
    printf("Information:\n");
    printf("  --status, -s        live hardware snapshot\n");
    printf("  --watch, -w         rolling one-line telemetry (Ctrl-C stops)\n");
    printf("  --doctor            capability report\n");
    printf("  --help, -h          this help\n");
    printf("  --version, -V       version\n\n");
    printf("Modes & profiles:\n");
    printf("  --mode <name>       apply a shortcut bundle (see modes.ini)\n");
    printf("  mode list|show <n>|add <name> <steps>|delete <n>\n");
    printf("  profile list\n");
    printf("  profile apply|export|delete <name>\n\n");
    printf("Hardware (key = value pairs, same as mode steps):\n");
    printf("  --profile quiet|balanced|performance\n");
    printf("  --epp power|balance_power|balance_performance|performance\n");
    printf("  --freq <mhz>            CPU max frequency\n");
    printf("  --hz <rate|max>         display refresh (compositor backend)\n");
    printf("  --battery <20..100>     charge limit\n");
    printf("  --battery-oneshot       charge to full once\n");
    printf("  --ppt Q45|B60|P80|<spl>,<sppt>,<fppt>\n");
    printf("  --nv-boost <5..25 W>    NVIDIA dynamic boost\n");
    printf("  --nv-temp <75..87 C>    NVIDIA temp target\n");
    printf("  --panel-od on|off       panel overdrive\n");
    printf("  --cpu-boost on|off      cpufreq boost\n");
    printf("  --kbd off|low|med|high  keyboard backlight\n");
    printf("  --follow            run ctron actions from daeboard binds\n");
    printf("  --daeboard-start    start the daemon (sudo -n systemd-run)\n");
    printf("  --daeboard-stop     ask the daemon to quit\n");
    printf("  --daeboard-reload   recompile ~/.config/ctron/daeboard.binds\n");
    printf("  --aura <effect> [color|hex]\n");
    printf("  --fan stock|silent|cool|full|on|off\n");
    printf("  --fan-curve cpu|gpu <temps> <pwms>\n");
    printf("  --fan-write             write in-memory curve to the EC\n\n");
    printf("Config: %s [--config-dir DIR]\n", prog);
}

/* ---- status / doctor / watch --------------------------------------------- */

/* "--" for unknown values, else the number — into the caller's buffer
 * (no static ring: a printf with several of these cannot collide). */
static const char *dash_if(char *buf, size_t n, int v)
{
    if (v < 0)
        return "--";
    snprintf(buf, n, "%d", v);
    return buf;
}

static int cmd_status(hw_state_t *hw)
{
    char v1[16], v2[16], v3[16], v4[16], v5[16];
    printf("ctron — %s\n", hw->model);
    printf("  CPU            : %s\n", hw->cpu);
    printf("  CPU temp       : %s °C (k10temp)\n", dash_if(v1, sizeof(v1), hw->cpu_temp));
    printf("  GPU temp       : %s °C (nvidia-smi)\n", dash_if(v2, sizeof(v2), hw->gpu_temp));
    printf("  CPU clock      : %d MHz (limit %d MHz)\n", hw->cpu_mhz_cur, hw->cpu_mhz_limit);
    printf("  Fan RPM        : %s / %s (cpu/gpu)\n",
           dash_if(v3, sizeof(v3), hw->rpm_cpu), dash_if(v4, sizeof(v4), hw->rpm_gpu));
    printf("  Profile        : %s\n", hw_profile_name(hw->profile));
    printf("  EPP            : %s\n", hw_epp_name(hw->epp));
    printf("  Display        : %s Hz (%s)\n",
           hw->hz_cur > 0 ? dash_if(v5, sizeof(v5), hw->hz_cur) : "--",
           display_get() ? display_get()->name : "no backend");
    if (hw->hz_count > 0) {
        printf("  Modes          : ");
        for (int i = 0; i < hw->hz_count; i++)
            printf("%s%d", i ? ", " : "", hw->hz_modes[i]);
        printf(" Hz\n");
    }
    char pwr[24];
    hw_fmt_power(pwr, sizeof(pwr), hw->bat_mw_known, hw->bat_mw);
    printf("  Battery        : %d%% %s %s%s limit %d%%\n", hw->bat_pct,
           hw->bat_status, pwr, hw->ac_online ? " (AC)" : "", hw->bat_limit);
    printf("  PPT            : %s / %s / %s W (SPL/SPPT/FPPT; -- until written)\n",
           dash_if(v1, sizeof(v1), hw->ppt_spl),
           dash_if(v2, sizeof(v2), hw->ppt_sppt),
           dash_if(v3, sizeof(v3), hw->ppt_fppt));
    printf("  NV boost/temp  : %s W / %s °C\n",
           dash_if(v4, sizeof(v4), hw->nv_boost), dash_if(v5, sizeof(v5), hw->nv_temp));
    printf("  Panel OD       : %s   CPU boost: %s\n",
           hw->panel_od ? "on" : "off", hw->cpu_boost ? "on" : "off");
    printf("  Keyboard       : %s\n", hw_kbd_name(hw->kbd));
    {
        char ct[128], cp[128], gt[128], gp[128];
        fan_to_csv(&hw->fan_cpu, ct, sizeof(ct), cp, sizeof(cp));
        fan_to_csv(&hw->fan_gpu, gt, sizeof(gt), gp, sizeof(gp));
        printf("  Fan curve cpu  : %s %s (%s)\n", ct, cp, hw->fan_cpu_on ? "on" : "off");
        printf("  Fan curve gpu  : %s %s (%s)\n", gt, gp, hw->fan_gpu_on ? "on" : "off");
    }
    return 0;
}

static volatile sig_atomic_t s_watch_run = 1;
static void watch_stop(int sig) { (void)sig; s_watch_run = 0; }

static int cmd_watch(hw_state_t *hw)
{
    struct sigaction sa = {0};
    sa.sa_handler = watch_stop;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("ctron watch — Ctrl-C stops\n");
    while (s_watch_run) {
        char rc[16], rg[16], pwr[24];
        hw_refresh_fast(hw);
        hw_fmt_power(pwr, sizeof(pwr), hw->bat_mw_known, hw->bat_mw);
        printf("\r %3d°C  GPU %2d°C  %4d MHz  fan %4s/%4s rpm  BAT %3d%% %-11s %s  %s   ",
               hw->cpu_temp, hw->gpu_temp, hw->cpu_mhz_cur,
               dash_if(rc, sizeof(rc), hw->rpm_cpu), dash_if(rg, sizeof(rg), hw->rpm_gpu),
               hw->bat_pct, hw->bat_status, pwr, hw_profile_name(hw->profile));
        usleep((useconds_t)g_prefs.poll_ms * 1000);
    }
    printf("\n");
    return 0;
}

static int cmd_doctor(hw_state_t *hw)
{
    char os[128] = "?", dir[400];
    ut_read_file("/etc/os-release", os, sizeof(os)); /* first line; fine */
    {
        FILE *f = fopen("/etc/os-release", "r");
        if (f) {
            char line[256];
            os[0] = '\0';
            while (fgets(line, sizeof(line), f)) {
                if (!strncmp(line, "PRETTY_NAME=", 12)) {
                    char *v = line + 12;
                    v += (*v == '"');
                    v[strcspn(v, "\"\n")] = '\0';
                    snprintf(os, sizeof(os), "%s", v);
                    break;
                }
            }
            fclose(f);
        }
    }
    settings_dir(dir, sizeof(dir));
    const display_ops_t *d = display_get();

    printf("ctron doctor\n");
    printf("  os            : %s\n", os[0] ? os : "?");
    printf("  machine       : %s (%s)\n", hw->model, hw->is_asus ? "ASUS" : "not ASUS");
    printf("  cpu           : %s\n", hw->cpu);
    printf("  asusctl       : %s\n", hw->has_asusctl ? "yes" : "no");
    printf("  asus-armoury  : %s\n", hw->has_armoury ? "yes" : "no");
    printf("  fan curve     : %s\n", hw->has_fan_curve ? "hwmon asus_custom_fan_curve" : "missing");
    printf("  fan rpm       : %s\n", hw->has_fan_rpm ? "hwmon asus" : "missing");
    {
        char t1[16], t2[16];
        printf("  k10temp       : %s °C\n", dash_if(t1, sizeof(t1), hw->cpu_temp));
        printf("  nvidia-smi    : %s (%s °C)\n", hw->has_nvidia_smi ? "yes" : "no",
               dash_if(t2, sizeof(t2), hw->gpu_temp));
    }
    printf("  kbd led       : %s\n", hw->has_kbd_led ? "asus::kbd_backlight" : "missing");
    printf("  ppt sysfs     : %s\n",
           ut_path_exists("/sys/devices/platform/asus-nb-wmi/ppt_pl1_spl") ? "yes" : "no");
    printf("  display       : %s\n", d ? d->name : "no backend detected");
    printf("  sudo -n       : %s\n",
           system("sudo -n true >/dev/null 2>&1") == 0 ? "passwordless" : "unavailable");
    printf("  config        : %s\n", dir);
    return 0;
}

/* ---- mode / profile subcommands ------------------------------------------ */

static int cmd_mode(int argc, char **argv, hw_state_t *hw)
{
    (void)hw; /* modes are applied through --mode, not here */
    if (argc < 3) {
        fprintf(stderr, "usage: ctron mode list|show <name>|add <name> <steps>|delete <name>\n");
        return 2;
    }
    const char *sub = argv[2];

    mode_def_t modes[MODES_MAX];
    int n = modes_load(modes, MODES_MAX);

    if (!strcmp(sub, "list")) {
        for (int i = 0; i < n; i++)
            printf("%-12s %s\n", modes[i].name, modes[i].steps);
        return 0;
    }
    if (!strcmp(sub, "show") && argc > 3) {
        int i = mode_find(modes, n, argv[3]);
        if (i < 0) {
            fprintf(stderr, "no such mode: %s\n", argv[3]);
            return 1;
        }
        printf("[%s]\nsteps = %s\n", modes[i].name, modes[i].steps);
        return 0;
    }
    if (!strcmp(sub, "add") && argc > 4) {
        if (mode_find(modes, n, argv[3]) >= 0) {
            fprintf(stderr, "mode exists: %s (delete it first)\n", argv[3]);
            return 1;
        }
        char steps[MODE_STEPS_MAX] = {0};
        for (int i = 4; i < argc; i++) {
            strncat(steps, argv[i], sizeof(steps) - strlen(steps) - 2);
            if (i + 1 < argc)
                strncat(steps, " ", sizeof(steps) - strlen(steps) - 2);
        }
        snprintf(modes[n].name, MODE_NAME_MAX, "%s", argv[3]);
        snprintf(modes[n].steps, MODE_STEPS_MAX, "%s", steps);
        n++;
        modes_save(modes, n);
        printf("added mode '%s'\n", argv[3]);
        return 0;
    }
    if (!strcmp(sub, "delete") && argc > 3) {
        int i = mode_find(modes, n, argv[3]);
        if (i < 0) {
            fprintf(stderr, "no such mode: %s\n", argv[3]);
            return 1;
        }
        for (; i + 1 < n; i++)
            modes[i] = modes[i + 1];
        n--;
        modes_save(modes, n);
        printf("deleted mode '%s'\n", argv[3]);
        return 0;
    }
    fprintf(stderr, "unknown mode subcommand\n");
    return 2;
}

static int cmd_profile(int argc, char **argv, hw_state_t *hw)
{
    if (argc < 3) {
        fprintf(stderr, "usage: ctron profile list|apply|export|delete <name>\n");
        return 2;
    }
    const char *sub = argv[2];

    if (!strcmp(sub, "list")) {
        char list[MAX_PROFILES][PROFILE_NAME_MAX];
        int n = profile_list(list, MAX_PROFILES);
        for (int i = 0; i < n; i++)
            printf("%s\n", list[i]);
        return 0;
    }
    if (argc < 4) {
        fprintf(stderr, "profile %s needs a name\n", sub);
        return 2;
    }
    const char *name = argv[3];

    if (!strcmp(sub, "apply")) {
        char err[128];
        int rc = profile_import(name, hw, err, sizeof(err));
        if (rc == 0)
            ctrl_fan_write(hw);
        else
            fprintf(stderr, "%s\n", err[0] ? err : "apply failed");
        return rc == 0 ? 0 : 1;
    }
    if (!strcmp(sub, "export"))
        return profile_export(name, hw) == 0 ? 0 : 1;
    if (!strcmp(sub, "delete"))
        return profile_delete(name) == 0 ? 0 : 1;

    fprintf(stderr, "unknown profile subcommand: %s\n", sub);
    return 2;
}

/* ---- flag → cmd_run bridge ------------------------------------------------ */

static int run_flag(hw_state_t *hw, const char *key, const char *val)
{
    char err[192];
    int rc = cmd_run(hw, key, val, err, sizeof(err));
    if (rc != 0)
        fprintf(stderr, "ctron: %s\n", err[0] ? err : "command failed");
    return rc;
}

static int follow_fire(const char *key, void *ud)
{
    hw_state_t *hw = ud;
    char path[512];
    char text[8192];
    char cmd[64];
    char val[128];
    FILE *f;
    size_t n;

    db_binds_path(path, (int)sizeof path);
    f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "ctron: no %s\n", path);
        return 0;
    }
    n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    if (db_action_in(text, key, cmd, (int)sizeof cmd, val, (int)sizeof val) != 0)
        return 0;
    fprintf(stderr, "ctron: fire %s -> %s %s\n", key, cmd, val);
    run_flag(hw, cmd, val);
    return 0;
}

/* ---- main ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    /* --config-dir DIR (removes itself from argv) */
    int w = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--config-dir") && i + 1 < argc) {
            setenv("CTRON_CONFIG", argv[++i], 1);
            continue;
        }
        if (!strncmp(argv[i], "--config-dir=", 13)) {
            setenv("CTRON_CONFIG", argv[i] + 13, 1);
            continue;
        }
        argv[w++] = argv[i];
    }
    argv[w] = NULL;
    argc = w;

    if (argc > 1 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        print_usage(argv[0]);
        return 0;
    }
    if (argc > 1 && (!strcmp(argv[1], "--version") || !strcmp(argv[1], "-V"))) {
        print_version();
        return 0;
    }

    if (argc == 1) {
        /* no args: fullscreen TUI when interactive, help otherwise */
        if (isatty(0) && isatty(1)) {
            hw_state_t hw;
            hw_init(&hw);
            settings_load(&hw);
            int rc = ui_run(&hw);
            return rc == 0 ? 0 : 1;
        }
        print_version();
        printf("No terminal attached — printing help instead of the TUI.\n\n");
        print_usage(argv[0]);
        return 2;
    }

    hw_state_t hw;
    hw_init(&hw);
    settings_load(&hw);

    /* commands that never touch hardware state */
    if (!strcmp(argv[1], "--tui") || !strcmp(argv[1], "-t")) {
        if (!isatty(0) || !isatty(1)) {
            fprintf(stderr, "ctron: --tui needs a terminal\n");
            return 1;
        }
        int rc = ui_run(&hw);
        return rc == 0 ? 0 : 1;
    }
    if (!strcmp(argv[1], "--doctor")) {
        hw_refresh_live(&hw);
        return cmd_doctor(&hw);
    }
    if (!strcmp(argv[1], "--setup")) {
        char dir[400];
        settings_dir(dir, sizeof(dir));
        printf("ctron config lives in %s\n", dir);
        printf("edit %s/modes.ini to manage shortcut bundles;\n", dir);
        printf("profiles are stored in %s/profiles/*.ctr\n", dir);
        return 0;
    }
    if (!strcmp(argv[1], "mode"))
        return cmd_mode(argc, argv, &hw);
    if (!strcmp(argv[1], "profile"))
        return cmd_profile(argc, argv, &hw);

    /* status / watch need a live snapshot */
    if (!strcmp(argv[1], "--follow")) {
        return db_follow(follow_fire, &hw);
    }
    if (!strcmp(argv[1], "--daeboard-start")) {
        return db_start() == 0 ? 0 : 1;
    }
    if (!strcmp(argv[1], "--daeboard-stop")) {
        return db_quit() == 0 ? 0 : 1;
    }
    if (!strcmp(argv[1], "--daeboard-reload")) {
        char err[64];
        if (db_reload(err, (int)sizeof err) == 0)
            return 0;
        fprintf(stderr, "ctron: daeboard %s\n", err[0] ? err : "reload failed");
        return 1;
    }
    if (!strcmp(argv[1], "--status") || !strcmp(argv[1], "-s")) {
        hw_refresh_live(&hw);
        return cmd_status(&hw);
    }
    if (!strcmp(argv[1], "--watch") || !strcmp(argv[1], "-w")) {
        hw_refresh_fast(&hw);
        return cmd_watch(&hw);
    }

    /* everything else: hardware flags applied in order */
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        const char *flag = argv[i];
        if (flag[0] != '-' || flag[1] != '-') {
            fprintf(stderr, "ctron: unexpected argument '%s'\n", flag);
            return 2;
        }
        /* --flag=value */
        char key[64] = {0};
        const char *val = NULL;
        const char *eq = strchr(flag + 2, '=');
        if (eq) {
            size_t kn = (size_t)(eq - (flag + 2));
            if (kn >= sizeof(key))
                kn = sizeof(key) - 1;
            memcpy(key, flag + 2, kn);
            val = eq + 1;
        } else {
            snprintf(key, sizeof(key), "%s", flag + 2);
        }

        /* valueless flags */
        if (!strcmp(key, "fan-write") || !strcmp(key, "battery-oneshot")) {
            if (run_flag(&hw, key, "") != 0)
                rc = 1;
            continue;
        }

        /* multi-token flags: --fan-curve cpu T P (3 tokens after the flag),
         * --aura effect [color|hex] (1-2 tokens) */
        if (!val && (!strcmp(key, "fan-curve") || !strcmp(key, "aura"))) {
            int want = !strcmp(key, "fan-curve") ? 3 : 1;
            char joined[256] = {0};
            int got = 0;
            while (got < want && i + 1 < argc && argv[i + 1][0] != '-') {
                if (got)
                    strncat(joined, " ", sizeof(joined) - strlen(joined) - 1);
                strncat(joined, argv[++i], sizeof(joined) - strlen(joined) - 1);
                got++;
            }
            /* --aura with a color/hex right after "effect" already grabbed
             * one token; try to also absorb a color-looking second token */
            if (want == 1 && i + 1 < argc && argv[i + 1][0] != '-') {
                strncat(joined, " ", sizeof(joined) - strlen(joined) - 1);
                strncat(joined, argv[++i], sizeof(joined) - strlen(joined) - 1);
            }
            if (got == 0) {
                fprintf(stderr, "ctron: %s needs a value\n", flag);
                return 2;
            }
            if (run_flag(&hw, key, joined) != 0)
                rc = 1;
            continue;
        }

        if (!val) {
            if (i + 1 >= argc) {
                fprintf(stderr, "ctron: %s needs a value\n", flag);
                return 2;
            }
            val = argv[++i];
        }
        if (run_flag(&hw, key, val) != 0)
            rc = 1;
    }

    settings_save(&hw);
    return rc;
}
