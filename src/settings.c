#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>

static int mkdir_p(const char *path)
{
    char buf[512];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(buf))
        return -1;
    memcpy(buf, path, n + 1);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(buf, 0755);
            *p = '/';
        }
    }
    return mkdir(buf, 0755);
}

void settings_dir(char *out, size_t maxlen) {
    const char *env = getenv("CTRON_CONFIG");
    if (env && env[0]) {
        snprintf(out, maxlen, "%s", env);
        return;
    }
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) {
        snprintf(out, maxlen, "%s/ctron", xdg);
        return;
    }
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(out, maxlen, "%s/.config/ctron", home);
}

static void get_config_file(char *out, size_t maxlen) {
    char dir[512];
    settings_dir(dir, sizeof(dir));
    snprintf(out, maxlen, "%s/settings.ini", dir);
}

int settings_present(void) {
    char path[512], dir[512];
    settings_dir(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/config.ini", dir);
    if (access(path, F_OK) == 0)
        return 1;
    get_config_file(path, sizeof(path));
    return access(path, F_OK) == 0;
}

static char* trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == 0) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

int settings_init(void) {
    char dir[512], prof[512];
    settings_dir(dir, sizeof(dir));
    mkdir_p(dir);
    snprintf(prof, sizeof(prof), "%s/profiles", dir);
    mkdir_p(prof);
    return 0;
}

