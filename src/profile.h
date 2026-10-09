#ifndef CTRON_PROFILE_H
#define CTRON_PROFILE_H

#include <stddef.h>

#include "hw.h"

/* Saved hardware snapshots (.ctr) with free-form names ("oyun/turbo"
 * style names group into subdirectories). Format (ini): [perf] [power]
 * [light] sections whose key=value lines are fed through the shared
 * command table on import, so a snapshot is simply a frozen list of
 * the same commands the CLI accepts. */

#define MAX_PROFILES 64
#define PROFILE_NAME_MAX 48

void profile_gen_name(char *out, size_t n);

/* Public for tests: [A-Za-z0-9._-] plus '/' as a group separator
 * (collapsed/trimmed; "." and ".." segments are rejected). */
void profile_sanitize_name(const char *in, char *out, size_t n);

int profile_list(char list[][PROFILE_NAME_MAX], int max);
int profile_export(const char *name, const hw_state_t *hw);
int profile_import(const char *name, hw_state_t *hw, char *err, size_t errn);
int profile_delete(const char *name);
int profile_summary(const char *name, char *out, size_t n);
int profile_rename(const char *old_name, const char *new_name);
/* Rewrite only the "# note:" header line, byte-preserving the rest. */
int profile_set_note(const char *name, const char *note);

/* One-line summary of what export would capture right now (save
 * preview). */
void profile_capture_line(const hw_state_t *hw, char *out, size_t n);

/* Parsed snapshot metadata for the list summary + live diff. Fields
 * left at their zero/"" defaults when the file omits them. */
typedef struct {
    char profile[16];
    char epp[24];
    int freq;                        /* 0 unknown */
    int hz;                          /* 0 unknown */
    int bat;                         /* 0 unknown */
    char kbd[8];
    int fan_cpu_on, fan_gpu_on;      /* -1 unknown */
    char fan_cpu_t[64], fan_cpu_p[64];
    char fan_gpu_t[64], fan_gpu_p[64];
    char saved[24];                  /* "# saved:" header, "" none */
    char note[64];                   /* "# note:" header, "" none */
} profile_meta_t;

int profile_meta_parse(const char *name, profile_meta_t *m);

/* Pure: which tracked fields a snapshot apply would change right now
 * ("Δ profile · hz · fan-curve cpu"; "≡ live" when nothing differs,
 * absent fields never count). */
void profile_meta_diff(const hw_state_t *hw, const profile_meta_t *m,
                       char *out, size_t n);

#endif /* CTRON_PROFILE_H */
