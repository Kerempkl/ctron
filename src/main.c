#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include "hardware.h"
#include "settings.h"
#include "profile.h"
#include "ui.h"

static int parse_onoff(const char *s, int *out)
{
    if (!s)
        return -1;
    if (!strcasecmp(s, "on") || !strcasecmp(s, "1") || !strcasecmp(s, "true")) {
        *out = 1;
        return 0;
    }
    if (!strcasecmp(s, "off") || !strcasecmp(s, "0") || !strcasecmp(s, "false")) {
        *out = 0;
        return 0;
    }
    return -1;
}

static void read_kv_file(const char *path, const char *key, char *out, size_t n)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[256];
    size_t klen = strlen(key);
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, key, klen) != 0)
            continue;
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        eq++;
        while (*eq == '"' || isspace((unsigned char)*eq))
            eq++;
        size_t i = 0;
        while (eq[i] && eq[i] != '"' && eq[i] != '\n' && i + 1 < n)
            i++;
        memcpy(out, eq, i);
        out[i] = '\0';
        break;
    }
    fclose(f);
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [options]\n\n", prog);
    printf("Ctron — ASUS TUF (AMD Ryzen + NVIDIA) control program.\n");
    printf("Headless by default. Optional TUI: %s --tui\n\n", prog);
    printf("Options:\n");
    printf("  --setup                Interactive setup (config dir)\n");
    printf("  --config-dir DIR       Override config directory\n");
    printf("  --status, -s           Live hardware snapshot (sysfs/asusctl)\n");
    printf("  --watch, -w            Live temp/freq/battery line (250 ms)\n");
    printf("  --doctor               Capability dump\n");
    printf("  --tui, -t              TUI in this terminal\n");
    printf("  --profile <name>       Quiet | Balanced | Performance\n");
    printf("  --epp <mode>           power | balance_power | balance_performance | performance\n");
    printf("  --freq <mhz>           CPU scaling_max_freq cap\n");
    printf("  --tctl <C>             Software temp cap (70-105, 0=off)\n");
    printf("  --hz <rate>            60 | 144 | max\n");
    printf("  --battery <limit>      Charge limit 20-100\n");
    printf("  --battery-oneshot      asusctl battery oneshot\n");
    printf("  --ppt Q45|B60|P80      PPT preset\n");
    printf("  --ppt <spl>,<sppt>,<fppt>\n");
    printf("  --nv-boost <W>         NVIDIA dynamic boost watts\n");
    printf("  --nv-temp <C>          NVIDIA temp target\n");
    printf("  --panel-od on|off\n");
    printf("  --cpu-boost on|off\n");
    printf("  --fan <preset>         stock | silent | cool | full | on | off\n");
    printf("  --fan-curve cpu|gpu <temps> <pwms>\n");
    printf("  --fan-write            Write in-memory curve to EC/asusctl\n");
    printf("  --kbd off|low|med|high\n");
    printf("  --aura <effect> [color|hex]\n");
    printf("  --armoury-get <attr>\n");
    printf("  --armoury-set <attr> <val>\n");
    printf("  profile list|export|import|delete [TAG]\n");
    printf("  --help, -h\n");
}

static void print_welcome(void)
{
    char os[128] = "unknown", dmi[128] = "unknown", dir[512];
    read_kv_file("/etc/os-release", "PRETTY_NAME", os, sizeof(os));
    if (!os[0])
        read_kv_file("/etc/os-release", "NAME", os, sizeof(os));
    read_kv_file("/sys/class/dmi/id/product_name", "", dmi, sizeof(dmi));
    {
        FILE *f = fopen("/sys/class/dmi/id/product_name", "r");
        if (f) {
            if (fgets(dmi, sizeof(dmi), f)) {
                size_t n = strlen(dmi);
                while (n && (dmi[n - 1] == '\n' || dmi[n - 1] == '\r'))
                    dmi[--n] = '\0';
            }
            fclose(f);
        }
    }
    settings_dir(dir, sizeof(dir));
    printf("ctron — ASUS TUF control (AMD Ryzen + NVIDIA)\n");
    printf("  distro  : %s\n", os[0] ? os : "unknown");
    printf("  machine : %s\n", dmi[0] ? dmi : "unknown");
    printf("  config  : %s\n", dir);
    printf("  profiles: %s/profiles/*.ctr\n", dir);
    printf("\n");
    printf("  ctron --setup     choose config dir\n");
    printf("  ctron --help      flags\n");
    printf("  ctron --status    hardware\n");
    printf("  ctron --tui       interface in this terminal\n");
}

