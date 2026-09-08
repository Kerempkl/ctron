#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "hardware.h"
#include "settings.h"
#include "ui.h"

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n\n", prog);
    printf("Ctron — ASUS TUF Gaming Laptop Control Applet (Arcioth & Kerempkl)\n");
    printf("Built in pure C with Notcurses for 512x512 floating applet display.\n\n");
    printf("Options:\n");
    printf("  --status               Show current hardware status and exit\n");
    printf("  --tui, -t              Run interactive TUI inside current terminal\n");
    printf("  --profile <name>       Set profile (Quiet, Balanced, Performance)\n");
    printf("  --hz <rate>            Set screen refresh rate (60, 144, max)\n");
    printf("  --battery <limit>      Set battery charge limit (20-100)\n");
    printf("  --fan <preset>         Fan curve: stock, silent, cool, full, on, off\n");
    printf("  --epp <mode>           Set EPP (power, balance_power, balance_performance, performance)\n");
    printf("  --tctl <C>             Set Tctl temperature cap (70-105, 0=off)\n");
    printf("  --armoury-get <attr>   Get asus-armoury attribute (e.g. panel_od, boot_sound)\n");
    printf("  --armoury-set <a <v>>  Set asus-armoury attribute\n");
    printf("  --help, -h             Show this help message\n");
    printf("\nRun without arguments to launch the 512x512 floating applet window.\n");
}

