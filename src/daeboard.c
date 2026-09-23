#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "daeboard.h"
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define DB_SOCK "/run/daeboard/daeboard.sock"

int db_fmt_brightness(int level, char *out, int n)
{
	if (!out || n < 14 || level < 0 || level > 3)
		return -1;
	snprintf(out, (size_t)n, "brightness %d", level);
	return 0;
}

int db_fmt_color(const char *hex, char *out, int n)
{
	int i;

	if (!out || n < 14 || !hex || strlen(hex) != 6)
		return -1;
	for (i = 0; i < 6; i++) {
		char c = hex[i];
		if (!isxdigit((unsigned char)c))
			return -1;
	}
	snprintf(out, (size_t)n, "color %s", hex);
	return 0;
}

static int exchange(const char *cmd, char *reply, int replyn)
{
	struct sockaddr_un addr;
	int fd, n;
	char buf[64];

	if (!cmd || !reply || replyn < 2)
		return -1;
	fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -1;
	memset(&addr, 0, sizeof addr);
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof addr.sun_path, "%s", DB_SOCK);
	if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
		if (errno == EACCES)
			ut_log("daeboard: socket not readable (need group wheel)");
		close(fd);
		return -1;
	}
	if (send(fd, cmd, strlen(cmd), MSG_NOSIGNAL) < 0) {
		close(fd);
		return -1;
	}
	for (;;) {
		n = (int)recv(fd, buf, sizeof buf - 1, 0);
		if (n <= 0) {
			close(fd);
			return -1;
		}
		buf[n] = 0;
		if (strncmp(buf, "fire ", 5) == 0)
			continue;
		if (n >= replyn)
			n = replyn - 1;
		memcpy(reply, buf, (size_t)n);
		reply[n] = 0;
		close(fd);
		return 0;
	}
}

int db_up(void)
{
	char reply[32];

	if (exchange("ping", reply, (int)sizeof reply) != 0)
		return 0;
	return strcmp(reply, "pong") == 0;
}

int db_set_brightness(int level)
{
	char cmd[32], reply[32];

	if (db_fmt_brightness(level, cmd, (int)sizeof cmd) != 0)
		return -1;
	if (exchange(cmd, reply, (int)sizeof reply) != 0)
		return -1;
	return strcmp(reply, "ok") == 0 ? 0 : -1;
}

int db_set_color(const char *hex)
{
	char cmd[32], reply[32];

	if (db_fmt_color(hex, cmd, (int)sizeof cmd) != 0)
		return -1;
	if (exchange(cmd, reply, (int)sizeof reply) != 0)
		return -1;
	return strcmp(reply, "ok") == 0 ? 0 : -1;
}

int db_quit(void)
{
	char reply[32];

	if (exchange("quit", reply, (int)sizeof reply) != 0)
		return -1;
	return strcmp(reply, "ok") == 0 ? 0 : -1;
}

int db_reload(char *err, int errn)
{
	char reply[32];

	if (exchange("reload", reply, (int)sizeof reply) != 0)
		return -1;
	if (strcmp(reply, "ok") == 0)
		return 0;
	if (err && errn > 0)
		snprintf(err, (size_t)errn, "%s", reply);
	return -1;
}

void db_binds_path(char *out, int n)
{
	const char *cfg = getenv("CTRON_CONFIG");
	const char *home;

	if (cfg && cfg[0]) {
		snprintf(out, (size_t)n, "%s/daeboard.binds", cfg);
		return;
	}
	home = getenv("HOME");
	if (!home)
		home = "/tmp";
	snprintf(out, (size_t)n, "%s/.config/ctron/daeboard.binds", home);
}

static int pick_bin(char *out, int n)
{
	const char *env = getenv("DAEBOARD_BIN");
	const char *home = getenv("HOME");
	char guess[512];

	if (env && env[0] && access(env, X_OK) == 0) {
		snprintf(out, (size_t)n, "%s", env);
		return 0;
	}
	if (access("/usr/local/bin/daeboard", X_OK) == 0) {
		snprintf(out, (size_t)n, "/usr/local/bin/daeboard");
		return 0;
	}
	if (home && home[0]) {
		snprintf(guess, sizeof guess, "%s/Documents/daeboard/daeboard", home);
		if (access(guess, X_OK) == 0) {
			snprintf(out, (size_t)n, "%s", guess);
			return 0;
		}
	}
	return -1;
}

