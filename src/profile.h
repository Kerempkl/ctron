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

void profile_meta_summary(const profile_meta_t *m, char *out, size_t n);
int  profile_meta_diff_count(const hw_state_t *hw, const profile_meta_t *m);
void profile_meta_diff(const hw_state_t *hw, const profile_meta_t *m,
                       char *out, size_t n);

#endif /* CTRON_PROFILE_H */
