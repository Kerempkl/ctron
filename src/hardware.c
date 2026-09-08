#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "hardware.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <libgen.h>
#include <glob.h>
#include <time.h>
#include <sys/wait.h>
#include <ctype.h>

// Aura tables
const char* const AURA_EFFECT_NAMES[] = {
    "static", "breathe", "rainbow-cycle", "rainbow-wave",
    "pulse", "comet", "flash", "stars",
    "rain", "highlight", "laser", "ripple"
};
const int AURA_EFFECT_COUNT = sizeof(AURA_EFFECT_NAMES) / sizeof(AURA_EFFECT_NAMES[0]);

const char* const AURA_COLOR_NAMES[] = {
    "Ice Cyan", "Red", "Green", "Blue", "Yellow", "Purple", "Orange", "White"
};
const char* const AURA_COLOR_HEX[] = {
    "00ffff", "ff0000", "00ff00", "0055ff", "ffff00", "ff00ff", "ff7700", "ffffff"
};
const uint32_t AURA_COLOR_RGB[] = {
    0x00E5FF, 0xFF2222, 0x00FF66, 0x0088FF, 0xFFEE00, 0xDD22FF, 0xFF8800, 0xFFFFFF
};
const int AURA_COLOR_COUNT = sizeof(AURA_COLOR_NAMES) / sizeof(AURA_COLOR_NAMES[0]);

// Logging Ring Buffer
static char s_logs[MAX_LOG_ENTRIES][MAX_LOG_LEN];
static int s_log_head = 0;
static int s_log_count = 0;

void log_add(const char *fmt, ...) {
    time_t raw = time(NULL);
    struct tm *tm_info = localtime(&raw);
    char time_buf[16];
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_info);

    char msg_buf[MAX_LOG_LEN - 32];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);

    snprintf(s_logs[s_log_head], MAX_LOG_LEN, "[%s] %s", time_buf, msg_buf);
    s_log_head = (s_log_head + 1) % MAX_LOG_ENTRIES;
    if (s_log_count < MAX_LOG_ENTRIES) s_log_count++;
}

const char* log_get(int idx) {
    if (idx < 0 || idx >= s_log_count) return "";
    int actual_idx = (s_log_head - 1 - idx + MAX_LOG_ENTRIES) % MAX_LOG_ENTRIES;
    return s_logs[actual_idx];
}

int log_count(void) {
    return s_log_count;
}

// Helpers
static int read_sysfs_str(const char *path, char *out, size_t maxlen) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(out, maxlen, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    char *nl = strchr(out, '\n');
    if (nl) *nl = '\0';
    return 0;
}

static int read_sysfs_int(const char *path) {
    char buf[64];
    if (read_sysfs_str(path, buf, sizeof(buf)) == 0) {
        return atoi(buf);
    }
    return -1;
}

static int wmi_read_int(const char *attr);

static int exec_cmd_quiet(const char *cmd, char *out, size_t outlen) {
    char full[512];
    snprintf(full, sizeof(full), "timeout 2 %s 2>/dev/null", cmd);
    FILE *pipe = popen(full, "r");
    if (!pipe) return -1;
    if (out && outlen > 0) {
        size_t r = fread(out, 1, outlen - 1, pipe);
        out[r] = '\0';
        char *nl = strchr(out, '\n');
        if (nl) *nl = '\0';
    }
    return pclose(pipe);
}

const char* hw_profile_name(asus_profile_t p) {
    switch (p) {
        case PROF_QUIET: return "Quiet";
        case PROF_BALANCED: return "Balanced";
        case PROF_PERFORMANCE: return "Performance";
        default: return "Unknown";
    }
}

const char* hw_epp_name(epp_mode_t e) {
    switch (e) {
        case EPP_POWER: return "power";
        case EPP_BALANCED_POWER: return "balance_power";
        case EPP_BALANCED_PERF: return "balance_performance";
        case EPP_PERFORMANCE: return "performance";
        default: return "default";
    }
}

const char* hw_kbd_name(kbd_bright_t k) {
    switch (k) {
        case KBD_OFF: return "off";
        case KBD_LOW: return "low";
        case KBD_MED: return "med";
        case KBD_HIGH: return "high";
        default: return "unknown";
    }
}