static int cmd_setup(void)
{
    char def[512], line[512], dir[512];
    settings_dir(def, sizeof(def));
    print_welcome();
    printf("\n");
    if (isatty(0)) {
        printf("Config directory [%s]: ", def);
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin))
            line[0] = '\0';
        char *p = line;
        while (isspace((unsigned char)*p))
            p++;
        size_t n = strlen(p);
        while (n && isspace((unsigned char)p[n - 1]))
            p[--n] = '\0';
        if (n)
            setenv("CTRON_CONFIG", p, 1);
    }
    settings_dir(dir, sizeof(dir));
    if (settings_write_stub() != 0) {
        fprintf(stderr, "Could not write %s/config.ini\n", dir);
        return 1;
    }
    printf("Wrote %s/config.ini\n", dir);
    if (system("sudo -n true >/dev/null 2>&1") != 0)
        printf("Note: sysfs writes use sudo; this session is not passwordless.\n");
    return 0;
}

static void print_ppt(const hardware_state_t *hw)
{
    if (hw->ppt_spl > 5)
        printf("%d/%d/%d W", hw->ppt_spl, hw->ppt_sppt, hw->ppt_fppt);
    else
        printf("-- (sysfs 0 or 5 is not a live wattage)");
}

static void print_fan_line(const char *who, const fan_curve_t *fc, int on)
{
    char t[128], p[128];
    hw_fan_to_csv(fc, t, sizeof(t), p, sizeof(p));
    printf("  Fan %-4s        : %s  n=%d  T=%s  pwm=%s\n",
           who, on ? "on" : "off", fc->n, t[0] ? t : "-", p[0] ? p : "-");
}

static int cmd_status(const hardware_state_t *hw)
{
    printf("Hardware Status for %s:\n", hw->laptop_model);
    printf("  CPU Model       : %s\n", hw->cpu_model);
    printf("  CPU Temp        : %d°C (k10temp Tctl)\n", hw->cpu_temp_c);
    printf("  CPU Frequency   : %d MHz (scaling_max live %d, saved %d)\n",
           hw->cpu_cur_freq_mhz, hw->cpu_applied_max_mhz, hw->cpu_target_max_mhz);
    printf("  Temp cap        : %d°C (%s, software freq — not SMU Tctl)\n",
           hw->cpu_temp_cap_c, hw->cpu_temp_cap_on ? "on" : "off");
    printf("  Active Profile  : %s\n", hw_profile_name(hw->active_profile));
    printf("  Active EPP      : %s\n", hw_epp_name(hw->active_epp));
    printf("  Display         : %s @ %d Hz (Max: %d Hz)\n",
           hw->display_name, hw->display_cur_hz, hw->display_max_hz);
    printf("  Battery         : %d%% (%s) [Cap: %d%%] AC=%s\n",
           hw->battery_percent, hw->battery_status, hw->battery_charge_limit,
           hw->battery_ac_connected ? "yes" : "no");
    printf("  Fan curve       : %s\n",
           hw->has_fan_curve ? "asus_custom_fan_curve (live hwmon)" : "none");
    print_fan_line("CPU", &hw->fan_cpu, hw->fan_cpu_on);
    print_fan_line("GPU", &hw->fan_gpu, hw->fan_gpu_on);
    printf("  PPT SPL/SPPT/FPPT: ");
    print_ppt(hw);
    printf("\n");
    printf("  NV boost/temp   : %d W / %d°C  panel_od=%s  cpu_boost=%s\n",
           hw->nv_boost_w, hw->nv_temp_target,
           hw->panel_od ? "on" : "off", hw->cpu_boost ? "on" : "off");
    printf("  Keyboard        : %s\n", hw_kbd_name(hw->kbd_brightness));
    printf("  Aura            : %s (%s) (last set, not live EC)\n",
           AURA_EFFECT_NAMES[hw->aura_effect_idx],
           AURA_COLOR_NAMES[hw->aura_color_idx]);
    printf("  Armoury         : %s\n",
           hw->has_asus_armoury ? "asus-armoury" : "asus_wmi (no armoury sysfs)");
    return 0;
}

