#ifndef CTRON_HW_H
#define CTRON_HW_H

#include <stddef.h>
#include <stdbool.h>

/* hard cap for per-core arrays (largest realistic online CPU count) */
#define HW_CPU_MAX 256
/* counted network interfaces (physical + wifi; a laptop has 2-3) */
#define HW_NET_IF_MAX 4

/* parse "0-31" / "0-15,32-47" style cpu lists; pure, unit-tested */
int hw_cpu_list_parse(const char *s, int *ids, int max);

/* collapse the board name some ASUS firmware repeats in DMI
 * product_name ("… FA608PP_FA608PP"); pure, unit-tested */
void hw_sanitize_model(const char *in, char *out, size_t n);

/* telemetry source parsers — pure, unit-tested. */
int hw_parse_meminfo_kb(const char *text, const char *key, long *out_kb);
int hw_parse_gpu_csv(const char *line, int *temp, int *mw, int *mhz,
                     int *util, int *vram_used, int *vram_total,
                     char *pstate, size_t pstate_n, unsigned *reasons);
int hw_parse_diskstats(const char *text, long long *rd_sectors,
                       long long *wr_sectors);
/* amdgpu pp_dpm_sclk lines ("0: 400Mhz\n1: 2000Mhz *\n..."): level
 * count, the "*" active level (-1 when none is marked) and its MHz */
int hw_parse_pp_dpm(const char *text, int *count, int *active,
                    int *active_mhz);
/* gpu_metrics blob (Dragon Range layout, signature-gated): the gfx
 * power field in mW, -1 when the blob is a different format. The CPU
 * blocks of this table are permanently 0xFF on this silicon — there
 * is no CPU power to read here (verified live 2026-10-27). */

/* which /sys/class/net interfaces count as the machine's own network:
 * loopback and container/VM/VPN plumbing are excluded so traffic is
 * never double-counted (same spirit as the diskstats whole-disk rule) */
bool hw_net_if_whole(const char *name);

/* /proc/net/wireless row for `ifname`: link quality (raw units, the
 * mac80211 scale is 0-70) and the level in dBm when the driver reports
 * dBm (negative; drivers with a positive raw scale leave it 0 — we
 * cannot interpret those honestly). 0 when the link parsed, -1 when
 * the interface has no row. */
int hw_parse_wireless(const char *text, const char *ifname,
                      int *link, int *level_dbm);

/* /proc/<pid>/mounts row classifier: real disk mounts only (source
 * under /dev/, loop/zram devices and squashfs excluded). */
bool hw_mount_whole(const char *dev, const char *fs);

/* one whole-disk mount row */
typedef struct {
    char dev[24];
    char point[72];
    char fs[8];
} hw_mount_t;
#define HW_MOUNT_MAX 6

/* Parse whole /proc/<pid>/mounts text into whole-disk mounts. One row
 * per device: btrfs subvolumes of the same device share its space, so
 * later mounts of a known device are skipped and counted in *dupes
 * (NULL = ignore). Octal escapes (\040 space) are decoded. Returns the
 * stored mount count. */
int hw_parse_mounts(const char *text, hw_mount_t *out, int max, int *dupes);

/* /proc/loadavg: the 1/5/15-minute averages; missing fields stay -1. */
int hw_parse_loadavg(const char *s, double *l1, double *l5, double *l15);
/* /proc/stat per-cpu jiffies (busy = user+nice+system+irq+softirq+steal,
 * total = busy+idle+iowait). Returns highest cpu id seen + 1. Pure. */
int hw_parse_cpu_stat(const char *s, long long *busy, long long *total,
                      int max_cpu);
/* /proc/pressure/{cpu,memory,io}: the "some" line's avg10 — the % of
 * the last 10 s in which at least one task was stalled on that
 * resource. -1 out on any parse failure. Pure. */
int hw_parse_psi(const char *s, double *some_avg10);
/* mean utilisation over the valid (>= 0) entries, rounded to nearest
 * %; -1 when none are valid. Pure. */
int hw_core_avg_util(const int *util, int n);
/* per-CCD mean utilisation (%, -1 unknown) and peak clock (MHz, -1 unknown)
 * over physical cores belonging to `ccd_idx`. Returns matching core count. Pure. */
int hw_ccd_stats(const int *core_ccd, const int *core_util,
                 const int *core_mhz, int core_n, int ccd_idx,
                 int *avg_util_out, int *peak_mhz_out);
