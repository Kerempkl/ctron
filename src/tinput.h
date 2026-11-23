#ifndef CTRON_TINPUT_H
#define CTRON_TINPUT_H

/* Pure single-line text input with cursor editing (no notcurses
 * dependency so unit tests can exercise every motion directly).
 * buf holds the longest editable field (mode steps = MODE_STEPS_MAX). */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "modes.h"

typedef struct {
    char buf[MODE_STEPS_MAX];
    size_t len;
    size_t cur;
} tinput_t;

enum {
    TIN_KEY_LEFT      = 0x100001u,
    TIN_KEY_RIGHT     = 0x100002u,
    TIN_KEY_HOME      = 0x100003u,
    TIN_KEY_END       = 0x100004u,
    TIN_KEY_BACKSPACE = 0x100005u,
    TIN_KEY_DEL       = 0x100006u,
};

void tin_set(tinput_t *t, const char *s);
void tin_clear(tinput_t *t);
/* True for cursor/delete ops (non-printable editing keys). */
bool tin_is_edit_op(uint32_t op);
/* Process printable ASCII (32..126), Ctrl+A/E/K/U/W (0x01/0x05/0x0b/
 * 0x15/0x17), raw BS/DEL (8/127), or TIN_KEY_*. Returns true if handled. */
bool tin_op(tinput_t *t, uint32_t op);
/* Format t->buf into out. When active, renders '_' at end or '|' mid. */
void tin_format(const tinput_t *t, char *out, size_t n, bool active);

#endif /* CTRON_TINPUT_H */
