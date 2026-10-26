#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* ---- file / sysfs access -------------------------------------------- */

int ut_read_file(const char *path, char *out, size_t n)
{
    if (!path || !out || n == 0)
        return -1;
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    out[0] = '\0';
    if (!fgets(out, (int)n, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    out[strcspn(out, "\r\n")] = '\0';
    return 0;
}

int ut_read_file_all(const char *path, char *out, size_t n)
{
    if (!path || !out || n == 0)
        return -1;
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    size_t off = 0;
    out[0] = '\0';
    while (off + 1 < n && fgets(out + off, (int)(n - off), f))
        off += strlen(out + off);
    fclose(f);
    if (off == 0)
        return -1;
    out[off] = '\0';
    while (off > 0 && (out[off - 1] == '\n' || out[off - 1] == '\r'))
        out[--off] = '\0';
    return 0;
}

int ut_read_int(const char *path)
{
    char buf[64];
    if (ut_read_file(path, buf, sizeof(buf)) != 0)
        return -1;
    return atoi(buf);
}

int ut_write_file(const char *path, const char *val)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    size_t len = strlen(val);
    int ok = (fwrite(val, 1, len, f) == len) && (fflush(f) == 0);
    /* fclose always runs — a failed write must not leak the fd. */
    ok = (fclose(f) == 0) && ok;
    return ok ? 0 : -1;
}

int ut_write_int(const char *path, long v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", v);
    return ut_write_file(path, buf);
}

/* Overridden by settings.c. A binary that does not link settings
 * keeps the sudo fallback (the daeboard unit test). */
__attribute__((weak)) int ut_sudo_fallback_allowed(void)
{
    return 1;
}

int ut_priv_write(const char *path, const char *val)
{
    if (ut_write_file(path, val) == 0)
        return 0;

    if (!ut_sudo_fallback_allowed())
        return -1;

    /* The fallback shells out; a single quote would break the quoting. */
    if (strchr(val, '\'') || strchr(path, '\''))
        return -1;
    char cmd[768];
    snprintf(cmd, sizeof(cmd),
             "printf '%%s' '%s' | sudo -n tee '%s' >/dev/null 2>&1", val, path);
    int rc = ut_exec(cmd, NULL, 0);
    return rc == 0 ? 0 : -1;
}

/* ---- process helpers ------------------------------------------------- */

bool ut_have_cmd(const char *name)
{
    /* `command` is a shell builtin, so this must not go through the
     * timeout-prefixed helper (timeout cannot exec builtins). */
    char cmd[256];
    if (snprintf(cmd, sizeof(cmd), "command -v %s >/dev/null 2>&1", name) >= (int)sizeof(cmd))
        return false;
    FILE *p = popen(cmd, "r");
    if (!p)
        return false;
    int st = pclose(p);
    return st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

static int exec_run(const char *cmd, char *out, size_t n, bool raw)
{
    char full[1024];
    if (snprintf(full, sizeof(full), "timeout 3 %s 2>/dev/null", cmd) >= (int)sizeof(full))
        return -1;

    FILE *p = popen(full, "r");
    if (!p)
        return -1;

    if (out && n > 0) {
        size_t r = fread(out, 1, n - 1, p);
        out[r] = '\0';
        if (raw) {
            /* strip ANSI escape sequences in place */
            size_t w = 0;
            for (size_t i = 0; i < r; i++) {
                if (out[i] == 0x1b) {
                    while (i < r && !isalpha((unsigned char)out[i]))
                        i++;
                    continue;
                }
                out[w++] = out[i];
            }
            out[w] = '\0';
        } else {
            out[strcspn(out, "\r\n")] = '\0';
        }
    } else {
        /* drain anyway: a child that prints (ryzenadj does) while the
         * caller wants no output races our pclose for the pipe — its
         * write hits a closed read end and it dies on SIGPIPE (141),
         * turning a successful command into a fake failure */
        char sink[4096];
        while (fread(sink, 1, sizeof sink, p) > 0)
            ;
    }

    int st = pclose(p);
    if (st == -1)
        return -1;
    if (WIFEXITED(st))
        return WEXITSTATUS(st);
    return -1;
}

int ut_exec(const char *cmd, char *out, size_t n)
{
    return exec_run(cmd, out, n, false);
}

int ut_exec_raw(const char *cmd, char *out, size_t n)
{
    return exec_run(cmd, out, n, true);
}

/* ---- single-instance guard ------------------------------------------- */

static char s_pidfile[256];
static long s_pidfile_pid;

/* Parse the pidfile content: stale when it does not name a live
 * process with our comm. self and comm describe the would-be holder
 * (used to verify liveness via /proc). */
bool ut_singleton_stale(const char *pidstr, long self, const char *comm)
{
    if (!pidstr || !*pidstr)
        return true;
    char *end = NULL;
    long pid = strtol(pidstr, &end, 10);
    if (end == pidstr || (end && *end != '\0' && *end != '\n'))
        return true;
    if (pid <= 1 || pid == self)
        return true;
    char p[96], got[64];
    snprintf(p, sizeof p, "/proc/%ld/comm", pid);
    if (ut_read_file(p, got, sizeof got) != 0)
        return true; /* dead */
    return strcmp(got, comm) != 0;
}

int ut_singleton_enter(const char *comm)
{
    /* An explicitly isolated config (--config-dir) is a deliberately
     * parallel instance, and CTRON_TEST marks the pty harness — a real
     * TUI running on the desktop must never make its flows bow out. */
    if (getenv("CTRON_CONFIG") || getenv("CTRON_TEST"))
        return -1;

    const char *dir = getenv("XDG_RUNTIME_DIR");
    if (!dir || !*dir)
        dir = getenv("TMPDIR");
    if (!dir || !*dir)
        dir = "/tmp";
    if (snprintf(s_pidfile, sizeof s_pidfile, "%s/ctron-tui.pid", dir)
        >= (int)sizeof s_pidfile)
        return -1;

    s_pidfile_pid = (long)getpid();

    /* Claim with O_EXCL|O_NOFOLLOW: in a shared fallback dir a planted
     * symlink is never followed, and only our own regular file is
     * trusted as a holder. Taking over means unlinking (which never
     * follows) and retrying the exclusive create; when the slot cannot
     * be taken (e.g. sticky /tmp foreign entry) we fail open. */
    char pidstr[24];
    snprintf(pidstr, sizeof pidstr, "%ld", s_pidfile_pid);
    for (int tries = 0; tries < 3; tries++) {
        int fd = open(s_pidfile, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW,
                      0600);
        if (fd >= 0) {
            size_t len = strlen(pidstr);
            ssize_t w = write(fd, pidstr, len);
            int rc = close(fd);
            if (w != (ssize_t)len || rc != 0) {
                unlink(s_pidfile);
                return -1;
            }
            return 0;
        }
        if (errno != EEXIST)
            return -1; /* cannot claim — fail open */
        struct stat st;
        if (lstat(s_pidfile, &st) == 0 && S_ISREG(st.st_mode) &&
            st.st_uid == geteuid()) {
            char buf[32];
            if (ut_read_file(s_pidfile, buf, sizeof buf) == 0 &&
                !ut_singleton_stale(buf, s_pidfile_pid, comm))
                return atoi(buf); /* live instance holds the slot */
        }
        if (unlink(s_pidfile) != 0 && errno != ENOENT)
            return -1; /* cannot take over — fail open */
    }
    return -1;
}

void ut_singleton_leave(void)
{
    if (!s_pidfile[0])
        return;
    char buf[32];
    if (ut_read_file(s_pidfile, buf, sizeof buf) == 0 &&
        atol(buf) == s_pidfile_pid)
        unlink(s_pidfile);
    s_pidfile[0] = '\0';
}

/* ---- filesystem ------------------------------------------------------ */

int ut_mkdir_p(const char *path)
{
    char buf[512];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(buf))
        return -1;
    memcpy(buf, path, len + 1);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(buf, 0755);
            *p = '/';
        }
    }
    if (mkdir(buf, 0755) == 0 || errno == EEXIST)
        return 0;
    return -1;
}