// Hardware initialization
int hw_init(hardware_state_t *hw) {
    memset(hw, 0, sizeof(*hw));

    // Detect Laptop Model via DMI
    char vendor[64] = {0};
    char prod[64] = {0};
    read_sysfs_str("/sys/devices/virtual/dmi/id/sys_vendor", vendor, sizeof(vendor));
    read_sysfs_str("/sys/devices/virtual/dmi/id/product_name", prod, sizeof(prod));

    if (strstr(vendor, "ASUSTeK") || strstr(vendor, "ASUS")) {
        hw->is_asus = true;
    }
    if (prod[0]) {
        snprintf(hw->laptop_model, sizeof(hw->laptop_model), "%s", prod);
    } else {
        snprintf(hw->laptop_model, sizeof(hw->laptop_model), "ASUS TUF Gaming");
    }

    // Detect CPU
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "model name", 10) == 0) {
                char *colon = strchr(line, ':');
                if (colon) {
                    colon++;
                    while (isspace((unsigned char)*colon)) colon++;
                    char *nl = strchr(colon, '\n');
                    if (nl) *nl = '\0';
                    snprintf(hw->cpu_model, sizeof(hw->cpu_model), "%s", colon);
                    break;
                }
            }
        }
        fclose(f);
    }
    if (!hw->cpu_model[0]) snprintf(hw->cpu_model, sizeof(hw->cpu_model), "AMD Ryzen");

    // Check asusctl
    char test_buf[64] = {0};
    if (exec_cmd_quiet("asusctl --help", test_buf, sizeof(test_buf)) == 0) {
        hw->has_asusctl = true;
    }

    // Check asus-armoury driver (sysfs or asusctl)
    if (access("/sys/class/firmware-attributes/asus-armoury", F_OK) == 0) {
        hw->has_asus_armoury = true;
    } else {
        char arm_buf[64] = {0};
        if (hw->has_asusctl && exec_cmd_quiet("asusctl armoury list", arm_buf, sizeof(arm_buf)) == 0 && arm_buf[0] != '\0') {
            hw->has_asus_armoury = true;
        } else {
            hw->has_asus_armoury = false;
        }
    }

    // Initial CPU frequencies
    int min_khz = read_sysfs_int("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_min_freq");
    int max_khz = read_sysfs_int("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
    hw->cpu_min_freq_mhz = (min_khz > 0) ? (min_khz / 1000) : 400;
    hw->cpu_max_freq_mhz = (max_khz > 0) ? (max_khz / 1000) : 4550;
    hw->cpu_target_max_mhz = hw->cpu_max_freq_mhz;
    hw->cpu_applied_max_mhz = hw->cpu_target_max_mhz;
    hw->cpu_temp_cap_c = 95;
    hw->cpu_temp_cap_on = false;
    hw->has_ryzenadj = (exec_cmd_quiet("ryzenadj --help", test_buf, sizeof(test_buf)) == 0);

    // Display detection
    snprintf(hw->display_name, sizeof(hw->display_name), "eDP-1");
    snprintf(hw->display_res, sizeof(hw->display_res), "1920x1080");
    hw->display_min_hz = 60;
    hw->display_max_hz = 144;
    hw->display_cur_hz = 144;

    // Hyprland query if available
    char hypr_out[512] = {0};
    if (exec_cmd_quiet("hyprctl monitors -j", hypr_out, sizeof(hypr_out)) == 0) {
        // Quick parse for active rate and modes
        char *rate_str = strstr(hypr_out, "\"refreshRate\":");
        if (rate_str) {
            float r = 0;
            if (sscanf(rate_str + 14, "%f", &r) == 1) {
                hw->display_cur_hz = (int)(r + 0.5f);
                if (hw->display_cur_hz > 100) hw->display_max_hz = hw->display_cur_hz;
            }
        }
    }

    // Initial battery limit
    int lim = read_sysfs_int("/sys/class/power_supply/BAT1/charge_control_end_threshold");
    if (lim <= 0) lim = read_sysfs_int("/sys/class/power_supply/BAT0/charge_control_end_threshold");
    hw->battery_charge_limit = (lim > 0) ? lim : 80;

    hw_fan_read(hw);
    hw->fan_cpu_stock = hw->fan_cpu;
    hw->fan_gpu_stock = hw->fan_gpu;

    {
        int v;
        v = wmi_read_int("ppt_pl1_spl");
        hw->ppt_spl = (v > 5) ? v : 0;
        v = wmi_read_int("ppt_pl2_sppt");
        hw->ppt_sppt = (v > 5) ? v : 0;
        v = wmi_read_int("ppt_fppt");
        hw->ppt_fppt = (v > 5) ? v : 0;
        v = wmi_read_int("nv_dynamic_boost");
        hw->nv_boost_w = (v >= 5 && v <= 25) ? v : 5;
        v = wmi_read_int("nv_temp_target");
        hw->nv_temp_target = (v >= 75) ? v : 75;
        hw->panel_od = (wmi_read_int("panel_od") == 1);
        v = read_sysfs_int("/sys/devices/system/cpu/cpufreq/boost");
        hw->cpu_boost = (v != 0);
    }

    // Initial active profile
    char prof_buf[64] = {0};
    if (hw->has_asusctl && exec_cmd_quiet("asusctl profile get", prof_buf, sizeof(prof_buf)) == 0) {
        if (strstr(prof_buf, "Quiet")) hw->active_profile = PROF_QUIET;
        else if (strstr(prof_buf, "Balanced")) hw->active_profile = PROF_BALANCED;
        else hw->active_profile = PROF_PERFORMANCE;
    } else {
        char acpi_prof[64] = {0};
        read_sysfs_str("/sys/firmware/acpi/platform_profile", acpi_prof, sizeof(acpi_prof));
        if (strstr(acpi_prof, "quiet")) hw->active_profile = PROF_QUIET;
        else if (strstr(acpi_prof, "balanced")) hw->active_profile = PROF_BALANCED;
        else hw->active_profile = PROF_PERFORMANCE;
    }

    // Initial EPP
    char epp_buf[64] = {0};
    read_sysfs_str("/sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference", epp_buf, sizeof(epp_buf));
    if (strstr(epp_buf, "power") && !strstr(epp_buf, "balance")) hw->active_epp = EPP_POWER;
    else if (strstr(epp_buf, "balance_power")) hw->active_epp = EPP_BALANCED_POWER;
    else if (strstr(epp_buf, "balance_performance")) hw->active_epp = EPP_BALANCED_PERF;
    else hw->active_epp = EPP_PERFORMANCE;

    // Initial keyboard backlight
    int kbd_val = read_sysfs_int("/sys/class/leds/asus::kbd_backlight/brightness");
    if (kbd_val >= 0 && kbd_val <= 3) hw->kbd_brightness = (kbd_bright_t)kbd_val;
    else hw->kbd_brightness = KBD_LOW;

    hw->aura_effect_idx = 0; // static
    hw->aura_color_idx = 0;  // ice cyan

    // ACV Defaults
    hw->theme = THEME_TUF_ICE;
    hw->transparency_pct = 95;
    hw->tint_level = 1; /* Solid Opaque default */
    hw->sync_with_backlight = true;
    hw->sync_with_waybar = true;
    hw->active_input_field = 0;

    hw_read_backlight_hex(hw->custom_hex, &hw->custom_rgb);
    snprintf(hw->hex_input, sizeof(hw->hex_input), "%s", hw->custom_hex);
    snprintf(hw->profile_name_input, sizeof(hw->profile_name_input), "TUF");

    log_add("Hardware initialized: %s", hw->laptop_model);
    return 0;
}

static int read_k10temp_c(void)
{
    glob_t g;
    int c = -1;
    if (glob("/sys/class/hwmon/hwmon*", 0, NULL, &g) != 0)
        return -1;
    for (size_t i = 0; i < g.gl_pathc; i++) {
        char np[256], name[64] = {0};
        snprintf(np, sizeof(np), "%s/name", g.gl_pathv[i]);
        read_sysfs_str(np, name, sizeof(name));
        if (strcmp(name, "k10temp") != 0)
            continue;
        snprintf(np, sizeof(np), "%s/temp1_input", g.gl_pathv[i]);
        int raw = read_sysfs_int(np);
        if (raw > 0)
            c = raw / 1000;
        break;
    }
    globfree(&g);
    return c;
}

