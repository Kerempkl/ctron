#ifndef CTRON_UTIL_H
#define CTRON_UTIL_H

#include <stdbool.h>
#include <stddef.h>

/* ---- file / sysfs access -------------------------------------------- */

/* Read a whole (small) file, trimmed of the trailing newline.
 * Returns 0 on success, -1 if the file cannot be read. */
int ut_read_file(const char *path, char *out, size_t n);

/* Read an integer from a file (sysfs semantics). Returns -1 on failure. */
int ut_read_int(const char *path);

/* Write a string to a file directly (no privilege escalation).
 * Returns 0 on success, -1 otherwise. */
int ut_write_file(const char *path, const char *val);

int ut_write_int(const char *path, long v);

/* Privileged write chain: direct write first, then `sudo -n tee`.
 * Returns 0 on success. Never prompts for a password. */
int ut_priv_write(const char *path, const char *val);

/* ---- process helpers ------------------------------------------------- */

/* Run `cmd` through the shell with a 3 s timeout, stderr dropped.
 * First line of stdout is trimmed into `out` (may be NULL).
 * Returns the exit status, or -1 if the command could not be run. */
int ut_exec(const char *cmd, char *out, size_t n);

/* Same as ut_exec but keeps the whole output (no line trimming, ANSI
 * escape sequences stripped). For multi-line tool output parsers. */
int ut_exec_raw(const char *cmd, char *out, size_t n);

/* True when `name` resolves to an executable on PATH. */
bool ut_have_cmd(const char *name);

/* ---- filesystem ------------------------------------------------------ */

int ut_mkdir_p(const char *path);
bool ut_path_exists(const char *path);

/* ---- strings --------------------------------------------------------- */

char *ut_trim(char *s);

/* Parse a comma/whitespace separated int list. Returns count parsed. */
int ut_parse_ints(const char *s, int *out, int max);

/* Clamp helpers. */
int ut_clamp_i(int v, int lo, int hi);

/* ---- ring log (footer of the TUI, --status tail) --------------------- */

#define UT_LOG_ENTRIES 24
#define UT_LOG_LEN     128

void ut_log(const char *fmt, ...);
const char *ut_log_get(int idx); /* 0 = most recent */
int ut_log_count(void);

#endif /* CTRON_UTIL_H */