bool ut_path_exists(const char *path)
{
    return access(path, F_OK) == 0;
}

int ut_path_join(char *out, size_t n, const char *base, const char *leaf)
{
    if (!out || n == 0)
        return -1;
    int w = snprintf(out, n, "%s/%s", base ? base : "", leaf ? leaf : "");
    if (w < 0 || (size_t)w >= n)
        return -1;
    return 0;
}

/* ---- strings --------------------------------------------------------- */

char *ut_trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    if (!*s)
        return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end))
        end--;
    end[1] = '\0';
    return s;
}

int ut_parse_ints(const char *s, int *out, int max)
{
    int n = 0;
    const char *p = s;
    if (!s)
        return 0;
    while (*p && n < max) {
        while (*p == ',' || isspace((unsigned char)*p))
            p++;
        if (!*p)
            break;
        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end == p)
            break;
        out[n++] = (int)v;
        p = end;
    }
    return n;
}

int ut_clamp_i(int v, int lo, int hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

int ut_cells(const char *s)
{
    int cells = 0;
    if (!s)
        return 0;
    for (; *s; s++)
        if ((*s & 0xC0) != 0x80)
            cells++;
    return cells;
}

void ut_cell_join(char *dst, size_t n, const char *left, int cells,
                  const char *right)
{
    int pad;
    if (!dst || n == 0)
        return;
    left = left ? left : "";
    right = right ? right : "";
    pad = cells - ut_cells(left);
    if (pad < 0)
        pad = 0;
    snprintf(dst, n, "%s%*s%s", left, pad, "", right);
}

/* ---- ring log -------------------------------------------------------- */

static char s_logs[UT_LOG_ENTRIES][UT_LOG_LEN];
static int s_log_head = 0;
static int s_log_count = 0;
/* the apply worker logs while the UI thread reads the ring for the
 * footer — one lock covers both (get() copies under the lock, so a
 * slot being rewritten can never tear a displayed line) */
static pthread_mutex_t s_log_mtx = PTHREAD_MUTEX_INITIALIZER;

void ut_log(const char *fmt, ...)
{
    time_t now = time(NULL);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);
    char stamp[16];
    strftime(stamp, sizeof(stamp), "%H:%M:%S", &tm_buf);

    char msg[UT_LOG_LEN - 24];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    pthread_mutex_lock(&s_log_mtx);
    snprintf(s_logs[s_log_head], UT_LOG_LEN, "%s %s", stamp, msg);
    s_log_head = (s_log_head + 1) % UT_LOG_ENTRIES;
    if (s_log_count < UT_LOG_ENTRIES)
        s_log_count++;
    pthread_mutex_unlock(&s_log_mtx);
}

