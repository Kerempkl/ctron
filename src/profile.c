#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "profile.h"
#include "hardware.h"
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <glob.h>
#include <time.h>
#include <sys/stat.h>

static void get_profiles_dir(char *out, size_t maxlen) {
    const char *home = getenv("HOME");
    if (!home) home = "/home/arcioth";
    snprintf(out, maxlen, "%s/.config/vhelper/profiles", home);
    mkdir(out, 0755);
}

void profile_gen_random_name(char out[4]) {
    static const char *s_tags[] = {
        "GAM", "TUF", "ECO", "ICE", "MAX", "PWR", "SIL", "ARC",
        "VGZ", "BLZ", "NEO", "CYN", "RED", "GRN", "BLU", "ZEN",
        "BAT", "DEV", "FPS", "WAR", "HOT", "COL", "FLY", "RUN"
    };
    static bool seeded = false;
    if (!seeded) {
        srand(time(NULL) ^ getpid());
        seeded = true;
    }
    int idx = rand() % (sizeof(s_tags) / sizeof(s_tags[0]));
    snprintf(out, 4, "%s", s_tags[idx]);
}

int profile_list(char list[][4], int max_count) {
    char dir[512];
    get_profiles_dir(dir, sizeof(dir));

    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s/*.acv", dir);

    glob_t g;
    int count = 0;
    if (glob(pattern, 0, NULL, &g) == 0) {
        for (size_t i = 0; i < g.gl_pathc && count < max_count; i++) {
            const char *slash = strrchr(g.gl_pathv[i], '/');
            const char *fname = slash ? slash + 1 : g.gl_pathv[i];
            char base[32] = {0};
            strncpy(base, fname, sizeof(base) - 1);
            char *dot = strrchr(base, '.');
            if (dot) *dot = '\0';

            if (strlen(base) > 0) {
                // Keep 3 characters uppercase
                char code[4] = {0};
                for (int j = 0; j < 3 && base[j]; j++) {
                    code[j] = toupper((unsigned char)base[j]);
                }
                code[3] = '\0';
                strncpy(list[count], code, 4);
                count++;
            }
        }
        globfree(&g);
    }
    return count;
}

int profile_export(const char *name3, const hardware_state_t *hw, const acv_profile_filter_t *filter) {
    char dir[512];
    get_profiles_dir(dir, sizeof(dir));

    char code[4] = {0};
    for (int i = 0; i < 3 && name3[i]; i++) {
        code[i] = toupper((unsigned char)name3[i]);
    }
    if (strlen(code) == 0) profile_gen_random_name(code);

    char path[512];
    snprintf(path, sizeof(path), "%s/%s.acv", dir, code);

    FILE *f = fopen(path, "w");
    if (!f) return -1;

    fprintf(f, "# Ctron Profile Configuration\n");
    fprintf(f, "# Tag: %s | Device: %s\n\n", code, hw->laptop_model);
    fprintf(f, "[profile]\nname = %s\n\n", code);

    if (!filter || filter->include_perf) {
        fprintf(f, "[perf]\n");
        fprintf(f, "profile = %s\n", hw_profile_name(hw->active_profile));
        fprintf(f, "epp = %s\n", hw_epp_name(hw->active_epp));
        fprintf(f, "cpu_target_max_mhz = %d\n", hw->cpu_target_max_mhz);
        fprintf(f, "cpu_temp_cap_c = %d\n", hw->cpu_temp_cap_c);
        fprintf(f, "cpu_temp_cap_on = %d\n\n", hw->cpu_temp_cap_on ? 1 : 0);
    }

    if (!filter || filter->include_power) {
        fprintf(f, "[power]\n");
        fprintf(f, "display_hz = %d\n", hw->display_cur_hz);
        fprintf(f, "battery_limit = %d\n\n", hw->battery_charge_limit);
    }

    if (!filter || filter->include_aura) {
        fprintf(f, "[aura]\n");
        fprintf(f, "kbd_brightness = %s\n", hw_kbd_name(hw->kbd_brightness));
        fprintf(f, "aura_effect = %d\n", hw->aura_effect_idx);
        fprintf(f, "aura_color = %d\n", hw->aura_color_idx);
        fprintf(f, "custom_hex = %s\n\n", hw->custom_hex);
    }

    if (!filter || filter->include_theme) {
        fprintf(f, "[theme]\n");
        fprintf(f, "theme = %d\n", (int)hw->theme);
        fprintf(f, "tint_level = %d\n", hw->transparency_pct);
        fprintf(f, "sync_backlight = %d\n", hw->sync_with_backlight ? 1 : 0);
        fprintf(f, "sync_waybar = %d\n", hw->sync_with_waybar ? 1 : 0);
    }

    fclose(f);
    log_add("Exported profile %s.acv", code);
    return 0;
}

int profile_import(const char *name3, hardware_state_t *hw, const acv_profile_filter_t *filter) {
    char dir[512];
    get_profiles_dir(dir, sizeof(dir));

    char path[512];
    snprintf(path, sizeof(path), "%s/%s.acv", dir, name3);
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char line[256];
    char current_sec[32] = {0};

    while (fgets(line, sizeof(line), f)) {
        // Strip comments and whitespace
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *trim_line = line;
        while (isspace((unsigned char)*trim_line)) trim_line++;
        if (!*trim_line) continue;

        if (*trim_line == '[') {
            char *end = strchr(trim_line, ']');
            if (end) {
                *end = '\0';
                strncpy(current_sec, trim_line + 1, sizeof(current_sec) - 1);
            }
            continue;
        }

        char *eq = strchr(trim_line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *k = trim_line;
        char *v = eq + 1;
        while (isspace((unsigned char)*k)) k++;
        char *kend = k + strlen(k) - 1;
        while (kend > k && isspace((unsigned char)*kend)) *kend-- = '\0';

        while (isspace((unsigned char)*v)) v++;
        char *vend = v + strlen(v) - 1;
        while (vend > v && isspace((unsigned char)*vend)) *vend-- = '\0';

        // Check if section is enabled by filter
        if (strcasecmp(current_sec, "perf") == 0) {
            if (filter && !filter->include_perf) continue;
            if (strcmp(k, "profile") == 0) {
                if (strcasecmp(v, "quiet") == 0) hw_set_profile(hw, PROF_QUIET);
                else if (strcasecmp(v, "balanced") == 0) hw_set_profile(hw, PROF_BALANCED);
                else if (strcasecmp(v, "performance") == 0) hw_set_profile(hw, PROF_PERFORMANCE);
            } else if (strcmp(k, "epp") == 0) {
                if (strcasecmp(v, "power") == 0) hw_set_epp(hw, EPP_POWER);
                else if (strcasecmp(v, "balance_power") == 0) hw_set_epp(hw, EPP_BALANCED_POWER);
                else if (strcasecmp(v, "balance_performance") == 0) hw_set_epp(hw, EPP_BALANCED_PERF);
                else if (strcasecmp(v, "performance") == 0) hw_set_epp(hw, EPP_PERFORMANCE);
            } else if (strcmp(k, "cpu_target_max_mhz") == 0) {
                int f_val = atoi(v);
                if (f_val > 500 && f_val <= 6000) hw_set_cpu_max_freq(hw, f_val);
            } else if (strcmp(k, "cpu_temp_cap_c") == 0) {
                int t = atoi(v);
                if (t >= 70 && t <= 105) hw_set_temp_cap(hw, t);
            } else if (strcmp(k, "cpu_temp_cap_on") == 0) {
                hw_set_temp_cap_enabled(hw, atoi(v) != 0);
            }
        } else if (strcasecmp(current_sec, "power") == 0) {
            if (filter && !filter->include_power) continue;
            if (strcmp(k, "display_hz") == 0) {
                int hz = atoi(v);
                if (hz >= 60 && hz <= 360) hw_set_display_hz(hw, hz);
            } else if (strcmp(k, "battery_limit") == 0) {
                int bl = atoi(v);
                if (bl >= 60 && bl <= 100) hw_set_battery_limit(hw, bl);
            }
        } else if (strcasecmp(current_sec, "aura") == 0) {
            if (filter && !filter->include_aura) continue;
            if (strcmp(k, "kbd_brightness") == 0) {
                if (strcasecmp(v, "off") == 0) hw_set_kbd_brightness(hw, KBD_OFF);
                else if (strcasecmp(v, "low") == 0) hw_set_kbd_brightness(hw, KBD_LOW);
                else if (strcasecmp(v, "med") == 0) hw_set_kbd_brightness(hw, KBD_MED);
                else if (strcasecmp(v, "high") == 0) hw_set_kbd_brightness(hw, KBD_HIGH);
            } else if (strcmp(k, "aura_effect") == 0) {
                hw->aura_effect_idx = atoi(v);
            } else if (strcmp(k, "aura_color") == 0) {
                hw->aura_color_idx = atoi(v);
            } else if (strcmp(k, "custom_hex") == 0) {
                hw_set_aura_hex(hw, v);
            }
        } else if (strcasecmp(current_sec, "theme") == 0) {
            if (filter && !filter->include_theme) continue;
            if (strcmp(k, "theme") == 0) {
                int t = atoi(v);
                if (t >= 0 && t < THEME_COUNT) hw->theme = (acv_theme_t)t;
            } else if (strcmp(k, "tint_level") == 0) {
                hw->transparency_pct = atoi(v);
            } else if (strcmp(k, "sync_backlight") == 0) {
                hw->sync_with_backlight = (atoi(v) != 0);
            } else if (strcmp(k, "sync_waybar") == 0) {
                hw->sync_with_waybar = (atoi(v) != 0);
            }
        }
    }

    fclose(f);
    settings_save(hw);
    log_add("Applied profile %s.acv", name3);
    return 0;
}

int profile_delete(const char *name3) {
    char dir[512];
    get_profiles_dir(dir, sizeof(dir));
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.acv", dir, name3);
    int ret = unlink(path);
    if (ret == 0) log_add("Deleted profile %s.acv", name3);
    return ret;
}

int profile_get_summary(const char *name3, char out_perf[64], char out_pwr[64], char out_aura[64]) {
    char dir[512];
    get_profiles_dir(dir, sizeof(dir));
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.acv", dir, name3);
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    char prof[32] = "Bal", epp[32] = "bal_pwr", hz[16] = "144", bat[16] = "80", hex[16] = "00ffff";
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *k = line, *v = eq + 1;
        while (isspace((unsigned char)*k)) k++;
        char *ke = k + strlen(k) - 1; while (ke > k && isspace((unsigned char)*ke)) *ke-- = '\0';
        while (isspace((unsigned char)*v)) v++;
        char *ve = v + strlen(v) - 1; while (ve > v && isspace((unsigned char)*ve)) *ve-- = '\0';

        if (strcmp(k, "profile") == 0) strncpy(prof, v, sizeof(prof) - 1);
        else if (strcmp(k, "epp") == 0) strncpy(epp, v, sizeof(epp) - 1);
        else if (strcmp(k, "display_hz") == 0) strncpy(hz, v, sizeof(hz) - 1);
        else if (strcmp(k, "battery_limit") == 0) strncpy(bat, v, sizeof(bat) - 1);
        else if (strcmp(k, "custom_hex") == 0) strncpy(hex, v, sizeof(hex) - 1);
    }
    fclose(f);

    if (out_perf) snprintf(out_perf, 64, "%s | EPP: %s", prof, epp);
    if (out_pwr) snprintf(out_pwr, 64, "%s Hz | Bat: %s%%", hz, bat);
    if (out_aura) snprintf(out_aura, 64, "#%s", hex);
    return 0;
}
