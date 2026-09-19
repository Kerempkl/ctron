#ifndef CTRON_PROFILE_H
#define CTRON_PROFILE_H

#include <stddef.h>

#include "hw.h"

/* Saved hardware profiles (.ctr) with free-form names.
 * Format (ini): [perf] [power] [light] sections whose key=value lines are
 * fed through the shared command table on import, so a profile is simply
 * a frozen list of the same commands the CLI accepts. */

#define MAX_PROFILES 64
#define PROFILE_NAME_MAX 32

void profile_gen_name(char *out, size_t n);

int profile_list(char list[][PROFILE_NAME_MAX], int max);
int profile_export(const char *name, const hw_state_t *hw);
int profile_import(const char *name, hw_state_t *hw, char *err, size_t errn);
int profile_delete(const char *name);
int profile_summary(const char *name, char *out, size_t n);

#endif /* CTRON_PROFILE_H */