int main(int argc, char *argv[]) {
    // If run with no arguments and not already inside the floating applet:
    if (argc == 1 && !getenv("CTRON_APPLET")) {
        char self_path[512] = {0};
        ssize_t len = readlink("/proc/self/exe", self_path, sizeof(self_path) - 1);
        if (len > 0) self_path[len] = '\0';
        else snprintf(self_path, sizeof(self_path), "%s", argv[0]);

        // Launch in floating 512x512 Kitty window
        char cmd[1024];
        snprintf(cmd, sizeof(cmd),
            "kitty --class ctron_widget --title \"Ctron\" "
            "-o hide_window_decorations=yes -o remember_window_size=no "
            "-o initial_window_width=512 -o initial_window_height=512 "
            "-o window_padding_width=4 -e env CTRON_APPLET=1 \"%s\" >/dev/null 2>&1 &",
            self_path);
        if (system(cmd) == 0) {
            return 0;
        }
    }

    hardware_state_t hw;
    hw_init(&hw);
    settings_load(&hw);
    hw_poll_telemetry(&hw);

    if (argc > 1) {
        if (strcmp(argv[1], "--tui") == 0 || strcmp(argv[1], "-t") == 0) {
            return ui_run(&hw);
        } else if (strcmp(argv[1], "--status") == 0 || strcmp(argv[1], "-s") == 0) {
            printf("Hardware Status for %s:\n", hw.laptop_model);
            printf("  CPU Model       : %s\n", hw.cpu_model);
            printf("  CPU Temp        : %d°C\n", hw.cpu_temp_c);
            printf("  CPU Frequency   : %d MHz (Max Cap: %d MHz)\n", hw.cpu_cur_freq_mhz, hw.cpu_target_max_mhz);
            printf("  Tctl Cap        : %d°C (%s%s)\n", hw.cpu_temp_cap_c,
                   hw.cpu_temp_cap_on ? "on" : "off",
                   hw.has_ryzenadj ? ", ryzenadj" : ", freq throttle");
            printf("  Active Profile  : %s\n", hw_profile_name(hw.active_profile));
            printf("  Active EPP      : %s\n", hw_epp_name(hw.active_epp));
            printf("  Display         : %s @ %d Hz (Max: %d Hz)\n", hw.display_name, hw.display_cur_hz, hw.display_max_hz);
            printf("  Battery         : %d%% (%s) [Cap: %d%%]\n", hw.battery_percent, hw.battery_status, hw.battery_charge_limit);
            printf("  Fan curve       : %s CPU=%s GPU=%s\n",
                   hw.has_fan_curve ? "asus_custom_fan_curve" : "none",
                   hw.fan_cpu_on ? "on" : "off", hw.fan_gpu_on ? "on" : "off");
            printf("  PPT SPL/SPPT/FPPT: %d/%d/%d W (5=stale fw cache)\n",
                   hw.ppt_spl, hw.ppt_sppt, hw.ppt_fppt);
            printf("  NV boost/temp   : %d W / %d°C  panel_od=%s  cpu_boost=%s\n",
                   hw.nv_boost_w, hw.nv_temp_target,
                   hw.panel_od ? "on" : "off", hw.cpu_boost ? "on" : "off");
            printf("  Keyboard Backlight: %s\n", hw_kbd_name(hw.kbd_brightness));
            printf("  Aura Effect     : %s (%s)\n", AURA_EFFECT_NAMES[hw.aura_effect_idx], AURA_COLOR_NAMES[hw.aura_color_idx]);
            printf("  Armoury Driver  : %s\n", hw.has_asus_armoury ? "asus-armoury active" : "asus_wmi (kernel 6.18, armoury ready)");
            return 0;
        } else if (strcmp(argv[1], "--armoury-get") == 0 && argc > 2) {
            char val[64] = {0};
            if (hw_armoury_get(argv[2], val, sizeof(val)) == 0) {
                printf("%s = %s\n", argv[2], val);
                return 0;
            } else {
                fprintf(stderr, "Failed to get armoury attribute '%s' (driver not active on kernel 6.18)\n", argv[2]);
                return 1;
            }
        } else if (strcmp(argv[1], "--armoury-set") == 0 && argc > 3) {
            if (hw_armoury_set(argv[2], argv[3]) == 0) {
                printf("Set %s = %s\n", argv[2], argv[3]);
                return 0;
            } else {
                fprintf(stderr, "Failed to set armoury attribute '%s'\n", argv[2]);
                return 1;
            }
        } else if (strcmp(argv[1], "--profile") == 0 && argc > 2) {
            if (strcasecmp(argv[2], "quiet") == 0) hw_set_profile(&hw, PROF_QUIET);
            else if (strcasecmp(argv[2], "balanced") == 0) hw_set_profile(&hw, PROF_BALANCED);
            else if (strcasecmp(argv[2], "performance") == 0 || strcasecmp(argv[2], "perf") == 0) hw_set_profile(&hw, PROF_PERFORMANCE);
            else { fprintf(stderr, "Unknown profile: %s\n", argv[2]); return 1; }
            settings_save(&hw);
            return 0;
        } else if (strcmp(argv[1], "--hz") == 0 && argc > 2) {
            int hz = (strcasecmp(argv[2], "max") == 0) ? hw.display_max_hz : atoi(argv[2]);
            hw_set_display_hz(&hw, hz);
            settings_save(&hw);
            return 0;
        } else if (strcmp(argv[1], "--battery") == 0 && argc > 2) {
            int lim = atoi(argv[2]);
            hw_set_battery_limit(&hw, lim);
            settings_save(&hw);
            return 0;
        } else if (strcmp(argv[1], "--epp") == 0 && argc > 2) {
            if (strcasecmp(argv[2], "power") == 0) hw_set_epp(&hw, EPP_POWER);
            else if (strcasecmp(argv[2], "balance_power") == 0) hw_set_epp(&hw, EPP_BALANCED_POWER);
            else if (strcasecmp(argv[2], "balance_performance") == 0) hw_set_epp(&hw, EPP_BALANCED_PERF);
            else if (strcasecmp(argv[2], "performance") == 0) hw_set_epp(&hw, EPP_PERFORMANCE);
            else { fprintf(stderr, "Unknown EPP: %s\n", argv[2]); return 1; }
            settings_save(&hw);
            return 0;
        } else if (strcmp(argv[1], "--fan") == 0 && argc > 2) {
            if (strcasecmp(argv[2], "stock") == 0) hw_fan_preset(&hw, 0);
            else if (strcasecmp(argv[2], "silent") == 0) hw_fan_preset(&hw, 1);
            else if (strcasecmp(argv[2], "cool") == 0) hw_fan_preset(&hw, 2);
            else if (strcasecmp(argv[2], "full") == 0) hw_fan_preset(&hw, 3);
            else if (strcasecmp(argv[2], "on") == 0) hw_fan_enable(&hw, true, true);
            else if (strcasecmp(argv[2], "off") == 0) hw_fan_enable(&hw, false, false);
            else { fprintf(stderr, "Unknown fan preset: %s\n", argv[2]); return 1; }
            settings_save(&hw);
            return 0;
        } else if (strcmp(argv[1], "--tctl") == 0 && argc > 2) {
            int t = atoi(argv[2]);
            if (t <= 0) {
                hw_set_temp_cap_enabled(&hw, false);
            } else {
                hw_set_temp_cap(&hw, t);
                hw_set_temp_cap_enabled(&hw, true);
            }
            settings_save(&hw);
            return 0;
        } else if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[1]);
            print_usage(argv[0]);
            return 1;
        }
    }

    // Launch interactive TUI applet
    return ui_run(&hw);
}
