#ifndef CTRON_UTIL_H
#define CTRON_UTIL_H

#include <stdbool.h>
#include <stddef.h>

/* ---- file / sysfs access -------------------------------------------- */

/* Read the FIRST LINE of a file, trimmed of the trailing newline.
 * Returns 0 on success, -1 if the file cannot be read. */
int ut_read_file(const char *path, char *out, size_t n);

/* Read a WHOLE file (up to n-1 bytes), trailing newline(s) trimmed.
 * Returns 0 on success, -1 on error or an empty file. */
int ut_read_file_all(const char *path, char *out, size_t n);

/* Read an integer from a file (sysfs semantics). Returns -1 on failure. */
int ut_read_int(const char *path);

/* Write a string to a file directly (no privilege escalation).
 * Returns 0 on success, -1 otherwise. */
int ut_write_file(const char *path, const char *val);

int ut_write_int(const char *path, long v);

/* Privileged write chain: direct write first, then `sudo -n tee`.
 * Returns 0 on success. Never prompts for a password.
 * The sudo step runs only when ut_sudo_fallback_allowed() is nonzero.
 * The default is allowed. settings.c replaces it so "no sudo" is real. */
int ut_priv_write(const char *path, const char *val);
int ut_sudo_fallback_allowed(void);

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
/* base + "/" + leaf into out, bounds-checked. 0 ok, -1 truncated. */
int ut_path_join(char *out, size_t n, const char *base, const char *leaf);

/* ---- strings --------------------------------------------------------- */

char *ut_trim(char *s);

/* Parse a comma/whitespace separated int list. Returns count parsed. */
int ut_parse_ints(const char *s, int *out, int max);

/* Clamp helpers. */
int ut_clamp_i(int v, int lo, int hi);

/* Display cells in a UTF-8 string (lead bytes, not continuation). */
int ut_cells(const char *s);

/* Copy `left`, pad with spaces to `cells` display cells, then `right`. */
void ut_cell_join(char *dst, size_t n, const char *left, int cells,
                  const char *right);

/* ---- ring log (footer of the TUI, --status tail) --------------------- */

#define UT_LOG_ENTRIES 24
#define UT_LOG_LEN     128

void ut_log(const char *fmt, ...);
const char *ut_log_get(int idx); /* 0 = most recent */
int ut_log_count(void);

#endif /* CTRON_UTIL_H */
