#ifndef CTRON_CMDS_H
#define CTRON_CMDS_H

#include <stddef.h>

#include "hw.h"

/* Shared key/value command table. Used by:
 *   - the CLI flag parser (main.c: --profile X -> cmd_run(hw,"profile","X"))
 *   - user modes (modes.ini steps: "profile performance, fan cool")
 *   - .ctr profiles (key = value lines)
 *
 * On failure a short reason is written to `err` (when errn > 0) and -1 is
 * returned. Keys are matched case-insensitively. */

int cmd_run(hw_state_t *hw, const char *key, const char *val,
            char *err, size_t errn);

/* Parse a boolean-ish value: on/off/1/0/true/false. -1 when invalid. */
int cmd_parse_bool(const char *s);

#endif /* CTRON_CMDS_H */