const char *ut_log_get(int idx)
{
    /* single caller per frame: copy into one static buffer under the
     * lock and hand the copy back */
    static char out[UT_LOG_LEN];
    pthread_mutex_lock(&s_log_mtx);
    if (idx < 0 || idx >= s_log_count) {
        out[0] = '\0';
    } else {
        int actual = (s_log_head - 1 - idx + UT_LOG_ENTRIES) % UT_LOG_ENTRIES;
        snprintf(out, UT_LOG_LEN, "%s", s_logs[actual]);
    }
    pthread_mutex_unlock(&s_log_mtx);
    return out;
}

int ut_log_count(void)
{
    pthread_mutex_lock(&s_log_mtx);
    int n = s_log_count;
    pthread_mutex_unlock(&s_log_mtx);
    return n;
}

/* ---- worker progress --------------------------------------------------- */

static pthread_mutex_t s_prog_mtx = PTHREAD_MUTEX_INITIALIZER;
static char s_prog_text[96];
static int s_prog_pct = -1;

void ut_progress(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    pthread_mutex_lock(&s_prog_mtx);
    vsnprintf(s_prog_text, sizeof(s_prog_text), fmt, ap);
    pthread_mutex_unlock(&s_prog_mtx);
    va_end(ap);
}

void ut_progress_pct(int pct)
{
    pthread_mutex_lock(&s_prog_mtx);
    s_prog_pct = pct;
    pthread_mutex_unlock(&s_prog_mtx);
}

const char *ut_progress_get(char *out, size_t n, int *pct)
{
    pthread_mutex_lock(&s_prog_mtx);
    snprintf(out, n, "%s", s_prog_text);
    if (pct)
        *pct = s_prog_pct;
    pthread_mutex_unlock(&s_prog_mtx);
    return out;
}

void ut_progress_clear(void)
{
    pthread_mutex_lock(&s_prog_mtx);
    s_prog_text[0] = '\0';
    s_prog_pct = -1;
    pthread_mutex_unlock(&s_prog_mtx);
}