/* Fast: temp, clocks, battery. Safe for the TUI 250 ms loop. */
int hw_poll_telemetry(hardware_state_t *hw) {
    int t = read_k10temp_c();
    if (t > 0)
        hw->cpu_temp_c = t;

    int cur_khz = read_sysfs_int("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    if (cur_khz > 0)
        hw->cpu_cur_freq_mhz = cur_khz / 1000;
    int max_khz = read_sysfs_int("/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq");
    if (max_khz > 0)
        hw->cpu_applied_max_mhz = max_khz / 1000;

    int cap = read_sysfs_int("/sys/class/power_supply/BAT1/capacity");
    if (cap < 0)
        cap = read_sysfs_int("/sys/class/power_supply/BAT0/capacity");
    if (cap >= 0)
        hw->battery_percent = cap;

    char bat_stat[32] = {0};
    if (read_sysfs_str("/sys/class/power_supply/BAT1/status", bat_stat, sizeof(bat_stat)) < 0)
        read_sysfs_str("/sys/class/power_supply/BAT0/status", bat_stat, sizeof(bat_stat));
    if (bat_stat[0])
        snprintf(hw->battery_status, sizeof(hw->battery_status), "%s", bat_stat);

    int ac = read_sysfs_int("/sys/class/power_supply/ACAD/online");
    if (ac < 0)
        ac = read_sysfs_int("/sys/class/power_supply/AC/online");
    hw->battery_ac_connected = (ac == 1);

    return 0;
}

/* Full sysfs/asusctl snapshot. CLI --status/--doctor. Not the TUI poll. */
int hw_refresh_live(hardware_state_t *hw)
{
    hw_poll_telemetry(hw);
    hw_fan_read(hw);

    {
        int v;
        v = wmi_read_int("ppt_pl1_spl");
        hw->ppt_spl = (v > 5) ? v : 0;
        v = wmi_read_int("ppt_pl2_sppt");
        hw->ppt_sppt = (v > 5) ? v : 0;
        v = wmi_read_int("ppt_fppt");
        hw->ppt_fppt = (v > 5) ? v : 0;
        v = wmi_read_int("nv_dynamic_boost");
        if (v >= 5 && v <= 25)
            hw->nv_boost_w = v;
        v = wmi_read_int("nv_temp_target");
        if (v >= 75)
            hw->nv_temp_target = v;
        v = wmi_read_int("panel_od");
        if (v >= 0)
            hw->panel_od = (v == 1);
        v = read_sysfs_int("/sys/devices/system/cpu/cpufreq/boost");
        if (v >= 0)
            hw->cpu_boost = (v != 0);
    }

    int lim = read_sysfs_int("/sys/class/power_supply/BAT1/charge_control_end_threshold");
    if (lim <= 0)
        lim = read_sysfs_int("/sys/class/power_supply/BAT0/charge_control_end_threshold");
    if (lim > 0)
        hw->battery_charge_limit = lim;

    char acpi_prof[64] = {0};
    read_sysfs_str("/sys/firmware/acpi/platform_profile", acpi_prof, sizeof(acpi_prof));
    if (strstr(acpi_prof, "quiet"))
        hw->active_profile = PROF_QUIET;
    else if (strstr(acpi_prof, "balanced"))
        hw->active_profile = PROF_BALANCED;
    else if (strstr(acpi_prof, "performance"))
        hw->active_profile = PROF_PERFORMANCE;
    else if (hw->has_asusctl) {
        char prof_buf[64] = {0};
        if (exec_cmd_quiet("asusctl profile get", prof_buf, sizeof(prof_buf)) == 0) {
            if (strstr(prof_buf, "Quiet"))
                hw->active_profile = PROF_QUIET;
            else if (strstr(prof_buf, "Balanced"))
                hw->active_profile = PROF_BALANCED;
            else if (strstr(prof_buf, "Performance"))
                hw->active_profile = PROF_PERFORMANCE;
        }
    }

    char epp_buf[64] = {0};
    read_sysfs_str("/sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference",
                   epp_buf, sizeof(epp_buf));
    if (strstr(epp_buf, "balance_power"))
        hw->active_epp = EPP_BALANCED_POWER;
    else if (strstr(epp_buf, "balance_performance"))
        hw->active_epp = EPP_BALANCED_PERF;
    else if (strstr(epp_buf, "power"))
        hw->active_epp = EPP_POWER;
    else if (strstr(epp_buf, "performance"))
        hw->active_epp = EPP_PERFORMANCE;

    int kbd_val = read_sysfs_int("/sys/class/leds/asus::kbd_backlight/brightness");
    if (kbd_val >= 0 && kbd_val <= 3)
        hw->kbd_brightness = (kbd_bright_t)kbd_val;

    char hypr_out[512] = {0};
    if (exec_cmd_quiet("hyprctl monitors -j", hypr_out, sizeof(hypr_out)) == 0) {
        char *rate_str = strstr(hypr_out, "\"refreshRate\":");
        if (rate_str) {
            float r = 0;
            if (sscanf(rate_str + 14, "%f", &r) == 1)
                hw->display_cur_hz = (int)(r + 0.5f);
        }
    }

    return 0;
}

// Hardware setters
int hw_set_profile(hardware_state_t *hw, asus_profile_t profile) {
    if (profile >= PROF_COUNT) return -1;
    const char *name = hw_profile_name(profile);
    int rc = -1;

    if (hw->has_asusctl) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "asusctl profile set %s", name);
        rc = exec_cmd_quiet(cmd, NULL, 0);
    }
    if (rc != 0) {
        // Fallback to acpi platform_profile
        const char *acpi_name = "balanced";
        if (profile == PROF_QUIET) acpi_name = "quiet";
        else if (profile == PROF_PERFORMANCE) acpi_name = "performance";
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "echo %s | sudo tee /sys/firmware/acpi/platform_profile", acpi_name);
        rc = exec_cmd_quiet(cmd, NULL, 0);
    }

    hw->active_profile = profile;
    log_add("Profile set to %s", name);
    return rc;
}

int hw_set_epp(hardware_state_t *hw, epp_mode_t epp) {
    if (epp >= EPP_COUNT) return -1;
    const char *name = hw_epp_name(epp);
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "echo %s | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/energy_performance_preference", name);
    int rc = exec_cmd_quiet(cmd, NULL, 0);
    hw->active_epp = epp;
    log_add("EPP preference set to %s", name);
    return rc;
}

int hw_set_cpu_max_freq(hardware_state_t *hw, int mhz) {
    if (mhz < hw->cpu_min_freq_mhz || mhz > hw->cpu_max_freq_mhz + 500) return -1;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "echo %d | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_max_freq", mhz * 1000);
    int rc = exec_cmd_quiet(cmd, NULL, 0);
    hw->cpu_target_max_mhz = mhz;
    hw->cpu_applied_max_mhz = mhz;
    log_add("CPU max freq set to %d MHz", mhz);
    return rc;
}

int hw_set_temp_cap(hardware_state_t *hw, int celsius) {
    if (celsius < 70) celsius = 70;
    if (celsius > 105) celsius = 105;
    hw->cpu_temp_cap_c = celsius;
    if (hw->has_ryzenadj) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "sudo ryzenadj --tctl-temp=%d", celsius);
        exec_cmd_quiet(cmd, NULL, 0);
    }
    log_add("Tctl cap %d°C (%s)", celsius, hw->cpu_temp_cap_on ? "on" : "off");
    return 0;
}

int hw_set_temp_cap_enabled(hardware_state_t *hw, bool on) {
    hw->cpu_temp_cap_on = on;
    if (on)
        hw_set_temp_cap(hw, hw->cpu_temp_cap_c);
    else if (hw->cpu_target_max_mhz > 0)
        hw_set_cpu_max_freq(hw, hw->cpu_target_max_mhz);
    log_add("Tctl cap %s", on ? "ON" : "OFF");
    return 0;
}

int hw_set_display_hz(hardware_state_t *hw, int hz) {
    if (hz <= 0) return -1;
    int rc = -1;

    // Check Hyprland
    if (getenv("HYPRLAND_INSTANCE_SIGNATURE")) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "hyprctl keyword monitor \"%s,%s@%d,0x0,1\"",
                 hw->display_name, hw->display_res, hz);
        rc = exec_cmd_quiet(cmd, NULL, 0);
    }
    // Fallback: wlr-randr
    if (rc != 0) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "wlr-randr --output %s --mode %s@%dHz",
                 hw->display_name, hw->display_res, hz);
        rc = exec_cmd_quiet(cmd, NULL, 0);
    }
    // Fallback: kscreen-doctor
    if (rc != 0) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "kscreen-doctor output.%s.mode.%d", hw->display_name, hz);
        rc = exec_cmd_quiet(cmd, NULL, 0);
    }
    // Fallback: xrandr
    if (rc != 0) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "xrandr --output %s --mode %s -r %d",
                 hw->display_name, hw->display_res, hz);
        rc = exec_cmd_quiet(cmd, NULL, 0);
    }

    hw->display_cur_hz = hz;
    log_add("Display refresh rate set to %d Hz", hz);
    return 0;
}

