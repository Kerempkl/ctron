#ifndef CTRON_MODES_H
#define CTRON_MODES_H

#include <stddef.h>

#include "hw.h"

/* User modes ("CLI shortcuts"). Stored in modes.ini as
 *   [turbo]
 *   steps = profile performance, ppt P80, fan cool, hz max
 * and applied step-by-step through the shared command table. */

#define MODES_MAX     32
#define MODE_NAME_MAX 24
#define MODE_STEPS_MAX 220

typedef struct {
    char name[MODE_NAME_MAX];
    char steps[MODE_STEPS_MAX];
} mode_def_t;

const char *modes_path(char *out, size_t n);

/* Load all modes; writes the defaults first when the file is missing. */
int modes_load(mode_def_t *arr, int max);
int modes_save(const mode_def_t *arr, int n);

/* Seed modes.ini with the stock shortcuts when absent. */
void modes_write_defaults(void);

int mode_find(const mode_def_t *arr, int n, const char *name);

/* Execute a comma separated step list ("profile performance, fan cool").
 * Stops at the first failing step. Returns 0 when all steps applied. */
int mode_apply(hw_state_t *hw, const char *steps, char *err, size_t errn);

#endif /* CTRON_MODES_H */
