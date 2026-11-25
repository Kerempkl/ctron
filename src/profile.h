#ifndef CTRON_PROFILE_H
#define CTRON_PROFILE_H

#include <stddef.h>
#include <stdio.h>

#include "hw.h"

/* Saved hardware snapshots (.ctr) with free-form names ("oyun/turbo"
 * style names group into subdirectories). Format (ini): [perf] [power]
 * [light] sections whose key=value lines are fed through the shared
 * command table on import. Omitted fields are left untouched on apply
 * (selective snapshots). */

#define MAX_PROFILES 64
#define PROFILE_NAME_MAX 48

void profile_gen_name(char *out, size_t n);

/* Public for tests: [A-Za-z0-9._-] plus '/' as a group separator
 * (collapsed/trimmed; "." and ".." segments are rejected). */
void profile_sanitize_name(const char *in, char *out, size_t n);

int profile_list(char list[][PROFILE_NAME_MAX], int max);
int profile_export(const char *name, const hw_state_t *hw);
int profile_import(const char *name, hw_state_t *hw, char *err, size_t errn);
int profile_dry_run(const char *name, const hw_state_t *hw, FILE *out,
                    char *err, size_t errn);
int profile_delete(const char *name);
int profile_summary(const char *name, char *out, size_t n);
int profile_rename(const char *old_name, const char *new_name);
int profile_set_note(const char *name, const char *note);

void profile_capture_line(const hw_state_t *hw, char *out, size_t n);

/* Parsed snapshot model used for inspection, live diff, and interactive
 * in-TUI editing. Sentinel values (-1 / 0 / "") mean "omitted from .ctr
 * (leave hardware untouched when applied)". */
typedef struct {
    char profile[16];                /* "" omitted */
    char epp[24];                    /* "" omitted */
    int cpu_boost;                   /* -1 omitted, 0 off, 1 on */
    int freq;                        /* 0 omitted, >0 all-core MHz */
    int core_n;                      /* per-core overrides count */
    int core_ids[HW_CPU_MAX];
    int core_mhz[HW_CPU_MAX];
    int ppt_off;                     /* -1 omitted, 1 limits removed */
    int ppt_spl, ppt_sppt, ppt_fppt; /* 0 omitted, >0 W */
    int gpu_clock;                   /* -1 omitted, 0 reset, >0 MHz */
    int panel_od;                    /* -1 omitted, 0 off, 1 on */
    int hz;                          /* 0 omitted, >0 Hz */
    int bat;                         /* 0 omitted, 20..100 % */
    char kbd[8];                     /* "" omitted */
    char aura[24];                   /* "static 00ffff": "" omitted */
    int asusd_ac, asusd_bat;         /* -2 omitted, -1 off, 0..2 enforced */
    int fan_cpu_on, fan_gpu_on;      /* -1 omitted, 0 off, 1 on */
    char fan_cpu_t[64], fan_cpu_p[64];
    char fan_gpu_t[64], fan_gpu_p[64];
    char saved[24];                  /* "# saved:" header */
    char device[64];                 /* "# device:" header */
    char note[64];                   /* "# note:" header */
} profile_meta_t;

void profile_meta_init(profile_meta_t *m);
void profile_meta_from_hw(const hw_state_t *hw, profile_meta_t *m);
int  profile_meta_parse(const char *name, profile_meta_t *m);
int  profile_save_meta(const char *name, const profile_meta_t *m);

/* Normalise an `aura` command value ("<effect> [name|hex]", same syntax
 * as --aura) into the snapshot form "<effect> <hex>". A colour-less
 * value keeps the command table's own default (the first preset
 * colour), so a snapshot round-trips to exactly what applying that line
 * would write. Pure. 0 on success, -1 when malformed. */
int profile_aura_normalize(const char *val, char *out, size_t n);
/* Split a stored "static 00ffff" back into effect name and hex. */
int profile_aura_split(const char *aura, char *eff, size_t effn,
                       char *hex, size_t hexn);
/* Pure: true when the stored aura value differs from what the keyboard
 * shows. An unreadable keyboard is never a diff, and two non-static
 * effects (whose mode numbers ctron does not know) are compared by
 * colour only. */
bool profile_aura_differs(const hw_state_t *hw, const char *aura);

/* Pure: apply CPU-ceiling rules to a snapshot model — "<mhz>", "core
 * <cpu-id> <mhz>" (its SMT sibling too), "ccd <n> <mhz>", "max" or
 * "clear" (drop the per-core list); several may be joined by ',' or
 * ';'. Everything is validated before anything is stored, so a bad rule
 * leaves *m untouched. 0 on success, -1 with a reason in err. */
int profile_core_rule(profile_meta_t *m, const hw_state_t *hw, const char *spec,
                      char *err, size_t errn);

void profile_meta_summary(const profile_meta_t *m, char *out, size_t n);
int  profile_meta_diff_count(const hw_state_t *hw, const profile_meta_t *m);
void profile_meta_diff(const hw_state_t *hw, const profile_meta_t *m,
                       char *out, size_t n);

/* ---- Saved Fan Curves (~/.config/ctron/fan_curves/<name>.fc) ------------
 * First-class reusable CPU/GPU fan curve presets with free-form names
 * ("group/name"), notes, selective CPU/GPU inclusion, live diff, and
 * direct selection inside both the Snapshots and Fans views. */

#define MAX_FCURVES 64
#define FCURVE_NAME_MAX 48

typedef struct {
    int cpu_on;          /* -1 omitted (skip CPU), 0 factory auto, 1 custom */
    int gpu_on;          /* -1 omitted (skip GPU), 0 factory auto, 1 custom */
    bool has_cpu;        /* CPU 8-point curve stored */
    bool has_gpu;        /* GPU 8-point curve stored */
    fan_curve_t cpu;
    fan_curve_t gpu;
    char saved[24];      /* "# saved:" header */
    char note[64];       /* "# note:" header */
} fcurve_meta_t;

void fcurve_gen_name(char *out, size_t n);
int  fcurve_list(char list[][FCURVE_NAME_MAX], int max);
void fcurve_meta_init(fcurve_meta_t *m);
void fcurve_meta_from_hw(const hw_state_t *hw, fcurve_meta_t *m);
int  fcurve_parse(const char *name, fcurve_meta_t *m);
int  fcurve_save(const char *name, const fcurve_meta_t *m);
int  fcurve_export(const char *name, const hw_state_t *hw);
int  fcurve_apply(const char *name, hw_state_t *hw, char *err, size_t errn);
int  fcurve_dry_run(const char *name, FILE *out, char *err, size_t errn);
int  fcurve_delete(const char *name);
int  fcurve_rename(const char *old_name, const char *new_name);
int  fcurve_set_note(const char *name, const char *note);
void fcurve_summary(const fcurve_meta_t *m, char *out, size_t n);
int  fcurve_diff_count(const hw_state_t *hw, const fcurve_meta_t *m);
void fcurve_diff(const hw_state_t *hw, const fcurve_meta_t *m,
                 char *out, size_t n);

#endif /* CTRON_PROFILE_H */