int settings_write_stub(void) {
    settings_init();
    char dir[512], path[512];
    settings_dir(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/config.ini", dir);
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fprintf(f, "# ctron\n");
    fprintf(f, "platform = asus_tuf\n");
    fprintf(f, "config_dir = %s\n", dir);
    fclose(f);
    return 0;
}

int settings_load(hardware_state_t *hw) {
    char path[512];
    get_config_file(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char cpu_t[128] = {0}, cpu_p[128] = {0};
    char gpu_t[128] = {0}, gpu_p[128] = {0};
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(line);
        char *val = trim(eq + 1);

        if (strcmp(key, "profile") == 0) {
            if (strcasecmp(val, "Quiet") == 0) hw->active_profile = PROF_QUIET;
            else if (strcasecmp(val, "Balanced") == 0) hw->active_profile = PROF_BALANCED;
            else if (strcasecmp(val, "Performance") == 0) hw->active_profile = PROF_PERFORMANCE;
        } else if (strcmp(key, "epp") == 0) {
            if (strcasecmp(val, "power") == 0) hw->active_epp = EPP_POWER;
            else if (strcasecmp(val, "balance_power") == 0) hw->active_epp = EPP_BALANCED_POWER;
            else if (strcasecmp(val, "balance_performance") == 0) hw->active_epp = EPP_BALANCED_PERF;
            else if (strcasecmp(val, "performance") == 0) hw->active_epp = EPP_PERFORMANCE;
        } else if (strcmp(key, "battery_limit") == 0) {
            int lim = atoi(val);
            if (lim >= 20 && lim <= 100) hw->battery_charge_limit = lim;
        } else if (strcmp(key, "display_hz") == 0) {
            int hz = atoi(val);
            if (hz >= 50 && hz <= 360) hw->display_cur_hz = hz;
        } else if (strcmp(key, "kbd_brightness") == 0) {
            if (strcasecmp(val, "off") == 0) hw->kbd_brightness = KBD_OFF;
            else if (strcasecmp(val, "low") == 0) hw->kbd_brightness = KBD_LOW;
            else if (strcasecmp(val, "med") == 0) hw->kbd_brightness = KBD_MED;
            else if (strcasecmp(val, "high") == 0) hw->kbd_brightness = KBD_HIGH;
        } else if (strcmp(key, "aura_effect") == 0) {
            int idx = atoi(val);
            if (idx >= 0 && idx < AURA_EFFECT_COUNT) hw->aura_effect_idx = idx;
        } else if (strcmp(key, "aura_color") == 0) {
            int idx = atoi(val);
            if (idx >= 0 && idx < AURA_COLOR_COUNT) hw->aura_color_idx = idx;
        } else if (strcmp(key, "cpu_target_max_mhz") == 0) {
            int freq = atoi(val);
            if (freq > 500 && freq <= 7000) hw->cpu_target_max_mhz = freq;
        } else if (strcmp(key, "cpu_temp_cap_c") == 0) {
            int t = atoi(val);
            if (t >= 70 && t <= 105) hw->cpu_temp_cap_c = t;
        } else if (strcmp(key, "cpu_temp_cap_on") == 0) {
            hw->cpu_temp_cap_on = (atoi(val) != 0);
        } else if (strcmp(key, "fan_cpu_on") == 0) {
            hw->fan_cpu_on = (atoi(val) != 0);
        } else if (strcmp(key, "fan_gpu_on") == 0) {
            hw->fan_gpu_on = (atoi(val) != 0);
        } else if (strcmp(key, "fan_cpu_t") == 0) {
            snprintf(cpu_t, sizeof(cpu_t), "%s", val);
        } else if (strcmp(key, "fan_cpu_p") == 0) {
            snprintf(cpu_p, sizeof(cpu_p), "%s", val);
        } else if (strcmp(key, "fan_gpu_t") == 0) {
            snprintf(gpu_t, sizeof(gpu_t), "%s", val);
        } else if (strcmp(key, "fan_gpu_p") == 0) {
            snprintf(gpu_p, sizeof(gpu_p), "%s", val);
        } else if (strcmp(key, "theme") == 0) {
            int t = atoi(val);
            if (t >= 0 && t < THEME_COUNT) hw->theme = (acv_theme_t)t;
        } else if (strcmp(key, "transparency") == 0) {
            int tr = atoi(val);
            if (tr >= 20 && tr <= 100) hw->transparency_pct = tr;
        } else if (strcmp(key, "tint_level") == 0) {
            int tl = atoi(val);
            if (tl >= 0 && tl <= 1) hw->tint_level = tl;
            else if (tl > 1) hw->tint_level = 1;
        } else if (strcmp(key, "sync_backlight") == 0) {
            hw->sync_with_backlight = (atoi(val) != 0);
        } else if (strcmp(key, "sync_waybar") == 0) {
            hw->sync_with_waybar = (atoi(val) != 0);
        } else if (strcmp(key, "picker_hue") == 0) {
            hw->picker_hue = atoi(val) % 360;
        } else if (strcmp(key, "picker_sat") == 0) {
            hw->picker_sat = atoi(val);
        } else if (strcmp(key, "picker_val") == 0) {
            hw->picker_val = atoi(val);
        } else if (strcmp(key, "custom_hex") == 0) {
            snprintf(hw->custom_hex, sizeof(hw->custom_hex), "%s", val);
        }
    }

    fclose(f);
    /* Mode A: restore editor points only. Missing keys keep hwmon. */
    if (cpu_t[0] && cpu_p[0])
        hw_fan_from_csv(&hw->fan_cpu, cpu_t, cpu_p);
    if (gpu_t[0] && gpu_p[0])
        hw_fan_from_csv(&hw->fan_gpu, gpu_t, gpu_p);
    return 0;
}

int settings_save(const hardware_state_t *hw) {
    settings_init();
    char path[512];
    get_config_file(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f, "# Ctron Configuration (Arcioth & Kerempkl)\n");
    fprintf(f, "profile = %s\n", hw_profile_name(hw->active_profile));
    fprintf(f, "epp = %s\n", hw_epp_name(hw->active_epp));
    fprintf(f, "battery_limit = %d\n", hw->battery_charge_limit);
    fprintf(f, "display_hz = %d\n", hw->display_cur_hz);
    fprintf(f, "kbd_brightness = %s\n", hw_kbd_name(hw->kbd_brightness));
    fprintf(f, "aura_effect = %d\n", hw->aura_effect_idx);
    fprintf(f, "aura_color = %d\n", hw->aura_color_idx);
    fprintf(f, "cpu_target_max_mhz = %d\n", hw->cpu_target_max_mhz);
    fprintf(f, "cpu_temp_cap_c = %d\n", hw->cpu_temp_cap_c);
    fprintf(f, "cpu_temp_cap_on = %d\n", hw->cpu_temp_cap_on ? 1 : 0);
    fprintf(f, "fan_cpu_on = %d\n", hw->fan_cpu_on ? 1 : 0);
    fprintf(f, "fan_gpu_on = %d\n", hw->fan_gpu_on ? 1 : 0);
    {
        char t[128], p[128];
        hw_fan_to_csv(&hw->fan_cpu, t, sizeof(t), p, sizeof(p));
        fprintf(f, "fan_cpu_n = %d\n", hw->fan_cpu.n);
        fprintf(f, "fan_cpu_t = %s\n", t);
        fprintf(f, "fan_cpu_p = %s\n", p);
        hw_fan_to_csv(&hw->fan_gpu, t, sizeof(t), p, sizeof(p));
        fprintf(f, "fan_gpu_n = %d\n", hw->fan_gpu.n);
        fprintf(f, "fan_gpu_t = %s\n", t);
        fprintf(f, "fan_gpu_p = %s\n", p);
    }
    fprintf(f, "theme = %d\n", (int)hw->theme);
    fprintf(f, "transparency = %d\n", hw->transparency_pct);
    fprintf(f, "tint_level = %d\n", hw->tint_level);
    fprintf(f, "sync_backlight = %d\n", hw->sync_with_backlight ? 1 : 0);
    fprintf(f, "sync_waybar = %d\n", hw->sync_with_waybar ? 1 : 0);
    fprintf(f, "picker_hue = %d\n", hw->picker_hue);
    fprintf(f, "picker_sat = %d\n", hw->picker_sat);
    fprintf(f, "picker_val = %d\n", hw->picker_val);
    fprintf(f, "custom_hex = %s\n", hw->custom_hex);

    fclose(f);
    return 0;
}