static int cmd_watch(hardware_state_t *hw)
{
    hw_refresh_live(hw);
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("ctron watch  (Ctrl-C to stop)\n");
    for (;;) {
        hw_poll_telemetry(hw);
        printf("\r  %3d°C  %4d MHz  (max %4d)  BAT %3d%% %-12s  %s %s    ",
               hw->cpu_temp_c, hw->cpu_cur_freq_mhz, hw->cpu_applied_max_mhz,
               hw->battery_percent, hw->battery_status,
               hw_profile_name(hw->active_profile),
               hw->battery_ac_connected ? "AC" : "DC");
        usleep(250000);
    }
    return 0;
}

static int cmd_doctor(const hardware_state_t *hw)
{
    char os[128] = "", dir[512];
    read_kv_file("/etc/os-release", "PRETTY_NAME", os, sizeof(os));
    settings_dir(dir, sizeof(dir));
    printf("ctron doctor\n");
    printf("  os            : %s\n", os[0] ? os : "unknown");
    printf("  dmi           : %s\n", hw->laptop_model);
    printf("  cpu           : %s\n", hw->cpu_model);
    printf("  config        : %s%s\n", dir, settings_present() ? "" : " (no config.ini yet)");
    printf("  asusctl       : %s\n", hw->has_asusctl ? "yes" : "no");
    printf("  asus-armoury  : %s\n", hw->has_asus_armoury ? "yes" : "no");
    printf("  fan hwmon     : %s\n", hw->has_fan_curve ? "asus_custom_fan_curve" : "missing");
    printf("  ppt sysfs     : %s\n",
           access("/sys/devices/platform/asus-nb-wmi/ppt_pl1_spl", F_OK) == 0 ? "yes" : "no");
    printf("  k10temp       : %d°C\n", hw->cpu_temp_c);
    printf("  ryzenadj      : %s\n", hw->has_ryzenadj ? "on PATH (not used as Tctl yet)" : "no");
    printf("  tui           : linked (--tui)\n");
    printf("  ppt live      : ");
    print_ppt(hw);
    printf("\n");
    return 0;
}

static int apply_ppt_token(hardware_state_t *hw, const char *tok)
{
    int spl, sppt, fppt;
    if (!strcasecmp(tok, "Q45") || !strcasecmp(tok, "q45")) {
        if (hw->battery_ac_connected)
            return hw_set_ppt(hw, 45, 55, 55);
        return hw_set_ppt(hw, 35, 45, 45);
    }
    if (!strcasecmp(tok, "B60") || !strcasecmp(tok, "b60")) {
        if (hw->battery_ac_connected)
            return hw_set_ppt(hw, 60, 75, 75);
        return hw_set_ppt(hw, 45, 54, 54);
    }
    if (!strcasecmp(tok, "P80") || !strcasecmp(tok, "p80")) {
        if (hw->battery_ac_connected)
            return hw_set_ppt(hw, 80, 80, 80);
        return hw_set_ppt(hw, 65, 65, 65);
    }
    if (sscanf(tok, "%d,%d,%d", &spl, &sppt, &fppt) == 3)
        return hw_set_ppt(hw, spl, sppt, fppt);
    fprintf(stderr, "Unknown PPT: %s (Q45|B60|P80 or spl,sppt,fppt)\n", tok);
    return -1;
}

static int aura_effect_idx(const char *name)
{
    for (int i = 0; i < AURA_EFFECT_COUNT; i++)
        if (!strcasecmp(name, AURA_EFFECT_NAMES[i]))
            return i;
    return -1;
}

static int aura_color_idx(const char *name)
{
    for (int i = 0; i < AURA_COLOR_COUNT; i++)
        if (!strcasecmp(name, AURA_COLOR_NAMES[i]))
            return i;
    return -1;
}

static int cmd_profile(hardware_state_t *hw, int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "profile list|export|import|delete [TAG]\n");
        return 1;
    }
    const char *sub = argv[2];
    acv_profile_filter_t all = {1, 1, 1, 1};
    if (!strcmp(sub, "list")) {
        char list[MAX_PROFILES][4];
        int n = profile_list(list, MAX_PROFILES);
        for (int i = 0; i < n; i++)
            printf("%s\n", list[i]);
        return 0;
    }
    if (argc < 4) {
        fprintf(stderr, "profile %s needs TAG\n", sub);
        return 1;
    }
    const char *tag = argv[3];
    if (!strcmp(sub, "export"))
        return profile_export(tag, hw, &all) == 0 ? 0 : 1;
    if (!strcmp(sub, "import"))
        return profile_import(tag, hw, &all) == 0 ? 0 : 1;
    if (!strcmp(sub, "delete"))
        return profile_delete(tag) == 0 ? 0 : 1;
    fprintf(stderr, "Unknown profile command: %s\n", sub);
    return 1;
}

