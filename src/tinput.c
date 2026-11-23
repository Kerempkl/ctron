#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "tinput.h"

#include <stdio.h>
#include <string.h>

static void tin_norm(tinput_t *t)
{
    if (t->len >= sizeof(t->buf))
        t->len = sizeof(t->buf) - 1;
    t->buf[t->len] = '\0';
    if (t->cur > t->len)
        t->cur = t->len;
}

void tin_set(tinput_t *t, const char *s)
{
    snprintf(t->buf, sizeof(t->buf), "%s", s ? s : "");
    t->len = strlen(t->buf);
    t->cur = t->len;
}

void tin_clear(tinput_t *t)
{
    t->buf[0] = '\0';
    t->len = 0;
    t->cur = 0;
}

bool tin_is_edit_op(uint32_t op)
{
    switch (op) {
    case TIN_KEY_LEFT:
    case TIN_KEY_RIGHT:
    case TIN_KEY_HOME:
    case TIN_KEY_END:
    case TIN_KEY_BACKSPACE:
    case TIN_KEY_DEL:
    case 8:
    case 127:
    case 0x01: /* Ctrl+A */
    case 0x05: /* Ctrl+E */
    case 0x0b: /* Ctrl+K */
    case 0x15: /* Ctrl+U */
    case 0x17: /* Ctrl+W */
        return true;
    default:
        return false;
    }
}

bool tin_op(tinput_t *t, uint32_t op)
{
    tin_norm(t);

    if (op >= 32 && op < 127) {
        if (t->len + 1 < sizeof(t->buf)) {
            memmove(t->buf + t->cur + 1, t->buf + t->cur,
                    t->len - t->cur + 1);
            t->buf[t->cur++] = (char)op;
            t->len++;
        }
        return true;
    }

    switch (op) {
    case TIN_KEY_LEFT:
        if (t->cur > 0)
            t->cur--;
        return true;
    case TIN_KEY_RIGHT:
        if (t->cur < t->len)
            t->cur++;
        return true;
    case TIN_KEY_HOME:
    case 0x01: /* Ctrl+A */
        t->cur = 0;
        return true;
    case TIN_KEY_END:
    case 0x05: /* Ctrl+E */
        t->cur = t->len;
        return true;
    case TIN_KEY_BACKSPACE:
    case 127:
    case 8:
        if (t->cur > 0) {
            memmove(t->buf + t->cur - 1, t->buf + t->cur,
                    t->len - t->cur + 1);
            t->cur--;
            t->len--;
        }
        return true;
    case TIN_KEY_DEL:
        if (t->cur < t->len) {
            memmove(t->buf + t->cur, t->buf + t->cur + 1,
                    t->len - t->cur);
            t->len--;
        }
        return true;
    case 0x15: /* Ctrl+U: kill to start */
        if (t->cur > 0) {
            memmove(t->buf, t->buf + t->cur, t->len - t->cur + 1);
            t->len -= t->cur;
            t->cur = 0;
        }
        return true;
    case 0x0b: /* Ctrl+K: kill to end */
        if (t->cur < t->len) {
            t->buf[t->cur] = '\0';
            t->len = t->cur;
        }
        return true;
    case 0x17: /* Ctrl+W: delete previous word */
        if (t->cur > 0) {
            size_t p = t->cur;
            while (p > 0 && t->buf[p - 1] == ' ')
                p--;
            while (p > 0 && t->buf[p - 1] != ' ')
                p--;
            size_t del = t->cur - p;
            memmove(t->buf + p, t->buf + t->cur, t->len - t->cur + 1);
            t->len -= del;
            t->cur = p;
        }
        return true;
    default:
        return false;
    }
}

void tin_format(const tinput_t *t, char *out, size_t n, bool active)
{
    if (!out || n == 0)
        return;
    if (!active) {
        snprintf(out, n, "%s", t->buf);
        return;
    }
    size_t len = t->len < sizeof(t->buf) ? t->len : sizeof(t->buf) - 1;
    size_t cur = t->cur <= len ? t->cur : len;
    if (cur == len) {
        snprintf(out, n, "%.*s_", (int)len, t->buf);
    } else {
        snprintf(out, n, "%.*s|%.*s",
                 (int)cur, t->buf, (int)(len - cur), t->buf + cur);
    }
}