int hw_set_battery_limit(hardware_state_t *hw, int limit) {
    if (limit < 20 || limit > 100) return -1;
    int rc = -1;

    if (hw->has_asusctl) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "asusctl battery limit %d", limit);
        rc = exec_cmd_quiet(cmd, NULL, 0);
    }
    if (rc != 0) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "echo %d | sudo tee /sys/class/power_supply/BAT*/charge_control_end_threshold", limit);
        rc = exec_cmd_quiet(cmd, NULL, 0);
    }

    hw->battery_charge_limit = limit;
    log_add("Battery charge limit set to %d%%", limit);
    return 0;
}

static int hwmon_by_name(const char *want, char *out, size_t n)
{
    glob_t g;
    int rc = -1;

    if (glob("/sys/class/hwmon/hwmon*", 0, NULL, &g) != 0)
        return -1;
    for (size_t i = 0; i < g.gl_pathc; i++) {
        char np[256], name[64] = {0};
        snprintf(np, sizeof(np), "%s/name", g.gl_pathv[i]);
        read_sysfs_str(np, name, sizeof(name));
        if (!strcmp(name, want)) {
            snprintf(out, n, "%s", g.gl_pathv[i]);
            rc = 0;
            break;
        }
    }
    globfree(&g);
    return rc;
}

static void fan_read_one(const char *base, const char *pwm, fan_curve_t *fc)
{
    char p[256];
    fc->n = FAN_POINTS;
    for (int i = 0; i < FAN_POINTS; i++) {
        snprintf(p, sizeof(p), "%s/%s_auto_point%d_temp", base, pwm, i + 1);
        fc->temp_c[i] = read_sysfs_int(p);
        snprintf(p, sizeof(p), "%s/%s_auto_point%d_pwm", base, pwm, i + 1);
        fc->pwm[i] = read_sysfs_int(p);
        if (fc->temp_c[i] < 0) fc->temp_c[i] = 20 + i * 10;
        if (fc->pwm[i] < 0) fc->pwm[i] = 30 + i * 20;
    }
}

int hw_fan_read(hardware_state_t *hw)
{
    char base[128];

    if (hwmon_by_name("asus_custom_fan_curve", base, sizeof(base)) < 0) {
        hw->has_fan_curve = false;
        return -1;
    }
    hw->has_fan_curve = true;
    fan_read_one(base, "pwm1", &hw->fan_cpu);
    fan_read_one(base, "pwm2", &hw->fan_gpu);
    {
        char p[256];
        snprintf(p, sizeof(p), "%s/pwm1_enable", base);
        hw->fan_cpu_on = (read_sysfs_int(p) == 2);
        snprintf(p, sizeof(p), "%s/pwm2_enable", base);
        hw->fan_gpu_on = (read_sysfs_int(p) == 2);
    }
    return 0;
}

static void fan_data_str(const fan_curve_t *fc, char *out, size_t n)
{
    size_t off = 0;
    out[0] = '\0';
    int np = fc->n > 0 && fc->n <= FAN_POINTS ? fc->n : FAN_POINTS;
    for (int i = 0; i < np; i++) {
        int w = snprintf(out + off, n - off, "%s%dc:%d",
                         i ? "," : "", fc->temp_c[i], fc->pwm[i]);
        if (w < 0 || (size_t)w >= n - off)
            break;
        off += (size_t)w;
    }
}

int hw_fan_apply(hardware_state_t *hw)
{
    char base[128];
    int rc = 0;

    if (hw->fan_cpu.n < 1 && hw->fan_gpu.n < 1) {
        log_add("Fan: add a point first");
        return -1;
    }
    if (hwmon_by_name("asus_custom_fan_curve", base, sizeof(base)) == 0) {
        for (int i = 0; i < FAN_POINTS; i++) {
            char cmd[320], path[256];
            int ic = hw->fan_cpu.n > 0
                ? (i < hw->fan_cpu.n ? i : hw->fan_cpu.n - 1) : 0;
            int ig = hw->fan_gpu.n > 0
                ? (i < hw->fan_gpu.n ? i : hw->fan_gpu.n - 1) : 0;
            int tc = hw->fan_cpu.n > 0 ? hw->fan_cpu.temp_c[ic] : 20;
            int pc = hw->fan_cpu.n > 0 ? hw->fan_cpu.pwm[ic] : 0;
            int tg = hw->fan_gpu.n > 0 ? hw->fan_gpu.temp_c[ig] : 20;
            int pg = hw->fan_gpu.n > 0 ? hw->fan_gpu.pwm[ig] : 0;
            snprintf(path, sizeof(path), "%s/pwm1_auto_point%d_temp", base, i + 1);
            snprintf(cmd, sizeof(cmd), "echo %d | sudo tee %s", tc, path);
            exec_cmd_quiet(cmd, NULL, 0);
            snprintf(path, sizeof(path), "%s/pwm1_auto_point%d_pwm", base, i + 1);
            snprintf(cmd, sizeof(cmd), "echo %d | sudo tee %s", pc, path);
            exec_cmd_quiet(cmd, NULL, 0);
            snprintf(path, sizeof(path), "%s/pwm2_auto_point%d_temp", base, i + 1);
            snprintf(cmd, sizeof(cmd), "echo %d | sudo tee %s", tg, path);
            exec_cmd_quiet(cmd, NULL, 0);
            snprintf(path, sizeof(path), "%s/pwm2_auto_point%d_pwm", base, i + 1);
            snprintf(cmd, sizeof(cmd), "echo %d | sudo tee %s", pg, path);
            exec_cmd_quiet(cmd, NULL, 0);
        }
        {
            char cmd[320];
            snprintf(cmd, sizeof(cmd), "echo %d | sudo tee %s/pwm1_enable",
                     hw->fan_cpu_on ? 2 : 0, base);
            exec_cmd_quiet(cmd, NULL, 0);
            snprintf(cmd, sizeof(cmd), "echo %d | sudo tee %s/pwm2_enable",
                     hw->fan_gpu_on ? 2 : 0, base);
            exec_cmd_quiet(cmd, NULL, 0);
        }
    }

    if (hw->has_asusctl) {
        const char *prof = hw_profile_name(hw->active_profile);
        char data[256], cmd[512];
        fan_data_str(&hw->fan_cpu, data, sizeof(data));
        snprintf(cmd, sizeof(cmd),
                 "asusctl fan-curve --mod-profile %s --fan cpu --data '%s'", prof, data);
        rc = exec_cmd_quiet(cmd, NULL, 0);
        snprintf(cmd, sizeof(cmd),
                 "asusctl fan-curve --mod-profile %s --enable-fan-curve %s --fan cpu",
                 prof, hw->fan_cpu_on ? "true" : "false");
        exec_cmd_quiet(cmd, NULL, 0);
        fan_data_str(&hw->fan_gpu, data, sizeof(data));
        snprintf(cmd, sizeof(cmd),
                 "asusctl fan-curve --mod-profile %s --fan gpu --data '%s'", prof, data);
        exec_cmd_quiet(cmd, NULL, 0);
        snprintf(cmd, sizeof(cmd),
                 "asusctl fan-curve --mod-profile %s --enable-fan-curve %s --fan gpu",
                 prof, hw->fan_gpu_on ? "true" : "false");
        exec_cmd_quiet(cmd, NULL, 0);
    }

    log_add("Fan curves %s (CPU %s GPU %s)",
            hw->has_fan_curve ? "applied" : "asusctl",
            hw->fan_cpu_on ? "on" : "off",
            hw->fan_gpu_on ? "on" : "off");
    return rc;
}