static void eat_config_dir(int *argc, char **argv)
{
    int w = 1;
    for (int i = 1; i < *argc; i++) {
        if (!strcmp(argv[i], "--config-dir") && i + 1 < *argc) {
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
    *argc = w;
}

int main(int argc, char *argv[])
{
    eat_config_dir(&argc, argv);

    if (argc > 1 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        print_usage(argv[0]);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--setup"))
        return cmd_setup();

    if (argc == 1) {
        print_welcome();
        if (!settings_present() && isatty(0))
            printf("\nNo config yet. Defaults above. Run ctron --setup to write them.\n");
        return 2;
    }

    hardware_state_t hw;
    hw_init(&hw);
    settings_load(&hw);

    if (!strcmp(argv[1], "--tui") || !strcmp(argv[1], "-t"))
        return ui_run(&hw);
    if (!strcmp(argv[1], "--watch") || !strcmp(argv[1], "-w") ||
        (( !strcmp(argv[1], "--status") || !strcmp(argv[1], "-s")) &&
         argc > 2 && (!strcmp(argv[2], "--watch") || !strcmp(argv[2], "-w")))) {
        return cmd_watch(&hw);
    }
    if (!strcmp(argv[1], "--status") || !strcmp(argv[1], "-s")) {
        hw_refresh_live(&hw);
        return cmd_status(&hw);
    }
    if (!strcmp(argv[1], "--doctor")) {
        hw_refresh_live(&hw);
        return cmd_doctor(&hw);
    }
    if (!strcmp(argv[1], "profile"))
        return cmd_profile(&hw, argc, argv);

    int rc = 0;
    if (!strcmp(argv[1], "--profile") && argc > 2) {
        if (!strcasecmp(argv[2], "quiet"))
            rc = hw_set_profile(&hw, PROF_QUIET);
        else if (!strcasecmp(argv[2], "balanced"))
            rc = hw_set_profile(&hw, PROF_BALANCED);
        else if (!strcasecmp(argv[2], "performance") || !strcasecmp(argv[2], "perf"))
            rc = hw_set_profile(&hw, PROF_PERFORMANCE);
        else {
            fprintf(stderr, "Unknown profile: %s\n", argv[2]);
            return 1;
        }
    } else if (!strcmp(argv[1], "--hz") && argc > 2) {
        int hz = !strcasecmp(argv[2], "max") ? hw.display_max_hz : atoi(argv[2]);
        rc = hw_set_display_hz(&hw, hz);
    } else if (!strcmp(argv[1], "--battery") && argc > 2) {
        rc = hw_set_battery_limit(&hw, atoi(argv[2]));
    } else if (!strcmp(argv[1], "--battery-oneshot")) {
        rc = hw_battery_oneshot(&hw);
    } else if (!strcmp(argv[1], "--epp") && argc > 2) {
        if (!strcasecmp(argv[2], "power"))
            rc = hw_set_epp(&hw, EPP_POWER);
        else if (!strcasecmp(argv[2], "balance_power"))
            rc = hw_set_epp(&hw, EPP_BALANCED_POWER);
        else if (!strcasecmp(argv[2], "balance_performance"))
            rc = hw_set_epp(&hw, EPP_BALANCED_PERF);
        else if (!strcasecmp(argv[2], "performance"))
            rc = hw_set_epp(&hw, EPP_PERFORMANCE);
        else {
            fprintf(stderr, "Unknown EPP: %s\n", argv[2]);
            return 1;
        }
    } else if (!strcmp(argv[1], "--freq") && argc > 2) {
        rc = hw_set_cpu_max_freq(&hw, atoi(argv[2]));
    } else if (!strcmp(argv[1], "--fan") && argc > 2) {
        if (!strcasecmp(argv[2], "stock"))
            rc = hw_fan_preset(&hw, 0);
        else if (!strcasecmp(argv[2], "silent"))
            rc = hw_fan_preset(&hw, 1);
        else if (!strcasecmp(argv[2], "cool"))
            rc = hw_fan_preset(&hw, 2);
        else if (!strcasecmp(argv[2], "full"))
            rc = hw_fan_preset(&hw, 3);
        else if (!strcasecmp(argv[2], "on"))
            rc = hw_fan_enable(&hw, true, true);
        else if (!strcasecmp(argv[2], "off"))
            rc = hw_fan_enable(&hw, false, false);
        else {
            fprintf(stderr, "Unknown fan preset: %s\n", argv[2]);
            return 1;
        }
    } else if (!strcmp(argv[1], "--fan-curve") && argc > 4) {
        int gpu = !strcasecmp(argv[2], "gpu");
        if (!gpu && strcasecmp(argv[2], "cpu")) {
            fprintf(stderr, "--fan-curve cpu|gpu <temps> <pwms>\n");
            return 1;
        }
        rc = hw_fan_from_csv(gpu ? &hw.fan_gpu : &hw.fan_cpu, argv[3], argv[4]);
        if (rc < 0) {
            fprintf(stderr, "bad curve\n");
            return 1;
        }
        rc = 0;
    } else if (!strcmp(argv[1], "--fan-write")) {
        rc = hw_fan_apply(&hw);
    } else if (!strcmp(argv[1], "--tctl") && argc > 2) {
        int t = atoi(argv[2]);
        if (t <= 0)
            rc = hw_set_temp_cap_enabled(&hw, false);
        else {
            hw_set_temp_cap(&hw, t);
            rc = hw_set_temp_cap_enabled(&hw, true);
        }
    } else if (!strcmp(argv[1], "--ppt") && argc > 2) {
        rc = apply_ppt_token(&hw, argv[2]);
        if (rc < 0)
            return 1;
    } else if (!strcmp(argv[1], "--nv-boost") && argc > 2) {
        rc = hw_set_nv_boost(&hw, atoi(argv[2]));
    } else if (!strcmp(argv[1], "--nv-temp") && argc > 2) {
        rc = hw_set_nv_temp(&hw, atoi(argv[2]));
    } else if (!strcmp(argv[1], "--panel-od") && argc > 2) {
        int on;
        if (parse_onoff(argv[2], &on) != 0)
            return 1;
        rc = hw_set_panel_od(&hw, on);
    } else if (!strcmp(argv[1], "--cpu-boost") && argc > 2) {
        int on;
        if (parse_onoff(argv[2], &on) != 0)
            return 1;
        rc = hw_set_cpu_boost(&hw, on);
    } else if (!strcmp(argv[1], "--kbd") && argc > 2) {
        if (!strcasecmp(argv[2], "off"))
            rc = hw_set_kbd_brightness(&hw, KBD_OFF);
        else if (!strcasecmp(argv[2], "low"))
            rc = hw_set_kbd_brightness(&hw, KBD_LOW);
        else if (!strcasecmp(argv[2], "med"))
            rc = hw_set_kbd_brightness(&hw, KBD_MED);
        else if (!strcasecmp(argv[2], "high"))
            rc = hw_set_kbd_brightness(&hw, KBD_HIGH);
        else {
            fprintf(stderr, "Unknown kbd: %s\n", argv[2]);
            return 1;
        }
    } else if (!strcmp(argv[1], "--aura") && argc > 2) {
        int ei = aura_effect_idx(argv[2]);
        if (ei < 0) {
            fprintf(stderr, "Unknown aura effect: %s\n", argv[2]);
            return 1;
        }
        int ci = hw.aura_color_idx;
        if (argc > 3) {
            if (strchr(argv[3], '#') || strspn(argv[3], "0123456789abcdefABCDEF") == strlen(argv[3])) {
                hw_set_aura_hex(&hw, argv[3]);
                ci = hw.aura_color_idx;
            } else {
                int c = aura_color_idx(argv[3]);
                if (c >= 0)
                    ci = c;
            }
        }
        rc = hw_set_aura(&hw, ei, ci);
    } else if (!strcmp(argv[1], "--armoury-get") && argc > 2) {
        char val[64] = {0};
        if (hw_armoury_get(argv[2], val, sizeof(val)) != 0) {
            fprintf(stderr, "Failed to get '%s'\n", argv[2]);
            return 1;
        }
        printf("%s = %s\n", argv[2], val);
        return 0;
    } else if (!strcmp(argv[1], "--armoury-set") && argc > 3) {
        rc = hw_armoury_set(argv[2], argv[3]);
    } else {
        fprintf(stderr, "Unknown option: %s\n", argv[1]);
        print_usage(argv[0]);
        return 1;
    }

    settings_save(&hw);
    return rc == 0 ? 0 : 1;
}
