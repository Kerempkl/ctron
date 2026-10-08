#ifndef CTRON_HW_H
#define CTRON_HW_H

/* hard cap for per-core arrays (largest realistic online CPU count) */
#define HW_CPU_MAX 256

/* parse "0-31" / "0-15,32-47" style cpu lists; pure, unit-tested */
int hw_cpu_list_parse(const char *s, int *ids, int max);

/* 1 when a GPU temperature query is allowed. pref_on is the Settings
 * toggle. runtime_status NULL means no runtime file (always-on GPU).
 * "suspended" and "suspending" are skipped so nvidia-smi does not
 * wake a sleeping dGPU. */
int hw_gpu_temp_allowed(int pref_on, const char *runtime_status);

/* cached sysfs device paths: probed once, an empty entry means probe
 * on next use, a failed read drops the entry (suspend/resume can
 * renumber hwmon/power-supply indices) */
typedef struct {
    char fan_curve[256];   /* asus_custom_fan_curve hwmon base */
    char k10temp[256];
    char fan_rpm[256];     /* asus hwmon base (fan1/2_input) */
    char battery[256];
    char mains[256];
} hw_paths_t;

#include <stdbool.h>
#include <stddef.h>

#include "fan.h"

/* PPT write interface, resolved once at init (probe order):
 *   ARMOURY  asus-armoury firmware-attributes current_value (official)
 *   RYZENADJ sudo -n ryzenadj SMU mailbox (no read-back on Dragon Range)
 *   LEGACY   asus-nb-wmi sysfs (accepted but ignored on FA608PP) */
typedef enum {
    HW_PPT_LEGACY = 0,
    HW_PPT_ARMOURY,
    HW_PPT_RYZENADJ,
} hw_ppt_mode_t;

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
    int cpu_mhz_limit;     /* aggregate scaling_max (max across cores) */
    int cpu_n;                                /* present cpu count */
    int cpu_ids[HW_CPU_MAX];                  /* present cpu ids, kernel numbering */
    int cpu_mhz_core[HW_CPU_MAX];             /* per-core scaling_max, MHz,
                                                 indexed by real cpu id */
    /* physical topology, built once at init (never changes at runtime):
     * cores from thread_siblings_list, CCDs from the L3 shared_cpu_list */
    int core_n;                /* physical cores */
    int core_cpu[HW_CPU_MAX];  /* first thread of each core */
    int core_sib[HW_CPU_MAX];  /* second thread, -1 = SMT off/none */
    int core_ccd[HW_CPU_MAX];  /* L3 group index of each core */
    int ccd_n;                 /* L3 group count (>=1) */
    bool topo_odd;             /* weird topology -> per-cpu grid fallback */
    hw_paths_t paths;                         /* cached device paths */
    int rpm_cpu, rpm_gpu;  /* -1 unknown */
    int bat_pct;
    char bat_status[16];
    bool ac_online;
    bool bat_mw_known;     /* bat_mw valid */
    int bat_mw;            /* mW, >0 discharging, <0 charging */
    int bat_limit;         /* charge_control_end_threshold, 0 unknown */
    int ppt_spl, ppt_sppt, ppt_fppt;   /* W, 0 unknown/stale */
    int ppt_mode;                      /* hw_ppt_mode_t */
    bool ppt_off;                       /* limits removed (platform maxima) */
    int ppt_saved_spl, ppt_saved_sppt, ppt_saved_fppt; /* to restore */
    int nv_boost;          /* W, 0 unknown */
    int nv_temp;           /* °C, 0 unknown */
    bool panel_od;
    bool cpu_boost;
    hw_kbd_t kbd;

    /* GPU core-clock lock (nvidia-smi -lgc), session-tracked: the
     * driver exposes no way to read the lock back, so this is what
     * ctron itself set. 0 = driver default. gpu_mhz_max = hardware
     * maximum, read once at init. */
    int gpu_clock_lock;
    int gpu_mhz_max;

    /* asusd power-source profile takeover (from /etc/asusd/asusd.ron):
     * -2 unknown (asusd absent), -1 auto-switching off, else the
     * hw_profile_t asusd enforces on that power source */
    int asusd_ac;
    int asusd_bat;

    /* fan curves (editor copies; hwmon stock snapshot kept) */
    fan_curve_t fan_cpu, fan_gpu;
    fan_curve_t fan_cpu_stock, fan_gpu_stock;
    bool fan_cpu_on, fan_gpu_on;
    /* true while the in-memory curves hold edits newer than the hwmon
     * table (fan editor, mode/profile steps, ini load): a live refresh
     * must not overwrite them. Cleared by ctrl_fan_write on success. */
    bool fan_staged;

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

/* asusd power-source takeover, daemon-live readers (name-based):
 * flag 1/0/-2 unreadable; profile hw_profile_t or -2 */
int hw_asusd_auto_flag(const char *prop);
int hw_asusd_auto_profile(int ac);

/* Refresh asusd_ac/asusd_bat: daemon state first (asusd.ron flushes
 * asynchronously), file only as a fallback. */
void hw_asusd_auto_read(hw_state_t *hw);

/* Pure: the profile asusd enforces on the CURRENT power source, or -1
 * when its takeover is off/unknown there. A conflict (enforced >= 0
 * and != hw->profile) means a manual profile choice gets reverted on
 * the next power event. */
int hw_asusd_enforced(const hw_state_t *hw);

/* Pure: parse the asusd.ron body for the power-source profile takeover.
 * *ac and *bat: -1 auto-switching off, 0..2 the hw_profile_t, -2 unknown
 * (change flag on but the profile name unreadable). */
void hw_asusd_ron_parse(const char *buf, int *ac, int *bat);

/* fan-curve hwmon base through the path cache (the write layer uses
 * it so reads and writes can never disagree) */
int hw_path_fan_curve(hw_state_t *hw, char *out, size_t n);

/* true when `id` is a present cpu (kernel numbering, hw->cpu_ids) */
bool hw_cpu_present(const hw_state_t *hw, int id);

/* Pure physical-topology grouping over sysfs-style list strings.
 * ids[0..n) are the present cpus; sib[i]/l3[i] are that cpu's
 * thread_siblings_list / L3 shared_cpu_list (NULL or "" = unknown).
 * Fills core_cpu/core_sib/core_ccd and *ccd_n_out; sets *odd_out when a
 * core would have more than two present threads (caller falls back to a
 * per-cpu grid). Returns core_n (0 with *odd_out on failure). */
int hw_topology_group(const int *ids, const char *const *sib,
                      const char *const *l3, int n,
                      int *core_cpu, int *core_sib, int *core_ccd,
                      int *ccd_n_out, bool *odd_out);

/* Read topology sysfs (once, at init) into the hw->core_* fields. */
void hw_topology_build(hw_state_t *hw);

/* asus-armoury firmware attribute raw read ("attr/current_value"). */
int hw_armoury_read(const char *attr, char *out, size_t n);

/* hwmon path lookup by name (e.g. "k10temp"). 0 on success. */
int hw_hwmon_path(const char *name, char *out, size_t n);

/* "dis 31.6W", "chg 12.0W", "0.0W", or "--" when unknown. */
void hw_fmt_power(char *out, size_t n, bool known, int mw);

#endif /* CTRON_HW_H */
