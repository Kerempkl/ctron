#ifndef CTRON_HW_H
#define CTRON_HW_H

#include <stdbool.h>
#include <stddef.h>

#include "fan.h"

/* ---- enums with canonical names ------------------------------------- */

typedef enum { HW_QUIET = 0, HW_BALANCED, HW_PERFORMANCE, HW_PROF_COUNT } hw_profile_t;
const char *hw_profile_name(hw_profile_t p);        /* Quiet/Balanced/Performance */
int hw_profile_from_name(const char *s);            /* -1 when unknown */

typedef enum {
    HW_EPP_POWER = 0,
    HW_EPP_BAL_POWER,
    HW_EPP_BAL_PERF,
    HW_EPP_PERF,
    HW_EPP_COUNT
} hw_epp_t;
const char *hw_epp_name(hw_epp_t e);                /* sysfs string */
int hw_epp_from_name(const char *s);

typedef enum { HW_KBD_OFF = 0, HW_KBD_LOW, HW_KBD_MED, HW_KBD_HIGH, HW_KBD_COUNT } hw_kbd_t;
const char *hw_kbd_name(hw_kbd_t k);                /* off/low/med/high */
int hw_kbd_from_name(const char *s);

/* ---- state ----------------------------------------------------------- */

#define HZ_MAX_MODES 16

typedef struct {
    /* identity */
    char model[64];
    char cpu[128];
    bool is_asus;

    /* capability probes (resolved once at init) */
    bool has_asusctl;
    bool has_armoury;      /* /sys/class/firmware-attributes/asus-armoury */
    bool has_fan_curve;    /* hwmon asus_custom_fan_curve */
    bool has_fan_rpm;      /* hwmon asus (fan*_input) */
    bool has_nvidia_smi;
    bool has_kbd_led;

    /* live values */
    hw_profile_t profile;
    hw_epp_t epp;
    int cpu_temp;          /* k10temp Tctl, °C (-1 unknown) */
    int gpu_temp;          /* nvidia-smi, °C (-1 unknown) */
    int cpu_mhz_cur;
    int cpu_mhz_min;
    int cpu_mhz_max;       /* cpuinfo_max_freq */
    int cpu_mhz_limit;     /* saved scaling_max target */
    int rpm_cpu, rpm_gpu;  /* -1 unknown */
    int bat_pct;
    char bat_status[16];
    bool ac_online;
    int bat_limit;         /* charge_control_end_threshold, 0 unknown */
    int ppt_spl, ppt_sppt, ppt_fppt;   /* W, 0 unknown/stale */
    int nv_boost;          /* W, 0 unknown */
    int nv_temp;           /* °C, 0 unknown */
    bool panel_od;
    bool cpu_boost;
    hw_kbd_t kbd;

    /* fan curves (editor copies; hwmon stock snapshot kept) */
    fan_curve_t fan_cpu, fan_gpu;
    fan_curve_t fan_cpu_stock, fan_gpu_stock;
    bool fan_cpu_on, fan_gpu_on;

    /* display */
    int hz_cur;            /* 0 unknown */
    int hz_modes[HZ_MAX_MODES];
    int hz_count;
} hw_state_t;

/* Probe capabilities and prefill state. Cheap-ish; called once. */
void hw_init(hw_state_t *hw);

/* Hot-path refresh (TUI poll / --watch): temp, freq, battery, RPM.
 * GPU temp is cached and only re-queried every 2 s. */
void hw_refresh_fast(hw_state_t *hw);

/* Full snapshot for --status / --doctor / TUI entry. */
void hw_refresh_live(hw_state_t *hw);

/* asus-armoury firmware attribute raw read ("attr/current_value"). */
int hw_armoury_read(const char *attr, char *out, size_t n);

/* hwmon path lookup by name (e.g. "k10temp"). 0 on success. */
int hw_hwmon_path(const char *name, char *out, size_t n);

#endif /* CTRON_HW_H */