static void scale_pwm(fan_curve_t *fc, const fan_curve_t *src, int num, int den)
{
    *fc = *src;
    if (fc->n < 1) fc->n = FAN_POINTS;
    for (int i = 0; i < fc->n && i < FAN_POINTS; i++) {
        int p = src->pwm[i] * num / den;
        if (p < 0) p = 0;
        if (p > 255) p = 255;
        fc->pwm[i] = p;
    }
}

int hw_fan_preset(hardware_state_t *hw, int preset)
{
    switch (preset) {
    case 1: /* silent */
        scale_pwm(&hw->fan_cpu, &hw->fan_cpu_stock, 6, 10);
        scale_pwm(&hw->fan_gpu, &hw->fan_gpu_stock, 6, 10);
        break;
    case 2: /* cool — more air earlier */
        scale_pwm(&hw->fan_cpu, &hw->fan_cpu_stock, 13, 10);
        scale_pwm(&hw->fan_gpu, &hw->fan_gpu_stock, 13, 10);
        hw->fan_cpu.pwm[FAN_POINTS - 1] = 255;
        hw->fan_gpu.pwm[FAN_POINTS - 1] = 255;
        break;
    case 3: /* full */
        hw->fan_cpu = hw->fan_cpu_stock;
        hw->fan_gpu = hw->fan_gpu_stock;
        for (int i = 4; i < FAN_POINTS; i++) {
            hw->fan_cpu.pwm[i] = 255;
            hw->fan_gpu.pwm[i] = 255;
        }
        break;
    default: /* stock */
        hw->fan_cpu = hw->fan_cpu_stock;
        hw->fan_gpu = hw->fan_gpu_stock;
        break;
    }
    hw->fan_cpu_on = true;
    hw->fan_gpu_on = true;
    return hw_fan_apply(hw);
}

int hw_fan_enable(hardware_state_t *hw, bool cpu_on, bool gpu_on)
{
    hw->fan_cpu_on = cpu_on;
    hw->fan_gpu_on = gpu_on;
    return hw_fan_apply(hw);
}

static void fan_clamp_xy(int *temp, int *pwm)
{
    if (*temp < 20) *temp = 20;
    if (*temp > 105) *temp = 105;
    if (*pwm < 0) *pwm = 0;
    if (*pwm > 255) *pwm = 255;
}

/* Sort by X (°C). keep_idx follows the point. Temps are made unique so
 * points do not stack on one cell. Returns the new index of keep_idx. */
static int fan_sort_xy(fan_curve_t *fc, int keep_idx)
{
    int n = fc->n;
    if (n < 0) {
        fc->n = 0;
        return 0;
    }
    if (n > FAN_POINTS) n = fc->n = FAN_POINTS;
    if (n < 2) {
        if (keep_idx < 0) return 0;
        if (keep_idx >= n) return n ? n - 1 : 0;
        return keep_idx;
    }

    int order[FAN_POINTS];
    for (int i = 0; i < n; i++)
        order[i] = i;
    for (int i = 1; i < n; i++) {
        int oi = order[i];
        int j = i;
        while (j > 0 && fc->temp_c[order[j - 1]] > fc->temp_c[oi]) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = oi;
    }
    int nt[FAN_POINTS], npwm[FAN_POINTS], new_keep = 0;
    for (int i = 0; i < n; i++) {
        nt[i] = fc->temp_c[order[i]];
        npwm[i] = fc->pwm[order[i]];
        if (order[i] == keep_idx)
            new_keep = i;
    }
    for (int i = 0; i < n; i++) {
        fc->temp_c[i] = nt[i];
        fc->pwm[i] = npwm[i];
    }

    for (int i = 1; i < n; i++) {
        if (fc->temp_c[i] <= fc->temp_c[i - 1])
            fc->temp_c[i] = fc->temp_c[i - 1] + 1;
    }
    if (fc->temp_c[n - 1] > 105) {
        fc->temp_c[n - 1] = 105;
        for (int i = n - 2; i >= 0; i--) {
            if (fc->temp_c[i] >= fc->temp_c[i + 1])
                fc->temp_c[i] = fc->temp_c[i + 1] - 1;
            if (fc->temp_c[i] < 20)
                fc->temp_c[i] = 20;
        }
        for (int i = 1; i < n; i++) {
            if (fc->temp_c[i] <= fc->temp_c[i - 1]) {
                int t = fc->temp_c[i - 1] + 1;
                fc->temp_c[i] = (t > 105) ? 105 : t;
            }
        }
    }
    return new_keep;
}

static int parse_csv_ints(const char *s, int *out, int max)
{
    int n = 0;
    const char *p = s;
    if (!s || !*s)
        return 0;
    while (*p && n < max) {
        while (*p == ',' || isspace((unsigned char)*p))
            p++;
        if (!*p)
            break;
        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end == p)
            break;
        out[n++] = (int)v;
        p = end;
    }
    return n;
}

int hw_fan_from_csv(fan_curve_t *fc, const char *temps, const char *pwms)
{
    int t[FAN_POINTS], p[FAN_POINTS];
    int nt = parse_csv_ints(temps, t, FAN_POINTS);
    int np = parse_csv_ints(pwms, p, FAN_POINTS);
    int n = nt < np ? nt : np;
    if (n < 1)
        return -1;
    fc->n = n;
    for (int i = 0; i < n; i++) {
        int tc = t[i], pc = p[i];
        fan_clamp_xy(&tc, &pc);
        fc->temp_c[i] = tc;
        fc->pwm[i] = pc;
    }
    fan_sort_xy(fc, 0);
    return fc->n;
}

void hw_fan_to_csv(const fan_curve_t *fc, char *temps, size_t tn, char *pwms, size_t pn)
{
    size_t to = 0, po = 0;
    if (temps && tn)
        temps[0] = '\0';
    if (pwms && pn)
        pwms[0] = '\0';
    int n = fc && fc->n > 0 && fc->n <= FAN_POINTS ? fc->n : 0;
    for (int i = 0; i < n; i++) {
        int w;
        if (temps && tn > to) {
            w = snprintf(temps + to, tn - to, "%s%d", i ? "," : "", fc->temp_c[i]);
            if (w > 0)
                to += (size_t)w;
        }
        if (pwms && pn > po) {
            w = snprintf(pwms + po, pn - po, "%s%d", i ? "," : "", fc->pwm[i]);
            if (w > 0)
                po += (size_t)w;
        }
    }
}