int db_start(void)
{
	char binds[512], cmd[1400], bin[512];

	if (pick_bin(bin, (int)sizeof bin) != 0) {
		ut_log("daeboard: binary not found (set DAEBOARD_BIN)");
		return -1;
	}
	if (strchr(bin, '\'') || strchr(bin, '\n'))
		return -1;
	db_binds_path(binds, (int)sizeof binds);
	if (strchr(binds, '\''))
		return -1;
	if (system("sudo -n true >/dev/null 2>&1") != 0) {
		ut_log("daeboard: start needs passwordless sudo");
		return -1;
	}
	snprintf(cmd, sizeof cmd,
		 "sudo -n systemd-run --unit=daeboard --collect "
		 "--description=daeboard '%s' --binds '%s' >/dev/null 2>&1",
		 bin, binds);
	if (system(cmd) != 0) {
		ut_log("daeboard: systemd-run failed");
		return -1;
	}
	ut_log("daeboard: started");
	return 0;
}

int db_install(void)
{
	char path[512];
	char cmd[640];
	const char *src = getenv("DAEBOARD_SRC");
	const char *home;

	if (access("/etc/NIXOS", F_OK) == 0) {
		ut_log("daeboard: NixOS uses Start, not install.sh");
		return db_start();
	}
	if (src && src[0])
		snprintf(path, sizeof path, "%s/install.sh", src);
	else {
		home = getenv("HOME");
		snprintf(path, sizeof path, "%s/Documents/daeboard/install.sh",
			 home ? home : "");
	}
	if (strchr(path, '\'') || access(path, X_OK) != 0) {
		ut_log("daeboard: no installer at %s (AUR package daeboard)", path);
		return -1;
	}
	if (system("sudo -n true >/dev/null 2>&1") != 0) {
		ut_log("daeboard: install needs passwordless sudo");
		return -1;
	}
	snprintf(cmd, sizeof cmd, "sudo -n '%s'", path);
	if (system(cmd) != 0) {
		ut_log("daeboard: install.sh failed");
		return -1;
	}
	ut_log("daeboard: installed");
	return 0;
}

void db_install_note(char *out, int n)
{
	if (access("/etc/NIXOS", F_OK) == 0) {
		snprintf(out, (size_t)n,
			 "NixOS: use Start. install.sh and nixos-rebuild stay out of ctron.");
		return;
	}
	snprintf(out, (size_t)n,
		 "Install AUR package daeboard, or sudo ./install.sh from the daeboard repo.");
}

int db_action_in(const char *text, const char *key,
		 char *cmd, int cmdn, char *val, int valn)
{
	const char *p = text;
	int in = 0;

	if (!text || !key || !cmd || !val)
		return -1;
	cmd[0] = val[0] = 0;
	while (*p) {
		const char *eol = strchr(p, '\n');
		char line[256];
		int n;
		char *s;

		if (!eol)
			eol = p + strlen(p);
		n = (int)(eol - p);
		if (n > (int)sizeof line - 1)
			n = (int)sizeof line - 1;
		memcpy(line, p, (size_t)n);
		line[n] = 0;
		s = line;
		while (*s == ' ' || *s == '\t')
			s++;
		if (*s == '[') {
			char name[32];
			char *e = strchr(s, ']');
			int kn;

			in = 0;
			if (e && e > s + 1) {
				kn = (int)(e - (s + 1));
				if (kn > (int)sizeof name - 1)
					kn = (int)sizeof name - 1;
				memcpy(name, s + 1, (size_t)kn);
				name[kn] = 0;
				if (strcasecmp(name, key) == 0)
					in = 1;
			}
		} else if (in && strncmp(s, "ctron", 5) == 0) {
			char *eq = strchr(s, '=');
			char *c, *v;

			if (!eq)
				return -1;
			c = eq + 1;
			while (*c == ' ' || *c == '\t')
				c++;
			v = c;
			while (*v && *v != ' ' && *v != '\t')
				v++;
			if (v == c)
				return -1;
			if ((int)(v - c) >= cmdn)
				return -1;
			memcpy(cmd, c, (size_t)(v - c));
			cmd[v - c] = 0;
			while (*v == ' ' || *v == '\t')
				v++;
			snprintf(val, (size_t)valn, "%s", v);
			return 0;
		}
		if (*eol == '\n')
			p = eol + 1;
		else
			break;
	}
	return -1;
}

int db_follow(int (*on_fire)(const char *key, void *ud), void *ud)
{
	struct sockaddr_un addr;
	int fd;

	fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -1;
	memset(&addr, 0, sizeof addr);
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof addr.sun_path, "%s", DB_SOCK);
	if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
		if (errno == EACCES)
			fprintf(stderr, "ctron: daeboard socket needs group wheel\n");
		else
			fprintf(stderr, "ctron: daeboard is not running\n");
		close(fd);
		return -1;
	}
	fprintf(stderr, "ctron: following daeboard\n");
	for (;;) {
		char buf[64];
		int n = (int)recv(fd, buf, sizeof buf - 1, 0);

		if (n <= 0)
			break;
		buf[n] = 0;
		if (strncmp(buf, "fire ", 5) != 0)
			continue;
		if (on_fire && on_fire(buf + 5, ud) != 0)
			break;
	}
	close(fd);
	return 0;
}