/* parse k10temp Tccd label ("Tccd1".."Tccd8") -> 1..8, else -1. Pure. */
int hw_parse_tccd_label(const char *label);
/* Parsed subset of ryzen_smu PM table (version-gated, honest -1 when unknown). */
typedef struct {
    int pkg_mw;             /* SOCKET_POWER, mW (-1 unknown) */
    int cores_mw;           /* VDDCR_VDD (CPU cores rail), mW (-1 unknown) */
    int soc_mw;             /* VDDCR_SOC (SoC / I/O die / iGPU rail), mW (-1) */
    int core_temp_mc[16];   /* per-core temperature, millidegrees °C (-1) */
    int core_pwr_mw[16];    /* per-core power, mW (-1) */
    int valid_temps;
    int valid_pwrs;
} hw_smu_pm_t;

/* parse ryzen_smu PM table blob (0x00540208 = Zen 4 Dragon Range 16-core:
 * VDDCR_VDD @ 0x050, VDDCR_SOC @ 0x054, SOCKET_POWER @ 0x068,
 * CORE_POWER[16] @ 0x4a8, CORE_TEMP[16] @ 0x528). Returns 0 on supported
 * table, -1 when unsupported/truncated. Pure. */
int hw_parse_ryzen_smu_pm(unsigned ver, const unsigned char *pm,
                          size_t pm_len, hw_smu_pm_t *out);
/* convenience wrapper for per-core temperatures only; returns valid count. Pure. */
int hw_parse_ryzen_smu_core_temps(unsigned ver, const unsigned char *pm,
                                  size_t pm_len, int *out_mc, int max_cores);
/* true when `ver` is a PM table version with verified offsets */
bool hw_ryzen_smu_ver_supported(unsigned ver);

/* pstate/throttle-reason labels, bit i = 1u<<i in hw->gpu_throttle,
 * in the same order as the nvidia-smi query field list */
#define HW_GPU_REASON_N 10
extern const char *const hw_gpu_reason_names[HW_GPU_REASON_N];
/* join the labels of every set bit; "none" when reasons == 0 */
void hw_gpu_throttle_str(unsigned reasons, char *out, size_t n);

/* cached sysfs device paths: probed once, an empty entry means probe
 * on next use, a failed read drops the entry (suspend/resume can
 * renumber hwmon/power-supply indices) */
typedef struct {
    char fan_curve[256];   /* asus_custom_fan_curve hwmon base */
    char k10temp[256];
    char fan_rpm[256];     /* asus hwmon base (fan1/2_input) */
    char battery[256];
    char mains[256];
    char amdgpu[256];      /* iGPU hwmon base (temp1/freq1/power1) */
    char ram0[256], ram1[256];   /* spd5118 RAM module hwmon bases */
    char nvme0[256], nvme1[256]; /* nvme hwmon bases */
} hw_paths_t;

/* nth (0-based) hwmon whose name matches; 0 = first */
int hw_hwmon_path_nth(const char *want, int nth, char *out, size_t n);

/* 1 when a GPU temperature query is allowed. pref_on is the Settings
 * toggle. runtime_status NULL means no runtime file (always-on GPU).
 * "suspended" and "suspending" are skipped so nvidia-smi does not
 * wake a sleeping dGPU. */
int hw_gpu_temp_allowed(int pref_on, const char *runtime_status);

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

/* ---- TUF single-zone AURA keyboard (asusd) ----------------------------- */

#define AURA_MODES_MAX 12
/* asusd's mode index of the one effect whose number is proven (the
 * daemon's file names it while the bus reports 0) */
#define HW_AURA_MODE_STATIC 0
/* aura_tuf.ron names a non-static mode but carries no index */
#define HW_AURA_MODE_NONSTATIC -2

/* Pure parsers for asusd's TUF aura interface, unit-tested. */
/* `busctl get-property` LedModeData output, e.g.
 *   (uu(yyy)(yyy)ss) 0 0 0 255 255 0 0 0 "Med" "Right"
 * -> mode, zone and colour1 (0xRRGGBB). 0 on success. */
int hw_aura_dbus_parse(const char *out, int *mode, int *zone, int *rgb);
/* SupportedBasicModes output ("au 2 0 1"): the mode indices into
 * ids[0..max). Returns the count, -1 when unparseable. */
int hw_aura_modes_parse(const char *out, int *ids, int max);
/* /etc/asusd/aura_tuf.ron body: the configured mode and its colour.
 * The file stores the mode by NAME, so *mode is 0 (Static) or 1 for any
 * other named mode (its index is not in the file); -1 when unreadable. */
int hw_aura_ron_parse(const char *text, int *mode, int *rgb);
/* Mode index as ctron can honestly name it: "static" for 0, "mode N"
 * otherwise (asusd's numbers for the other effects are not documented). */
const char *hw_aura_mode_label(int mode, char *out, size_t n);

/* ---- state ----------------------------------------------------------- */

#define HZ_MAX_MODES 16