int hw_fan_nudge_point(hardware_state_t *hw, int gpu, int idx, int dtemp, int dpwm)
{
    fan_curve_t *fc = gpu ? &hw->fan_gpu : &hw->fan_cpu;
    if (fc->n < 1 || idx < 0 || idx >= fc->n) return -1;
    int t = fc->temp_c[idx] + dtemp;
    int p = fc->pwm[idx] + dpwm;
    fan_clamp_xy(&t, &p);
    fc->temp_c[idx] = t;
    fc->pwm[idx] = p;
    return fan_sort_xy(fc, idx);
}

int hw_fan_add_point(hardware_state_t *hw, int gpu, int temp, int pwm)
{
    fan_curve_t *fc = gpu ? &hw->fan_gpu : &hw->fan_cpu;
    if (fc->n < 0) fc->n = 0;
    if (fc->n >= FAN_POINTS) return -1;

    if (temp < 0) {
        if (fc->n == 0) {
            temp = 40;
            pwm = 80;
        } else {
            int best_i = -1, best_gap = fc->temp_c[0] - 20;
            for (int i = 0; i < fc->n - 1; i++) {
                int g = fc->temp_c[i + 1] - fc->temp_c[i];
                if (g > best_gap) {
                    best_gap = g;
                    best_i = i;
                }
            }
            int tail = 105 - fc->temp_c[fc->n - 1];
            if (tail > best_gap) {
                temp = (fc->temp_c[fc->n - 1] + 105) / 2;
                pwm = fc->pwm[fc->n - 1];
            } else if (best_i < 0) {
                temp = (20 + fc->temp_c[0]) / 2;
                pwm = fc->pwm[0];
            } else {
                temp = (fc->temp_c[best_i] + fc->temp_c[best_i + 1]) / 2;
                pwm = (fc->pwm[best_i] + fc->pwm[best_i + 1]) / 2;
            }
        }
    }
    fan_clamp_xy(&temp, &pwm);
    fc->temp_c[fc->n] = temp;
    fc->pwm[fc->n] = pwm;
    fc->n++;
    return fan_sort_xy(fc, fc->n - 1);
}

int hw_fan_del_point(hardware_state_t *hw, int gpu, int idx)
{
    fan_curve_t *fc = gpu ? &hw->fan_gpu : &hw->fan_cpu;
    if (fc->n < 1) return -1;
    if (idx < 0 || idx >= fc->n) idx = fc->n - 1;
    for (int i = idx; i < fc->n - 1; i++) {
        fc->temp_c[i] = fc->temp_c[i + 1];
        fc->pwm[i] = fc->pwm[i + 1];
    }
    fc->n--;
    return 0;
}

int hw_fan_set_abs(hardware_state_t *hw, int gpu, int idx, int temp, int pwm)
{
    fan_curve_t *fc = gpu ? &hw->fan_gpu : &hw->fan_cpu;
    if (fc->n < 1) {
        return hw_fan_add_point(hw, gpu, temp, pwm);
    }
    if (idx < 0 || idx >= fc->n) idx = fc->n - 1;
    fan_clamp_xy(&temp, &pwm);
    fc->temp_c[idx] = temp;
    fc->pwm[idx] = pwm;
    return fan_sort_xy(fc, idx);
}

#define WMI_BASE "/sys/devices/platform/asus-nb-wmi"

static int wmi_read_int(const char *attr)
{
    char path[256];
    snprintf(path, sizeof(path), WMI_BASE "/%s", attr);
    return read_sysfs_int(path);
}

static int wmi_write_int(const char *attr, int v)
{
    char cmd[320], path[256];
    snprintf(path, sizeof(path), WMI_BASE "/%s", attr);
    snprintf(cmd, sizeof(cmd), "echo %d | sudo tee %s", v, path);
    return exec_cmd_quiet(cmd, NULL, 0);
}

/* FA507N Armoury Crate (same family as FA507NVR). Watts. */
void hw_ppt_limits(const hardware_state_t *hw, int *spl_min, int *spl_max,
                   int *sppt_min, int *sppt_max, int *fppt_min, int *fppt_max)
{
    if (hw->battery_ac_connected) {
        *spl_min = 15; *spl_max = 80;
        *sppt_min = 35; *sppt_max = 80;
        *fppt_min = 35; *fppt_max = 80;
    } else {
        *spl_min = 15; *spl_max = 65;
        *sppt_min = 35; *sppt_max = 65;
        *fppt_min = 35; *fppt_max = 65;
    }
}

static int clamp_i(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int hw_set_ppt(hardware_state_t *hw, int spl, int sppt, int fppt)
{
    int smin, smax, pmin, pmax, fmin, fmax;
    hw_ppt_limits(hw, &smin, &smax, &pmin, &pmax, &fmin, &fmax);
    spl = clamp_i(spl, smin, smax);
    sppt = clamp_i(sppt, pmin, pmax);
    fppt = clamp_i(fppt, fmin, fmax);
    if (sppt < spl) sppt = spl;
    if (fppt < sppt) fppt = sppt;
    wmi_write_int("ppt_pl1_spl", spl);
    wmi_write_int("ppt_pl2_sppt", sppt);
    wmi_write_int("ppt_fppt", fppt);
    hw->ppt_spl = spl;
    hw->ppt_sppt = sppt;
    hw->ppt_fppt = fppt;
    log_add("PPT SPL %d SPPT %d FPPT %d W", spl, sppt, fppt);
    return 0;
}

int hw_set_nv_boost(hardware_state_t *hw, int watts)
{
    watts = clamp_i(watts, 5, 25);
    wmi_write_int("nv_dynamic_boost", watts);
    hw->nv_boost_w = watts;
    log_add("NV dynamic boost %d W", watts);
    return 0;
}

int hw_set_nv_temp(hardware_state_t *hw, int celsius)
{
    celsius = clamp_i(celsius, 75, 87);
    wmi_write_int("nv_temp_target", celsius);
    hw->nv_temp_target = celsius;
    log_add("NV temp target %d°C", celsius);
    return 0;
}

int hw_set_panel_od(hardware_state_t *hw, bool on)
{
    wmi_write_int("panel_od", on ? 1 : 0);
    hw->panel_od = on;
    log_add("Panel OD %s", on ? "on" : "off");
    return 0;
}

int hw_set_cpu_boost(hardware_state_t *hw, bool on)
{
    char cmd[192];
    snprintf(cmd, sizeof(cmd), "echo %d | sudo tee /sys/devices/system/cpu/cpufreq/boost", on ? 1 : 0);
    exec_cmd_quiet(cmd, NULL, 0);
    hw->cpu_boost = on;
    log_add("CPU boost %s", on ? "on" : "off");
    return 0;
}

int hw_battery_oneshot(hardware_state_t *hw)
{
    (void)hw;
    int rc = exec_cmd_quiet("asusctl battery oneshot", NULL, 0);
    log_add("Battery oneshot %s", rc == 0 ? "ok" : "fail");
    return rc;
}

int hw_set_kbd_brightness(hardware_state_t *hw, kbd_bright_t lvl) {
    if (lvl >= KBD_COUNT) return -1;
    const char *name = hw_kbd_name(lvl);
    int rc = -1;

    if (hw->has_asusctl) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "asusctl leds set %s", name);
        rc = exec_cmd_quiet(cmd, NULL, 0);
    }
    if (rc != 0) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "echo %d | sudo tee /sys/class/leds/asus::kbd_backlight/brightness", (int)lvl);
        rc = exec_cmd_quiet(cmd, NULL, 0);
    }

    hw->kbd_brightness = lvl;
    log_add("Keyboard backlight set to %s", name);
    return 0;
}

