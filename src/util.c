#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "util.h"

#include <ctype.h>
#include <errno.h>
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
    int ok = (fwrite(val, 1, len, f) == len) && (fflush(f) == 0) && (fclose(f) == 0);
    if (!ok)
        return -1;
    return 0;
}

int ut_write_int(const char *path, long v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", v);
    return ut_write_file(path, buf);
}

int ut_priv_write(const char *path, const char *val)
{
    if (ut_write_file(path, val) == 0)
        return 0;

    /* Only plain values are ever passed by callers; quote defensively. */
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

/* ---- ring log -------------------------------------------------------- */

static char s_logs[UT_LOG_ENTRIES][UT_LOG_LEN];
static int s_log_head = 0;
static int s_log_count = 0;

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

    snprintf(s_logs[s_log_head], UT_LOG_LEN, "%s %s", stamp, msg);
    s_log_head = (s_log_head + 1) % UT_LOG_ENTRIES;
    if (s_log_count < UT_LOG_ENTRIES)
        s_log_count++;
}

const char *ut_log_get(int idx)
{
    if (idx < 0 || idx >= s_log_count)
        return "";
    int actual = (s_log_head - 1 - idx + UT_LOG_ENTRIES) % UT_LOG_ENTRIES;
    return s_logs[actual];
}

int ut_log_count(void)
{
    return s_log_count;
}
