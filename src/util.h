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
 * The default is allowed. settings.c replaces it so "no sudo" is real.
 * The fallback is a pure argv exec — no shell ever parses the path or
 * the value, so quotes/metacharacters are safe to write. */
int ut_priv_write(const char *path, const char *val);
int ut_sudo_fallback_allowed(void);

/* Feed `data` to a child's stdin and wait for it: fork + execvp of a
 * plain argv vector (no shell). Returns the child's exit status, or -1
 * when it could not be started/reaped. The writer survives a child
 * that exits before reading (EPIPE is not fatal). */
int ut_write_via_argv(const char *const argv[], const char *data);

/* ---- process helpers ------------------------------------------------- */

/* Run `cmd` through the shell with a 3 s timeout, stderr dropped.
 * First line of stdout is trimmed into `out` (may be NULL).
 * Returns the exit status, or -1 if the command could not be run. */
int ut_exec(const char *cmd, char *out, size_t n);

/* Same as ut_exec but keeps the whole output (no line trimming, ANSI
 * escape sequences stripped). For multi-line tool output parsers. */
int ut_exec_raw(const char *cmd, char *out, size_t n);

/* Shell-free variants of the same contract: fork + execvp of a plain
 * argv vector under `timeout 3`. Nothing parses the arguments, so
 * quoting/metacharacters are never a concern. ut_exec_argv keeps the
 * first line only; _raw keeps everything with ANSI stripped; _both
 * additionally merges stderr into the capture (privileged driver calls
 * whose verdict text may land on either stream). Output beyond the
 * buffer is drained, so a chatty child cannot fake a SIGPIPE failure. */
int ut_exec_argv(const char *const argv[], char *out, size_t n);
int ut_exec_argv_raw(const char *const argv[], char *out, size_t n);
int ut_exec_argv_both(const char *const argv[], char *out, size_t n);

/* True when `name` resolves to an executable on PATH. */
bool ut_have_cmd(const char *name);

/* ---- single-instance guard (fullscreen TUI) ----------------------------- */

/* Claim the single TUI slot for processes whose /proc comm equals
 * `comm` (the binary name). Returns:
 *    0  slot claimed for us (pidfile written, call ut_singleton_leave)
 *   >0  another live instance holds it (its pid — bow out)
 *   -1  no runtime dir / undecidable — fail open, run anyway.
 * A stale pidfile (dead pid, comm mismatch, garbage) is overwritten.
 * The pidfile is created 0600 with O_EXCL|O_NOFOLLOW; a symlink or
 * foreign-owned entry in a shared fallback dir is never followed or
 * trusted as a holder — it is replaced (or the guard fails open).
 * Pure decision part (ut_singleton_stale) is unit-tested. */
int ut_singleton_enter(const char *comm);
void ut_singleton_leave(void); /* removes the pidfile if it is ours */
bool ut_singleton_stale(const char *pidstr, long self, const char *comm);

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

/* ---- worker progress (TUI apply status line; CLI silent) ---------- */

/* Publish what a long write sequence is doing right now; pct -1 =
 * indeterminate. Thread-safe. */
void ut_progress(const char *fmt, ...);
void ut_progress_pct(int pct);
/* Copy the latest progress out (pct via *pct, may be NULL). Returns
 * an empty string after ut_progress_clear(). */
const char *ut_progress_get(char *out, size_t n, int *pct);
void ut_progress_clear(void);

#endif /* CTRON_UTIL_H */