int hw_set_aura(hardware_state_t *hw, int effect_idx, int color_idx) {
    if (effect_idx < 0 || effect_idx >= AURA_EFFECT_COUNT) return -1;
    if (color_idx < 0 || color_idx >= AURA_COLOR_COUNT) return -1;

    const char *effect = AURA_EFFECT_NAMES[effect_idx];
    const char *hex = AURA_COLOR_HEX[color_idx];
    char cmd[256];

    if (strcmp(effect, "static") == 0 || strcmp(effect, "pulse") == 0 ||
        strcmp(effect, "comet") == 0  || strcmp(effect, "flash") == 0) {
        snprintf(cmd, sizeof(cmd), "asusctl aura effect %s -c %s", effect, hex);
    } else if (strcmp(effect, "highlight") == 0 || strcmp(effect, "laser") == 0 || strcmp(effect, "ripple") == 0) {
        snprintf(cmd, sizeof(cmd), "asusctl aura effect %s -c %s --speed med", effect, hex);
    } else if (strcmp(effect, "breathe") == 0 || strcmp(effect, "stars") == 0) {
        snprintf(cmd, sizeof(cmd), "asusctl aura effect %s --colour %s --colour2 000000 --speed med", effect, hex);
    } else if (strcmp(effect, "rainbow-cycle") == 0 || strcmp(effect, "rain") == 0) {
        snprintf(cmd, sizeof(cmd), "asusctl aura effect %s --speed med", effect);
    } else if (strcmp(effect, "rainbow-wave") == 0) {
        snprintf(cmd, sizeof(cmd), "asusctl aura effect rainbow-wave --direction right --speed med");
    } else {
        snprintf(cmd, sizeof(cmd), "asusctl aura effect %s -c %s", effect, hex);
    }

    exec_cmd_quiet(cmd, NULL, 0);
    hw->aura_effect_idx = effect_idx;
    hw->aura_color_idx = color_idx;
    log_add("Aura lighting: %s (%s)", effect, AURA_COLOR_NAMES[color_idx]);
    return 0;
}

int hw_armoury_get(const char *attr, char *out, size_t maxlen) {
    char path[256];
    snprintf(path, sizeof(path), "/sys/class/firmware-attributes/asus-armoury/attributes/%s/current_value", attr);
    if (read_sysfs_str(path, out, maxlen) == 0) return 0;

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "asusctl armoury get %s", attr);
    return exec_cmd_quiet(cmd, out, maxlen);
}

int hw_armoury_set(const char *attr, const char *val) {
    char path[256];
    snprintf(path, sizeof(path), "/sys/class/firmware-attributes/asus-armoury/attributes/%s/current_value", attr);
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "echo %s | sudo tee %s", val, path);
    if (exec_cmd_quiet(cmd, NULL, 0) == 0) {
        log_add("asus-armoury: set %s = %s", attr, val);
        return 0;
    }

    snprintf(cmd, sizeof(cmd), "asusctl armoury set %s %s", attr, val);
    int rc = exec_cmd_quiet(cmd, NULL, 0);
    if (rc == 0) {
        log_add("asusctl armoury: set %s = %s", attr, val);
    }
    return rc;
}

void hw_calc_hsv_rgb(int h, int s, int v, uint32_t *out_rgb, char out_hex[8]) {
    h = (h % 360 + 360) % 360;
    if (s < 0) s = 0;
    if (s > 100) s = 100;
    if (v < 0) v = 0;
    if (v > 100) v = 100;

    float hf = (float)h / 60.0f;
    float sf = (float)s / 100.0f;
    float vf = (float)v / 100.0f;

    int i = (int)hf;
    float f = hf - (float)i;
    float p = vf * (1.0f - sf);
    float q = vf * (1.0f - sf * f);
    float t = vf * (1.0f - sf * (1.0f - f));

    float r = 0, g = 0, b = 0;
    switch (i) {
        case 0: r = vf; g = t;  b = p;  break;
        case 1: r = q;  g = vf; b = p;  break;
        case 2: r = p;  g = vf; b = t;  break;
        case 3: r = p;  g = q;  b = vf; break;
        case 4: r = t;  g = p;  b = vf; break;
        default: r = vf; g = p; b = q;  break;
    }
    int ri = (int)(r * 255.0f + 0.5f);
    int gi = (int)(g * 255.0f + 0.5f);
    int bi = (int)(b * 255.0f + 0.5f);
    uint32_t rgb = ((ri & 0xFF) << 16) | ((gi & 0xFF) << 8) | (bi & 0xFF);
    if (out_rgb) *out_rgb = rgb;
    if (out_hex) snprintf(out_hex, 8, "%02x%02x%02x", ri, gi, bi);
}

void hw_sync_waybar_color(const char *hex) {
    if (!hex || strlen(hex) < 6) return;
    /* Sync OFF: do not write CSS, do not poke waybar. */
    if (access("/tmp/acv-waybar-sync.off", F_OK) == 0)
        return;
    char path[512];
    const char *home = getenv("HOME");
    if (!home) home = "/home/arcioth";

    // 1. Write /tmp/waybar-kb-hex
    FILE *f_tmp = fopen("/tmp/waybar-kb-hex", "w");
    if (f_tmp) {
        fprintf(f_tmp, "%s\n", hex);
        fclose(f_tmp);
    }

    // 2. Write ~/.config/waybar/colors.css
    snprintf(path, sizeof(path), "%s/.config/waybar/colors.css", home);
    FILE *f_css = fopen(path, "w");
    if (f_css) {
        unsigned int r = 0, g = 0, b = 0;
        sscanf(hex, "%02x%02x%02x", &r, &g, &b);
        fprintf(f_css, "@define-color kb #%s;\n", hex);
        fprintf(f_css, "@define-color kb-fg #05080c;\n");
        fprintf(f_css, "@define-color kb-muted rgba(%u, %u, %u, 0.45);\n", r, g, b);
        fprintf(f_css, "@define-color kb-bg rgba(5, 8, 12, 0.42);\n");
        fclose(f_css);
    }

    exec_cmd_quiet("pkill -USR2 waybar 2>/dev/null || true", NULL, 0);
}

void hw_apply_transparency(int pct) {
    if (pct < 20) pct = 20;
    if (pct > 100) pct = 100;
    float op = (float)pct / 100.0f;

    // Hyprland dynamic window property
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "hyprctl setprop active opacity %.2f lock 2>/dev/null", op);
    exec_cmd_quiet(cmd, NULL, 0);

    // Terminal OSC 11 background transparency sequence
    printf("\033]11;[%d]%%#050A0E\007", pct);
    fflush(stdout);
}

