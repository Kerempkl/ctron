#ifndef TUF_HARDWARE_H
#define TUF_HARDWARE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define MAX_LOG_ENTRIES 16
#define MAX_LOG_LEN 128
#define FAN_POINTS 8

typedef struct {
    int n; /* 1–FAN_POINTS in use */
    int temp_c[FAN_POINTS];
    int pwm[FAN_POINTS]; /* 0–255 */
} fan_curve_t;

typedef enum {
    PROF_QUIET = 0,
    PROF_BALANCED,
    PROF_PERFORMANCE,
    PROF_COUNT
} asus_profile_t;

typedef enum {
    EPP_POWER = 0,
    EPP_BALANCED_POWER,
    EPP_BALANCED_PERF,
    EPP_PERFORMANCE,
    EPP_COUNT
} epp_mode_t;

typedef enum {
    KBD_OFF = 0,
    KBD_LOW,
    KBD_MED,
    KBD_HIGH,
    KBD_COUNT
} kbd_bright_t;

typedef enum {
    THEME_TUF_ICE = 0,
    THEME_TACTICAL,
    THEME_EMERALD,
    THEME_CRIMSON,
    THEME_STEALTH,
    THEME_BACKLIGHT_SYNC,
    THEME_COUNT
} acv_theme_t;

typedef struct {
    uint32_t fg_accent;
    uint32_t fg_secondary;
    uint32_t bg_primary;
    uint32_t bg_card;
    uint32_t border_dim;
    uint32_t text_norm;
    uint32_t text_muted;
} theme_palette_t;

typedef struct {
    char laptop_model[64];
    char cpu_model[64];
    bool is_asus;

    // Profiles & CPU
    asus_profile_t active_profile;
    epp_mode_t active_epp;
    int cpu_cur_freq_mhz;
    int cpu_min_freq_mhz;
    int cpu_max_freq_mhz;
    int cpu_target_max_mhz;
    int cpu_temp_c;
    int cpu_temp_cap_c;       /* user Tctl cap, 70–105 */
    bool cpu_temp_cap_on;
    int cpu_applied_max_mhz;  /* live scaling_max (may be below target while throttling) */
    bool has_ryzenadj;

    // Display
    char display_name[32];
    char display_res[32];
    int display_cur_hz;
    int display_min_hz;
    int display_max_hz;

    // Battery
    int battery_percent;
    bool battery_ac_connected;
    char battery_status[32];
    int battery_charge_limit;

    /* asus_custom_fan_curve / asusctl fan-curve */
    fan_curve_t fan_cpu;
    fan_curve_t fan_gpu;
    fan_curve_t fan_cpu_stock;
    fan_curve_t fan_gpu_stock;
    bool fan_cpu_on;
    bool fan_gpu_on;
    bool has_fan_curve;

    // Keyboard & Aura
    kbd_bright_t kbd_brightness;
    int aura_effect_idx;
    int aura_color_idx;

    // ACV Preferences & Theme
    acv_theme_t theme;
    int transparency_pct;
    int tint_level; // 0=Clear Glass, 1=Solid Opaque
    bool sync_with_backlight;
    bool sync_with_waybar;

    // Color Entry & Profiles
    char hex_input[8];
    char profile_name_input[4];
    int active_input_field; // 0 none, 1 hex, 2 profile tag, 3 fan X°C, 4 fan Y pwm

    // RGB Color
    int picker_hue;
    int picker_sat;
    int picker_val;
    char custom_hex[8];
    uint32_t custom_rgb;

    // Has asusctl daemon & asus-armoury driver
    bool has_asusctl;
    bool has_asus_armoury;

    /* asus-nb-wmi tunables (watts). sysfs 5 = stale kernel cache, not live EC. */
    int ppt_spl;
    int ppt_sppt;
    int ppt_fppt;
    int nv_boost_w;
    int nv_temp_target;
    bool panel_od;
    bool cpu_boost;
} hardware_state_t;

int hw_read_backlight_hex(char out_hex[8], uint32_t *out_rgb);

// Aura constants
extern const char* const AURA_EFFECT_NAMES[];
extern const int AURA_EFFECT_COUNT;

extern const char* const AURA_COLOR_NAMES[];
extern const char* const AURA_COLOR_HEX[];
extern const uint32_t AURA_COLOR_RGB[];
extern const int AURA_COLOR_COUNT;

// Core functions
int hw_init(hardware_state_t *hw);
int hw_poll_telemetry(hardware_state_t *hw);
int hw_refresh_live(hardware_state_t *hw);

// asus-armoury driver interface
int hw_armoury_get(const char *attr, char *out, size_t maxlen);
int hw_armoury_set(const char *attr, const char *val);

int hw_set_profile(hardware_state_t *hw, asus_profile_t profile);
int hw_set_epp(hardware_state_t *hw, epp_mode_t epp);
int hw_set_cpu_max_freq(hardware_state_t *hw, int mhz);
int hw_set_temp_cap(hardware_state_t *hw, int celsius);
int hw_set_temp_cap_enabled(hardware_state_t *hw, bool on);
int hw_set_display_hz(hardware_state_t *hw, int hz);
int hw_set_battery_limit(hardware_state_t *hw, int limit);
int hw_fan_read(hardware_state_t *hw);
int hw_fan_apply(hardware_state_t *hw);
int hw_fan_from_csv(fan_curve_t *fc, const char *temps, const char *pwms);
void hw_fan_to_csv(const fan_curve_t *fc, char *temps, size_t tn, char *pwms, size_t pn);
int hw_fan_preset(hardware_state_t *hw, int preset); /* 0 stock 1 silent 2 cool 3 full */
int hw_fan_enable(hardware_state_t *hw, bool cpu_on, bool gpu_on);
int hw_fan_nudge_point(hardware_state_t *hw, int gpu, int idx, int dtemp, int dpwm);
int hw_fan_set_abs(hardware_state_t *hw, int gpu, int idx, int temp, int pwm);
int hw_fan_add_point(hardware_state_t *hw, int gpu, int temp, int pwm);
int hw_fan_del_point(hardware_state_t *hw, int gpu, int idx);
int hw_set_ppt(hardware_state_t *hw, int spl, int sppt, int fppt);
int hw_set_nv_boost(hardware_state_t *hw, int watts);
int hw_set_nv_temp(hardware_state_t *hw, int celsius);
int hw_set_panel_od(hardware_state_t *hw, bool on);
int hw_set_cpu_boost(hardware_state_t *hw, bool on);
int hw_battery_oneshot(hardware_state_t *hw);
void hw_ppt_limits(const hardware_state_t *hw, int *spl_min, int *spl_max,
                   int *sppt_min, int *sppt_max, int *fppt_min, int *fppt_max);
int hw_set_kbd_brightness(hardware_state_t *hw, kbd_bright_t lvl);
int hw_set_aura(hardware_state_t *hw, int effect_idx, int color_idx);
int hw_set_aura_hex(hardware_state_t *hw, const char *hex);

void hw_calc_hsv_rgb(int h, int s, int v, uint32_t *out_rgb, char out_hex[8]);
void hw_apply_transparency(int pct);
void hw_sync_waybar_color(const char *hex);
void hw_waybar_sync_enable(bool on);

const char* hw_theme_name(acv_theme_t t);
theme_palette_t hw_get_palette(const hardware_state_t *hw);
const char* hw_profile_name(asus_profile_t p);
const char* hw_epp_name(epp_mode_t e);
const char* hw_kbd_name(kbd_bright_t k);

// Logging
void log_add(const char *fmt, ...);
const char* log_get(int idx);
int log_count(void);

#endif // TUF_HARDWARE_H
