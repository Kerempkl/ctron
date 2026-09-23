#ifndef CTRON_DAEBOARD_H
#define CTRON_DAEBOARD_H

/* Talks to the daeboard daemon. No link to that tree.
 * Light commands go here when ping succeeds. The daemon stays the
 * only writer of the keyboard while it is up. */

int db_fmt_brightness(int level, char *out, int n);
int db_fmt_color(const char *hex, char *out, int n);

/* 1 when the daemon answers ping. */
int db_up(void);
int db_set_brightness(int level);
int db_set_color(const char *hex);
int db_quit(void);
/* 0 ok. err receives "err" or "err <line>". */
int db_reload(char *err, int errn);

void db_binds_path(char *out, int n);
int db_start(void);
/* Writes a one-line install note. Never runs an AUR helper or nixos-rebuild. */
void db_install_note(char *out, int n);
/* Non-NixOS: sudo -n the repo install.sh. NixOS: same as start. */
int db_install(void);

/* Find `ctron = <cmd> <val>` under [key] in a binds file body.
 * 0 when found. */
int db_action_in(const char *text, const char *key,
		 char *cmd, int cmdn, char *val, int valn);

/* Block, calling on_fire with the key name from each `fire` datagram.
 * Returns when the socket closes. */
int db_follow(int (*on_fire)(const char *key, void *ud), void *ud);

#endif