int hw_set_aura_hex(hardware_state_t *hw, const char *hex) {
    if (!hex || strlen(hex) < 6) return -1;
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "asusctl aura effect static -c %s", hex);
    exec_cmd_quiet(cmd, NULL, 0);

    snprintf(hw->custom_hex, sizeof(hw->custom_hex), "%s", hex);
    unsigned int r = 0, g = 0, b = 0;
    sscanf(hex, "%02x%02x%02x", &r, &g, &b);
    hw->custom_rgb = (r << 16) | (g << 8) | b;

    log_add("Aura custom hex: #%s", hex);

    if (hw->sync_with_waybar)
        hw_sync_waybar_color(hex);
    return 0;
}

void hw_waybar_sync_enable(bool on)
{
    if (on)
        unlink("/tmp/acv-waybar-sync.off");
    else {
        FILE *f = fopen("/tmp/acv-waybar-sync.off", "w");
        if (f) fclose(f);
    }
}

const char* hw_theme_name(acv_theme_t t) {
    switch (t) {
        case THEME_TUF_ICE: return "TUF Ice (Default)";
        case THEME_TACTICAL: return "TUF Tactical Amber";
        case THEME_EMERALD: return "Cyber Emerald";
        case THEME_CRIMSON: return "Reze Crimson";
        case THEME_STEALTH: return "Stealth Monochrome";
        case THEME_BACKLIGHT_SYNC: return "Backlight Live Sync";
        default: return "Custom";
    }
}

theme_palette_t hw_get_palette(const hardware_state_t *hw) {
    theme_palette_t pal;
    switch (hw->theme) {
        case THEME_TACTICAL:
            pal.fg_accent   = 0xFF8800;
            pal.fg_secondary= 0xFF5500;
            pal.bg_primary  = 0x0C0805;
            pal.bg_card     = 0x1E140A;
            pal.border_dim  = 0x442810;
            pal.text_norm   = 0xF0E0D0;
            pal.text_muted  = 0x907860;
            break;
        case THEME_EMERALD:
            pal.fg_accent   = 0x00FF66;
            pal.fg_secondary= 0x00CC44;
            pal.bg_primary  = 0x040E06;
            pal.bg_card     = 0x0C1E10;
            pal.border_dim  = 0x164420;
            pal.text_norm   = 0xD0F8D8;
            pal.text_muted  = 0x60A070;
            break;
        case THEME_CRIMSON:
            pal.fg_accent   = 0xFF3366;
            pal.fg_secondary= 0xFF6688;
            pal.bg_primary  = 0x10050A;
            pal.bg_card     = 0x220C16;
            pal.border_dim  = 0x481628;
            pal.text_norm   = 0xFFE0E8;
            pal.text_muted  = 0xA06078;
            break;
        case THEME_STEALTH:
            pal.fg_accent   = 0xD0D8E0;
            pal.fg_secondary= 0x8090A0;
            pal.bg_primary  = 0x080808;
            pal.bg_card     = 0x14161A;
            pal.border_dim  = 0x2C3038;
            pal.text_norm   = 0xE0E4E8;
            pal.text_muted  = 0x788088;
            break;
        case THEME_BACKLIGHT_SYNC: {
            uint32_t rgb = hw->custom_rgb;
            if (rgb == 0) {
                char hex[8] = {0};
                hw_read_backlight_hex(hex, &rgb);
            }
            if (rgb == 0) rgb = 0x00E5FF;

            int r = (rgb >> 16) & 0xFF;
            int g = (rgb >> 8) & 0xFF;
            int b = rgb & 0xFF;

            // Ensure readable luminance: if too dark, boost it
            int lum = (r * 299 + g * 587 + b * 114) / 1000;
            if (lum < 90) {
                r += 100; if (r > 255) r = 255;
                g += 100; if (g > 255) g = 255;
                b += 100; if (b > 255) b = 255;
                rgb = (r << 16) | (g << 8) | b;
            }

            pal.fg_accent   = rgb;
            pal.fg_secondary= rgb;
            pal.bg_primary  = ((r / 20) << 16) | ((g / 20) << 8) | (b / 20);
            pal.bg_card     = ((r / 10) << 16) | ((g / 10) << 8) | (b / 10);
            pal.border_dim  = ((r / 4)  << 16) | ((g / 4)  << 8) | (b / 4);
            pal.text_norm   = 0xFFFFFF;
            pal.text_muted  = 0xA0B0C0;
            break;
        }
        case THEME_TUF_ICE:
        default:
            pal.fg_accent   = 0x00E5FF;
            pal.fg_secondary= 0x00C5E6;
            pal.bg_primary  = 0x050A0E;
            pal.bg_card     = 0x0D1924;
            pal.border_dim  = 0x153040;
            pal.text_norm   = 0xC5E6F2;
            pal.text_muted  = 0x4D7A94;
            break;
    }
    return pal;
}

int hw_read_backlight_hex(char out_hex[8], uint32_t *out_rgb) {
    // 1. Try /tmp/waybar-kb-hex
    FILE *f = fopen("/tmp/waybar-kb-hex", "r");
    if (f) {
        char buf[32] = {0};
        if (fgets(buf, sizeof(buf), f)) {
            char *nl = strchr(buf, '\n');
            if (nl) *nl = '\0';
            if (strlen(buf) >= 6) {
                unsigned int r=0, g=0, b=0;
                if (sscanf(buf, "%02x%02x%02x", &r, &g, &b) == 3 && (r || g || b)) {
                    fclose(f);
                    if (out_hex) snprintf(out_hex, 8, "%02x%02x%02x", r, g, b);
                    if (out_rgb) *out_rgb = (r << 16) | (g << 8) | b;
                    return 0;
                }
            }
        }
        fclose(f);
    }

    // 2. Try /etc/asusd/aura_tuf.ron
    FILE *fron = fopen("/etc/asusd/aura_tuf.ron", "r");
    if (fron) {
        char line[256];
        bool in_colour1 = false;
        int r = -1, g = -1, b = -1;
        while (fgets(line, sizeof(line), fron)) {
            if (strstr(line, "colour1:")) in_colour1 = true;
            if (in_colour1) {
                if (strstr(line, "r:")) sscanf(strstr(line, "r:") + 2, "%d", &r);
                if (strstr(line, "g:")) sscanf(strstr(line, "g:") + 2, "%d", &g);
                if (strstr(line, "b:")) sscanf(strstr(line, "b:") + 2, "%d", &b);
                if (r >= 0 && g >= 0 && b >= 0) break;
            }
        }
        fclose(fron);
        if (r >= 0 && g >= 0 && b >= 0 && (r || g || b)) {
            if (out_hex) snprintf(out_hex, 8, "%02x%02x%02x", r, g, b);
            if (out_rgb) *out_rgb = (r << 16) | (g << 8) | b;
            return 0;
        }
    }

    // Default fallback
    if (out_hex) snprintf(out_hex, 8, "00e5ff");
    if (out_rgb) *out_rgb = 0x00E5FF;
    return 0;
}