typedef struct hw_state {
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
    int cpu_temp_mc;       /* k10temp Tctl, millidegrees °C (-1 unknown) */
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
    int core_phys[HW_CPU_MAX]; /* physical core index (0..core_n-1 or topology core_id) */
    int ccd_n;                 /* L3 group count (>=1) */
    bool topo_odd;             /* weird topology -> per-cpu grid fallback */
    /* per-core NOW telemetry (fast-poll deltas, indexed like core_cpu):
     * util from /proc/stat jiffies (max of the SMT pair), live clock
     * from amd-pstate — the ceilings array above is the CAP, this is
     * what the core runs at right now. -1 = unknown (first poll, or
     * the source briefly vanished — both honest "--") */
    int core_util[HW_CPU_MAX];
    int core_mhz[HW_CPU_MAX];
    int core_temp_mc[HW_CPU_MAX]; /* per-core temp, millidegrees °C (-1 unknown),
                                     from ryzen_smu pm_table when loaded */
    int core_pwr_mw[HW_CPU_MAX];  /* per-core power, mW (-1 unknown) */
    bool has_core_temp;           /* true when at least one core_temp_mc >= 0 */
    bool has_core_pwr;            /* true when at least one core_pwr_mw >= 0 */
    int cpu_pkg_mw;               /* ryzen_smu SOCKET_POWER, mW (-1 unknown) */
    int cpu_cores_mw;             /* ryzen_smu VDDCR_VDD (CPU cores), mW (-1) */
    int cpu_soc_mw;               /* ryzen_smu VDDCR_SOC (SoC/iGPU), mW (-1) */
    unsigned ryzen_smu_pm_ver;    /* ryzen_smu pm_table_version, 0 when absent */
    int busiest_util;          /* highest thread utilisation, % (-1) */
    hw_paths_t paths;                         /* cached device paths */
    int rpm_cpu, rpm_gpu;  /* -1 unknown (0 = fan stopped, honest) */
    int bat_pct;           /* -1 unknown */
    char bat_status[16];
    bool ac_online;
    bool bat_mw_known;     /* bat_mw valid */
    int bat_mw;            /* mW, >0 discharging, <0 charging */
    int bat_limit;         /* charge_control_end_threshold, 0 unknown */
    int bat_health;        /* charge_full / design, % (-1 unknown, live-only) */
    int bat_v_mv;          /* voltage_now, mV (-1 unknown, fast poll) */
    int bat_a_ma;          /* current_now magnitude, mA (-1 unknown) */
    int pd_n;              /* USB-C PD source ports found (0..4) */
    int pd_in_mw;          /* total PD input power, mW (0 = none attached) */
    int ppt_spl, ppt_sppt, ppt_fppt;   /* W, -1 unknown/stale */
    int ppt_mode;                      /* hw_ppt_mode_t */
    bool ppt_off;                       /* limits removed (platform maxima) */
    int ppt_saved_spl, ppt_saved_sppt, ppt_saved_fppt; /* to restore */
    /* Same-boot memory for write-only PPT (Dragon Range ryzenadj exposes no
     * read-back, and hw_refresh_live resets ppt_spl/sppt/fppt to -1).
     * Persisted in settings.ini tagged with Linux boot_id so reopening ctron
     * in the same boot honestly shows what was applied as "(set)" without
     * fabricating live kernel reads in --status/--json. */
    bool ppt_set_valid;
    int ppt_set_spl, ppt_set_sppt, ppt_set_fppt;
    bool ppt_set_off;
    int nv_boost;          /* W, -1 unknown */
    int nv_temp;           /* °C, -1 unknown */

    /* telemetry extras (refresh_fast reads; -1/0 = unknown) */
    double load1;                              /* /proc/loadavg 1-min */
    double load5, load15;                      /* 5/15-min, -1 unknown */
    double psi_cpu, psi_mem, psi_io;           /* /proc/pressure "some"
                                                  avg10 stall %, -1 unknown */
    int mem_total_mb, mem_avail_mb, mem_used_mb;   /* /proc/meminfo */
    int swap_total_mb, swap_used_mb;
    char wifi_if[16];                          /* counted wifi iface, "" none */
    int wifi_link_pct;                         /* 0-100, -1 unknown */
    int wifi_level_dbm;                        /* driver level, dBm (0 unknown; valid < 0) */
    int mnt_n;                                 /* statvfs'd whole-disk mounts */
    hw_mount_t mnt[HW_MOUNT_MAX];
    int mnt_used_mb[HW_MOUNT_MAX], mnt_total_mb[HW_MOUNT_MAX];
    int mnt_avail_mb[HW_MOUNT_MAX];            /* f_bavail (df-style pct) */
    int mnt_dupes;                             /* same-device mounts skipped */
    int igpu_temp, igpu_mhz, igpu_mw;          /* amdgpu hwmon */
    int igpu_vddgfx_mv, igpu_vddnb_mv;         /* amdgpu in0/in1_input */
    int igpu_busy;                             /* drm gpu_busy_percent */
    int igpu_dpm, igpu_dpm_n, igpu_dpm_mhz;    /* active pp_dpm_sclk level */
    int cpu_temp_ccd1, cpu_temp_ccd2;          /* k10temp Tccd1/2, °C */
    int cpu_temp_ccd1_mc, cpu_temp_ccd2_mc;    /* k10temp Tccd1/2, millidegrees °C */
    int ram_temp_n, ram_temp[2];               /* spd5118 modules, °C */
    int ram_thresh_max, ram_thresh_crit;       /* spd5118 limits, °C */
    int ram_alarm;                             /* bit i = module i over max */
    int nvme_temp_n, nvme_temp[2];             /* nvme drives, °C */
    int nvme_sensor_max[2];                    /* hottest Sensor 1/2 per drive */
    long long disk_rd_sectors, disk_wr_sectors; /* raw /proc/diskstats */
    int fs_used_mb, fs_total_mb;               /* statvfs of / */
    int fs_avail_mb;                            /* f_bavail of / (df pct) */
    int gpu_mw, gpu_mhz, gpu_util;             /* extended nvidia-smi */
    int gpu_vram_used_mb, gpu_vram_total_mb;
    char gpu_pstate[8];        /* "P0".."P12", "" unknown (ext query) */
    unsigned gpu_throttle;     /* clocks_event_reasons bits, 0 = none */

    /* network (fast poll, hw_net_if_whole-counted interfaces only);
     * counters are cumulative bytes, rates live in the telem sampler */
    int net_n;                                  /* 0 = none counted */
    char net_if[HW_NET_IF_MAX][16];
    bool net_up[HW_NET_IF_MAX];                 /* operstate == "up" */
    long long net_rx[HW_NET_IF_MAX], net_tx[HW_NET_IF_MAX];
    long long net_rx_tot, net_tx_tot;           /* sums, -1 unknown */

    bool panel_od;
    bool cpu_boost;
    hw_kbd_t kbd;

    /* TUF single-zone AURA keyboard through asusd (xyz.ljones.Aura on
     * /xyz/ljones/aura/tuf), read by hw_refresh_live. While the daeboard
     * daemon is up it owns the keyboard and nothing here is read. */
    bool has_aura;                     /* asusd answered the TUF interface */
    int aura_mode;                     /* live mode index, -1 unknown */
    int aura_rgb;                      /* live colour 0xRRGGBB, -1 unknown */
    int aura_modes[AURA_MODES_MAX];    /* SupportedBasicModes indices */
    int aura_mode_n;                   /* 0 = unknown */

    /* GPU core-clock lock (nvidia-smi -lgc), session-tracked: the
     * driver exposes no way to read the lock back, so this is what
     * ctron itself set. 0 = driver default. gpu_clock_from_mem is true
     * when restored from settings.ini via a matching boot_id.
     * gpu_mhz_max = hardware maximum, read once at init. */
    int gpu_clock_lock;
    bool gpu_clock_from_mem;
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
    /* session latch: the hwmon sysfs path proved unwritable (no
     * sudoers rule) — ctrl_fan_write skips its doomed tee attempts */
    bool fan_sysfs_dead;
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

/* Unconditional live read of the fan curve hwmon into cpu/gpu (either may be NULL).
 * Returns 0 on success, -1 if no fan curve hwmon path exists. */
int hw_fan_hwmon_read_live(hw_state_t *hw, fan_curve_t *cpu, fan_curve_t *gpu);

/* Read the keyboard LED mode + colour straight from asusd (no daeboard
 * check, no hw update): the write layer's read-back. 0 on success. */
int hw_aura_read_live(int *mode, int *rgb);
/* Refresh hw->aura_*: asusd first, /etc/asusd/aura_tuf.ron as the
 * fallback when the daemon is down (that file may lag a fresh write). */
void hw_aura_read(hw_state_t *hw);
/* True when asusd reports exactly one effect and it is static. */
bool hw_aura_static_only(const hw_state_t *hw);
/* "static only" | "static + N" | "--" for status/doctor/the TUI. */
const char *hw_aura_effects_label(const hw_state_t *hw, char *out, size_t n);

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
/* cumulative bytes: "30.2 GB" / "512 MB" / "--" unknown. */
void hw_fmt_bytes(char *out, size_t n, long long bytes);

#endif /* CTRON_HW_H */
