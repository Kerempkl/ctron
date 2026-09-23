#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ui_internal.h"
#include "../daeboard.h"
#include "../util.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <linux/input.h>

#define DBE_KEYS 16
#define DBE_STEPS 32

typedef struct {
	char name[24];
	char down[DBE_STEPS][48];
	char up[DBE_STEPS][48];
	int n_down, n_up;
	char ctron[72];
} dbe_key;

static dbe_key keys[DBE_KEYS];
static int nkeys;
static int keyi, side, step, focus; /* focus: 0 keys, 1 steps, 2 ctron, 3 typing */
static int typing_ctron;
static int dirty;
static int picker;
static int keypick;
static int rebind;
static int scr[2];
static int vis_rows = 8;
static int cap_fd = -1;

static void see_step(void)
{
	int *s = &scr[side ? 1 : 0];

	if (vis_rows < 1)
		vis_rows = 1;
	if (step < *s)
		*s = step;
	if (step >= *s + vis_rows)
		*s = step - vis_rows + 1;
	if (*s < 0)
		*s = 0;
}

static void choose_key(const char *name);

static const char *name_for_code(int code);

static int open_keyboard(void)
{
	DIR *d;
	struct dirent *de;

	d = opendir("/sys/class/input");
	if (!d)
		return -1;
	while ((de = readdir(d)) != NULL) {
		char path[512], name[128];
		int fd, n;

		if (strncmp(de->d_name, "event", 5) != 0)
			continue;
		snprintf(path, sizeof path, "/sys/class/input/%s/device/name", de->d_name);
		fd = open(path, O_RDONLY | O_CLOEXEC);
		if (fd < 0)
			continue;
		n = (int)read(fd, name, sizeof name - 1);
		close(fd);
		if (n <= 0)
			continue;
		if (name[n - 1] == '\n')
			n--;
		name[n] = 0;
		if (strcmp(name, "AT Translated Set 2 keyboard") != 0)
			continue;
		snprintf(path, sizeof path, "/dev/input/%s", de->d_name);
		closedir(d);
		return open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	}
	closedir(d);
	return -1;
}

static void poll_key_capture(void)
{
	struct input_event ev;

	if (!keypick)
		return;
	if (cap_fd < 0)
		cap_fd = open_keyboard();
	if (cap_fd < 0)
		return;
	while (read(cap_fd, &ev, sizeof ev) == (ssize_t)sizeof ev) {
		const char *name;

		if (ev.type != EV_KEY || ev.value != 1)
			continue;
		name = name_for_code(ev.code);
		if (!name) {
			ut_log("daeboard: key %d is not in the map", ev.code);
			continue;
		}
		choose_key(name);
		return;
	}
}
static tinput_t typed;

static const char *const kops[] = { "color", "brightness", "breathe", "fade", "rest" };

static void seed(void)
{
	nkeys = 3;
	memset(keys, 0, sizeof keys);
	snprintf(keys[0].name, sizeof keys[0].name, "leftmeta");
	snprintf(keys[0].down[0], sizeof keys[0].down[0], "breathe 0");
	keys[0].n_down = 1;
	snprintf(keys[0].up[0], sizeof keys[0].up[0], "rest");
	keys[0].n_up = 1;
	snprintf(keys[1].name, sizeof keys[1].name, "enter");
	snprintf(keys[1].down[0], sizeof keys[1].down[0], "brightness 0 120");
	snprintf(keys[1].down[1], sizeof keys[1].down[1], "brightness 3 120");
	snprintf(keys[1].down[2], sizeof keys[1].down[2], "brightness 0 120");
	snprintf(keys[1].down[3], sizeof keys[1].down[3], "brightness 3 120");
	keys[1].n_down = 4;
	snprintf(keys[2].name, sizeof keys[2].name, "backspace");
	snprintf(keys[2].down[0], sizeof keys[2].down[0], "color ff0000 0");
	keys[2].n_down = 1;
	snprintf(keys[2].up[0], sizeof keys[2].up[0], "fade 2000");
	keys[2].n_up = 1;
}

static char *steps_of(dbe_key *k, int *n)
{
	if (side == 0) {
		*n = k->n_down;
		return k->down[0];
	}
	*n = k->n_up;
	return k->up[0];
}

static char *step_at(dbe_key *k, int i)
{
	if (side == 0)
		return k->down[i];
	return k->up[i];
}

static void set_count(dbe_key *k, int n)
{
	if (side == 0)
		k->n_down = n;
	else
		k->n_up = n;
}

static void trim(char *s)
{
	char *e;
	while (*s == ' ' || *s == '\t')
		memmove(s, s + 1, strlen(s));
	e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
		*--e = 0;
}

static int load_file(void)
{
	char path[512];
	FILE *f;
	char line[8192];
	dbe_key *cur = NULL;

	db_binds_path(path, (int)sizeof path);
	f = fopen(path, "r");
	nkeys = 0;
	memset(keys, 0, sizeof keys);
	if (!f) {
		seed();
		dirty = 0;
		return 0;
	}
	while (fgets(line, sizeof line, f)) {
		char *s = line;
		char *lb;
		trim(s);
		if (!*s || *s == '#')
			continue;
		if (*s == '[') {
			char *e = strchr(s, ']');
			if (!e || nkeys >= DBE_KEYS)
				continue;
			*e = 0;
			cur = &keys[nkeys++];
			memset(cur, 0, sizeof *cur);
			snprintf(cur->name, sizeof cur->name, "%s", s + 1);
			continue;
		}
		if (!cur)
			continue;
		lb = strchr(s, '=');
		if (!lb)
			continue;
		*lb = 0;
		trim(s);
		trim(lb + 1);
		if (!strcmp(s, "ctron")) {
			snprintf(cur->ctron, sizeof cur->ctron, "%s", lb + 1);
		} else if (!strcmp(s, "down") || !strcmp(s, "up")) {
			char *list = lb + 1;
			int up = !strcmp(s, "up");
			char *dst = up ? cur->up[0] : cur->down[0];
			int *pn = up ? &cur->n_up : &cur->n_down;
			while (*list && *pn < DBE_STEPS) {
				char *comma = strchr(list, ',');
				char *slot = dst + (*pn) * 48;
				int n;
				if (comma)
					*comma = 0;
				trim(list);
				n = (int)strlen(list);
				if (n > 47)
					n = 47;
				memcpy(slot, list, (size_t)n);
				slot[n] = 0;
				if (slot[0])
					(*pn)++;
				if (!comma)
					break;
				list = comma + 1;
			}
		}
	}
	fclose(f);
	if (nkeys == 0)
		seed();
	dirty = 0;
	return 0;
}

static int save_file(void)
{
	char path[512];
	FILE *f;
	int i, s;

	db_binds_path(path, (int)sizeof path);
	f = fopen(path, "w");
	if (!f) {
		ut_log("daeboard: cannot write %s", path);
		return -1;
	}
	for (i = 0; i < nkeys; i++) {
		fprintf(f, "[%s]\n", keys[i].name);
		if (keys[i].n_down) {
			fprintf(f, "down =");
			for (s = 0; s < keys[i].n_down; s++)
				fprintf(f, "%s %s", s ? "," : "", keys[i].down[s]);
			fprintf(f, "\n");
		}
		if (keys[i].n_up) {
			fprintf(f, "up =");
			for (s = 0; s < keys[i].n_up; s++)
				fprintf(f, "%s %s", s ? "," : "", keys[i].up[s]);
			fprintf(f, "\n");
		}
		if (keys[i].ctron[0])
			fprintf(f, "ctron = %s\n", keys[i].ctron);
		fprintf(f, "\n");
	}
	fclose(f);
	dirty = 0;
	{
		char err[64];
		if (!db_up())
			db_start();
		if (db_reload(err, (int)sizeof err) != 0)
			ut_log("daeboard: saved, reload %s", err[0] ? err : "failed");
		else
			ut_log("daeboard: saved and reloaded");
	}
	return 0;
}

static int op_index(const char *step)
{
	int i;
	for (i = 0; i < 5; i++) {
		int n = (int)strlen(kops[i]);
		if (!strncmp(step, kops[i], (size_t)n) &&
		    (step[n] == 0 || step[n] == ' '))
			return i;
	}
	return 0;
}

static int last_int(const char *step)
{
	const char *p = step;
	int v = -1;
	while (*p) {
		if (isdigit((unsigned char)*p)) {
			v = 0;
			while (isdigit((unsigned char)*p)) {
				v = v * 10 + (*p - '0');
				p++;
			}
		} else {
			p++;
		}
	}
	return v < 0 ? 0 : v;
}

static void cycle_step(char *step)
{
	int op = (op_index(step) + 1) % 5;
	int ms = last_int(step);
	if (op == 3 && ms == 0)
		ms = 2000;
	switch (op) {
	case 0: snprintf(step, 48, "color ffffff %d", ms ? ms : 200); break;
	case 1: snprintf(step, 48, "brightness 3 %d", ms); break;
	case 2: snprintf(step, 48, "breathe %d", ms); break;
	case 3: snprintf(step, 48, "fade %d", ms ? ms : 2000); break;
	default: snprintf(step, 48, "rest"); break;
	}
}

static void bump_step(char *step, int delta)
{
	int op = op_index(step);
	int ms = last_int(step) + delta;
	if (ms < 0)
		ms = 0;
	if (op == 4)
		return;
	if (op == 0) {
		char hex[8] = "ffffff";
		const char *p = strchr(step, ' ');
		if (p) {
			int i = 0;
			p++;
			while (*p && *p != ' ' && i < 6) {
				hex[i++] = *p++;
			}
			hex[i] = 0;
		}
		snprintf(step, 48, "color %s %d", hex, ms);
	} else if (op == 1) {
		int bri = 3;
		if (sscanf(step, "brightness %d", &bri) != 1)
			bri = 3;
		snprintf(step, 48, "brightness %d %d", bri, ms);
	} else if (op == 2) {
		snprintf(step, 48, "breathe %d", ms);
	} else if (op == 3) {
		snprintf(step, 48, "fade %d", ms);
	}
}

enum {
	ID_START = 1,
	ID_STOP,
	ID_INSTALL,
	ID_SAVE,
	ID_SIDE_DOWN,
	ID_SIDE_UP,
	ID_ADD_COLOR,
	ID_ADD_FADE,
	ID_ADD_BREATHE,
	ID_CUT,
	ID_ADD_KEY,
	ID_DEL_KEY,
	ID_CHANGE_KEY,
	/* Each range is wider than DBE_STEPS so the ids cannot overlap. */
	ID_PRESET = 100,
	ID_KEY = 200,
	ID_STEP = 400,
	ID_MVU = 800,
	ID_MVD = 1200,
	ID_HEX = 1600,
	ID_SWATCH = 2000,
	ID_PICKKEY = 3000
};

#define COL 64

static const struct {
	const char *name;
	int code;
} keycatalog[] = {
	{ "esc", 1 }, { "1", 2 }, { "2", 3 }, { "3", 4 }, { "4", 5 },
	{ "5", 6 }, { "6", 7 }, { "7", 8 }, { "8", 9 }, { "9", 10 }, { "0", 11 },
	{ "minus", 12 }, { "equal", 13 }, { "backspace", 14 }, { "tab", 15 },
	{ "q", 16 }, { "w", 17 }, { "e", 18 }, { "r", 19 }, { "t", 20 },
	{ "y", 21 }, { "u", 22 }, { "i", 23 }, { "o", 24 }, { "p", 25 },
	{ "enter", 28 }, { "leftctrl", 29 }, { "a", 30 }, { "s", 31 }, { "d", 32 },
	{ "f", 33 }, { "g", 34 }, { "h", 35 }, { "j", 36 }, { "k", 37 }, { "l", 38 },
	{ "leftshift", 42 }, { "z", 44 }, { "x", 45 }, { "c", 46 }, { "v", 47 },
	{ "b", 48 }, { "n", 49 }, { "m", 50 }, { "rightshift", 54 },
	{ "leftalt", 56 }, { "space", 57 }, { "capslock", 58 },
	{ "f1", 59 }, { "f2", 60 }, { "f3", 61 }, { "f4", 62 }, { "f5", 63 },
	{ "f6", 64 }, { "f7", 65 }, { "f8", 66 }, { "f9", 67 }, { "f10", 68 },
	{ "f11", 87 }, { "f12", 88 }, { "rightctrl", 97 }, { "rightalt", 100 },
	{ "up", 103 }, { "left", 105 }, { "right", 106 }, { "down", 108 },
	{ "delete", 111 }, { "leftmeta", 125 }, { "rightmeta", 126 },
};
#define NKEYS_CAT ((int)(sizeof keycatalog / sizeof keycatalog[0]))

static const char *name_for_code(int code)
{
	int i;

	for (i = 0; i < NKEYS_CAT; i++) {
		if (keycatalog[i].code == code)
			return keycatalog[i].name;
	}
	return NULL;
}

static const struct {
	const char *name;
	const char *hex;
	unsigned rgb;
} swatches[] = {
	{ "Ice", "ccfffe", 0xccfffeu },
	{ "White", "ffffff", 0xffffffu },
	{ "Black", "000000", 0x000000u },
	{ "Red", "ff0000", 0xff0000u },
	{ "Amber", "ff8800", 0xff8800u },
	{ "Green", "00ff66", 0x00ff66u },
	{ "Cyan", "00e5ff", 0x00e5ffu },
	{ "Blue", "2266ff", 0x2266ffu },
	{ "Purple", "aa44ff", 0xaa44ffu },
};

static void move_step(int dir)
{
	int n = 0, j;
	char tmp[48];
	char *base;

	if (keyi < 0 || keyi >= nkeys)
		return;
	base = steps_of(&keys[keyi], &n);
	j = step + dir;
	if (n < 2 || j < 0 || j >= n)
		return;
	memcpy(tmp, base + step * 48, 48);
	memcpy(base + step * 48, base + j * 48, 48);
	memcpy(base + j * 48, tmp, 48);
	step = j;
	dirty = 1;
	focus = 1;
	see_step();
}

static void paint_color(char *slot, const char *hex)
{
	int ms = last_int(slot);

	if (op_index(slot) != 0)
		ms = ms ? ms : 0;
	snprintf(slot, 48, "color %s %d", hex, ms);
	dirty = 1;
}

static void clamp_step(void);

static void cut_step(void)
{
	int n = 0, i;
	char *base;

	if (keyi < 0 || keyi >= nkeys)
		return;
	base = steps_of(&keys[keyi], &n);
	if (n <= 0 || step < 0 || step >= n)
		return;
	for (i = step; i + 1 < n; i++)
		memcpy(base + i * 48, base + (i + 1) * 48, 48);
	set_count(&keys[keyi], n - 1);
	clamp_step();
	focus = 1;
	dirty = 1;
}

static int known_key(const char *name)
{
	int i;

	for (i = 0; i < NKEYS_CAT; i++) {
		if (!strcmp(keycatalog[i].name, name))
			return 1;
	}
	return 0;
}

static void choose_key(const char *name)
{
	int i;

	for (i = 0; i < nkeys; i++) {
		if (strcmp(keys[i].name, name) != 0)
			continue;
		if (rebind && i != keyi) {
			ut_log("daeboard: %s already has its own steps", name);
			keypick = 0;
			rebind = 0;
			return;
		}
		keyi = i;
		step = 0;
		keypick = 0;
		rebind = 0;
		return;
	}
	if (rebind) {
		if (keyi < 0 || keyi >= nkeys)
			return;
		snprintf(keys[keyi].name, sizeof keys[keyi].name, "%s", name);
		dirty = 1;
		keypick = 0;
		rebind = 0;
		return;
	}
	if (nkeys >= DBE_KEYS)
		return;
	memset(&keys[nkeys], 0, sizeof keys[nkeys]);
	snprintf(keys[nkeys].name, sizeof keys[nkeys].name, "%s", name);
	keyi = nkeys++;
	step = 0;
	side = 0;
	focus = 1;
	dirty = 1;
	keypick = 0;
}

static void delete_key(void)
{
	int i;

	if (keyi < 0 || keyi >= nkeys)
		return;
	for (i = keyi; i + 1 < nkeys; i++)
		keys[i] = keys[i + 1];
	nkeys--;
	if (keyi >= nkeys)
		keyi = nkeys - 1;
	if (keyi < 0)
		keyi = 0;
	step = 0;
	dirty = 1;
}

static void add_key(void)
{
	rebind = 0;
	keypick = 1;
	picker = 0;
}

static void change_key(void)
{
	if (keyi < 0 || keyi >= nkeys)
		return;
	rebind = 1;
	keypick = 1;
	picker = 0;
}

static void add_step_text(const char *text)
{
	dbe_key *k;
	int *pn;
	char *dest;

	if (keyi < 0 || keyi >= nkeys)
		return;
	k = &keys[keyi];
	pn = side == 0 ? &k->n_down : &k->n_up;
	if (*pn >= DBE_STEPS)
		return;
	dest = side == 0 ? k->down[*pn] : k->up[*pn];
	snprintf(dest, 48, "%s", text);
	step = (*pn)++;
	focus = 1;
	dirty = 1;
}

static void apply_preset(int which)
{
	const char *lines[4];
	int n = 0, i;

	if (keyi < 0 || keyi >= nkeys)
		return;
	switch (which) {
	case 0:
		lines[0] = "brightness 0 120";
		lines[1] = "brightness 3 120";
		lines[2] = "brightness 0 120";
		lines[3] = "brightness 3 120";
		n = 4;
		break;
	case 1:
		lines[0] = "breathe 0";
		n = 1;
		break;
	case 2:
		lines[0] = "color ff0000 0";
		n = 1;
		break;
	case 3:
		lines[0] = "fade 2000";
		n = 1;
		break;
	case 4:
		lines[0] = "color 000000 0";
		n = 1;
		break;
	case 5:
		lines[0] = "color ccfffe 0";
		n = 1;
		break;
	case 6:
		lines[0] = "color ffffff 80";
		lines[1] = "color 000000 80";
		lines[2] = "color ffffff 80";
		n = 3;
		break;
	case 7:
		lines[0] = "brightness 0 60";
		lines[1] = "brightness 3 60";
		lines[2] = "brightness 0 60";
		lines[3] = "brightness 3 60";
		n = 4;
		break;
	case 8:
		lines[0] = "color ff8800 0";
		n = 1;
		break;
	case 9:
		lines[0] = "color ffffff 100";
		lines[1] = "fade 400";
		n = 2;
		break;
	case 10:
		lines[0] = "brightness 1 0";
		n = 1;
		break;
	case 11:
		lines[0] = "brightness 3 0";
		n = 1;
		break;
	case 12:
		lines[0] = "fade 400";
		n = 1;
		break;
	default:
		lines[0] = "fade 5000";
		n = 1;
		break;
	}
	for (i = 0; i < n; i++)
		add_step_text(lines[i]);
}

static void draw_swatch(struct ncplane *pl, int x, int y, unsigned rgb, int id)
{
	uint64_t ch = 0;

	ncchannels_set_fg_rgb(&ch, rgb);
	ncchannels_set_bg_rgb(&ch, rgb);
	ncplane_set_channels(pl, ch);
	ncplane_putstr_yx(pl, y, x, "██");
	ncplane_set_styles(pl, NCSTYLE_NONE);
	if (id)
		tgt_register(x, y, 2, 1, TGT(TGT_PANEL_DAEBOARD, id));
}

void editor_daeboard_open(void)
{
	load_file();
	keyi = 0;
	side = 0;
	step = 0;
	focus = 1;
	typing_ctron = 0;
	picker = 0;
	keypick = 0;
	g_ui.db_overlay = true;
}

static void step_label(const char *slot, char *label, int ln, char *value, int vn, char *hex)
{
	int op = op_index(slot);
	int ms = last_int(slot);

	hex[0] = 0;
	if (op == 0) {
		const char *p = strchr(slot, ' ');
		int i = 0;
		snprintf(label, (size_t)ln, "color");
		if (p) {
			p++;
			while (*p && *p != ' ' && i < 6)
				hex[i++] = *p++;
			hex[i] = 0;
		}
		snprintf(value, (size_t)vn, "%s   %d ms", hex[0] ? hex : "------", ms);
	} else if (op == 1) {
		int bri = 0;
		sscanf(slot, "brightness %d", &bri);
		snprintf(label, (size_t)ln, "brightness");
		snprintf(value, (size_t)vn, "%d   %d ms", bri, ms);
	} else if (op == 2) {
		snprintf(label, (size_t)ln, "breathe");
		snprintf(value, (size_t)vn, ms ? "%d ms" : "hold", ms);
	} else if (op == 3) {
		snprintf(label, (size_t)ln, "fade");
		snprintf(value, (size_t)vn, "%d ms  → rest", ms);
	} else {
		snprintf(label, (size_t)ln, "rest");
		snprintf(value, (size_t)vn, "saved color");
	}
}

void editor_daeboard_draw(struct ncplane *pl, const rect_t *r)
{
	const palette_t *pal = ui_palette(g_prefs.theme);
	int x = r->x + 2;
	int w = r->w - 4;
	int y = r->y + 1;
	int i;
	char line[160];
	dbe_key *k;
	static const char *const presets[] = {
		" Blink ", " Breathe ", " Red ", " Fade ", " Off ", " Ice ",
		" Pulse ", " Strobe ", " Amber ", " Flash ", " Dim ", " Full ",
		" Fast ", " Slow "
	};
	ui_btndef_t top[4];
	static time_t probed;
	static int up;
	time_t now = time(NULL);

	poll_key_capture();
	ui_window(pl, r, keypick ? "PRESS A KEY" : "DAEBOARD");
	if (now != probed) {
		up = db_up();
		probed = now;
	}
	top[0] = (ui_btndef_t){ " Start ", up, TGT(TGT_PANEL_DAEBOARD, ID_START) };
	top[1] = (ui_btndef_t){ " Stop ", 0, TGT(TGT_PANEL_DAEBOARD, ID_STOP) };
	top[2] = (ui_btndef_t){ " Install ", 0, TGT(TGT_PANEL_DAEBOARD, ID_INSTALL) };
	top[3] = (ui_btndef_t){ " Save ", dirty, TGT(TGT_PANEL_DAEBOARD, ID_SAVE) };
	ui_btn_row(pl, y, x, w, top, 4);
	snprintf(line, sizeof line, "%s", up ? "running" : "stopped");
	ui_putln(pl, x + 42, y, w - 42, line, up ? pal->accent : pal->muted, true);
	y++;
	{
		char path[200];
		db_binds_path(path, (int)sizeof path);
		ui_putln(pl, x, y, w, path, pal->muted, false);
	}
	y++;
	{
		ui_btndef_t prow[8];
		for (i = 0; i < 8; i++)
			prow[i] = (ui_btndef_t){ presets[i], false, TGT(TGT_PANEL_DAEBOARD, ID_PRESET + i) };
		ui_btn_row(pl, y, x, w, prow, 8);
	}
	y++;
	{
		ui_btndef_t prow[6];
		for (i = 0; i < 6; i++)
			prow[i] = (ui_btndef_t){ presets[8 + i], false, TGT(TGT_PANEL_DAEBOARD, ID_PRESET + 8 + i) };
		ui_btn_row(pl, y, x, w, prow, 6);
	}
	y++;
	{
		ui_btndef_t add[5] = {
			{ " + Color ", false, TGT(TGT_PANEL_DAEBOARD, ID_ADD_COLOR) },
			{ " + Fade ", false, TGT(TGT_PANEL_DAEBOARD, ID_ADD_FADE) },
			{ " + Breathe ", false, TGT(TGT_PANEL_DAEBOARD, ID_ADD_BREATHE) },
			{ " Cut step ", false, TGT(TGT_PANEL_DAEBOARD, ID_CUT) },
			{ " Add key ", false, TGT(TGT_PANEL_DAEBOARD, ID_ADD_KEY) },
		};
		ui_btndef_t keys_btns[2] = {
			{ " Change key ", false, TGT(TGT_PANEL_DAEBOARD, ID_CHANGE_KEY) },
			{ " Del key ", false, TGT(TGT_PANEL_DAEBOARD, ID_DEL_KEY) },
		};
		ui_btn_row(pl, y, x, w, add, 5);
		y++;
		ui_btn_row(pl, y, x, w, keys_btns, 2);
	}
	y++;
	{
		int colw = (w - 18) / 2;
		int y0 = y;
		int maxr = r->y + r->h - 4 - y0;
		if (colw < 18)
			colw = 18;
		vis_rows = maxr - 1;
		if (vis_rows < 1)
			vis_rows = 1;
		ui_btn(pl, x + 18, y0 - 0, side == 0 ? " DOWN " : " down ",
		       side == 0, false, TGT(TGT_PANEL_DAEBOARD, ID_SIDE_DOWN));
		ui_btn(pl, x + 18 + colw, y0, side == 1 ? " UP " : " up ",
		       side == 1, false, TGT(TGT_PANEL_DAEBOARD, ID_SIDE_UP));
		y0++;
		for (i = 0; i < nkeys && i < maxr; i++) {
			ui_row(pl, x, y0 + i, 16, keys[i].name,
			       known_key(keys[i].name) ? "" : "ignored", i == keyi);
			tgt_register(x, y0 + i, 16, 1, TGT(TGT_PANEL_DAEBOARD, ID_KEY + i));
		}
		if (keyi >= 0 && keyi < nkeys) {
			int col;
			k = &keys[keyi];
			for (col = 0; col < 2; col++) {
				int cx = x + 18 + col * colw;
				int saved = side;
				int count = 0;
				char *base;
				side = col;
				base = steps_of(k, &count);
				(void)base;
				for (i = scr[col]; i < count && i < scr[col] + vis_rows; i++) {
					char label[24], value[48], hex[8];
					int sel = (saved == col && i == step);
					const char *slot;
					unsigned rgb = 0x334455u;
					int idb = col ? COL : 0;
					side = col;
					slot = step_at(k, i);
					step_label(slot, label, (int)sizeof label, value, (int)sizeof value, hex);
					if (hex[0])
						sscanf(hex, "%x", &rgb);
					ui_row(pl, cx, y0 + (i - scr[col]), colw - 6, label, value, sel);
					tgt_register(cx, y0 + (i - scr[col]), colw - 8, 1,
						     TGT(TGT_PANEL_DAEBOARD, ID_STEP + idb + i));
					if (hex[0])
						draw_swatch(pl, cx + 1, y0 + (i - scr[col]), rgb, ID_HEX + idb + i);
					ui_btn(pl, cx + colw - 5, y0 + (i - scr[col]), "^", false, false,
					       TGT(TGT_PANEL_DAEBOARD, ID_MVU + idb + i));
					ui_btn(pl, cx + colw - 3, y0 + (i - scr[col]), "v", false, false,
					       TGT(TGT_PANEL_DAEBOARD, ID_MVD + idb + i));
				}
				if (count == 0)
					ui_row(pl, cx, y0, colw - 6, "(empty)", "", false);
				side = saved;
			}
		}
	}
	if (keyi < 0 || keyi >= nkeys)
		return;
	k = &keys[keyi];
	y = r->y + r->h - 3;
	if (focus == 3) {
		snprintf(line, sizeof line, "hex %s_", typed.buf);
		ui_putln(pl, x, y, w, line, pal->accent, true);
	} else {
		snprintf(line, sizeof line, "ctron  %s", k->ctron[0] ? k->ctron : "—");
		ui_row(pl, x, y, w - 4, "command", line, focus == 2);
	}
	y++;
	ui_putln(pl, x, y, w,
		 "a add color on the lit side   Change key keeps the steps   Del key removes it   Del cuts a step",
		 pal->muted, false);

	if (picker) {
		rect_t box = { r->x + r->w - 36, r->y + 3, 34, 16 };
		int sy;
		if (box.x < r->x + 2)
			box.x = r->x + 2;
		ui_window(pl, &box, "COLORS");
		sy = box.y + 1;
		for (i = 0; i < 9; i++) {
			int col = i % 3;
			int row = i / 3;
			int sx = box.x + 2 + col * 10;
			int yy = sy + row * 2;
			draw_swatch(pl, sx, yy, swatches[i].rgb, ID_SWATCH + i);
			ui_putln(pl, sx + 3, yy, 8, swatches[i].name, pal->text, false);
			tgt_register(sx, yy, 10, 1, TGT(TGT_PANEL_DAEBOARD, ID_SWATCH + i));
		}
		ui_putln(pl, box.x + 2, box.y + box.h - 2, box.w - 4,
			 "click a color   esc back", pal->muted, false);
	}
	if (keypick) {
		rect_t box = { r->x + 4, r->y + 4, r->w - 8, r->h - 8 };
		int i2, cx, cy;
		ui_window(pl, &box, "WHICH KEY");
		cx = box.x + 2;
		cy = box.y + 1;
		for (i2 = 0; i2 < NKEYS_CAT; i2++) {
			char lab[16];
			int len;
			snprintf(lab, sizeof lab, " %s ", keycatalog[i2].name);
			len = (int)strlen(lab);
			if (cx + len > box.x + box.w - 2) {
				cx = box.x + 2;
				cy++;
				if (cy >= box.y + box.h - 2)
					break;
			}
			ui_btn(pl, cx, cy, lab, false, false,
			       TGT(TGT_PANEL_DAEBOARD, ID_PICKKEY + i2));
			cx += len + 1;
		}
		ui_putln(pl, box.x + 2, box.y + box.h - 2, box.w - 4,
			 "click the key   esc back", pal->muted, false);
	}
}

static void clamp_step(void)
{
	int n = 0;
	if (keyi < 0 || keyi >= nkeys) {
		step = 0;
		return;
	}
	steps_of(&keys[keyi], &n);
	if (n == 0)
		step = 0;
	else if (step >= n)
		step = n - 1;
	if (step < 0)
		step = 0;
}

void editor_daeboard_key(uint32_t key)
{
	dbe_key *k;
	int n = 0;
	char *slot;

	if (focus == 3) {
		if (key == NCKEY_ESC) {
			focus = 1;
			return;
		}
		if (typing_ctron == 3) {
			if (key == NCKEY_ENTER || key == '\r' || key == '\n') {
				if (keyi >= 0 && keyi < nkeys && typed.len == 6) {
					int nstep = 0;
					steps_of(&keys[keyi], &nstep);
					if (step < nstep)
						paint_color(step_at(&keys[keyi], step), typed.buf);
				}
				focus = 1;
				return;
			}
			if ((key >= '0' && key <= '9') || (key >= 'a' && key <= 'f') ||
			    (key >= 'A' && key <= 'F') || key == NCKEY_BACKSPACE ||
			    key == 127 || key == 8)
				tin_key(&typed, key);
			return;
		}
		if (key == NCKEY_ENTER || key == '\r' || key == '\n') {
			if (keyi >= 0 && keyi < nkeys) {
				if (typing_ctron == 1) {
					snprintf(keys[keyi].ctron, sizeof keys[keyi].ctron, "%s", typed.buf);
				} else if (typing_ctron == 2) {
					if (typed.buf[0])
						snprintf(keys[keyi].name, sizeof keys[keyi].name, "%s", typed.buf);
				} else if (step >= 0) {
					steps_of(&keys[keyi], &n);
					if (step < n)
						snprintf(step_at(&keys[keyi], step), 48, "%s", typed.buf);
				}
				dirty = 1;
			}
			focus = typing_ctron ? 2 : 1;
			return;
		}
		tin_key(&typed, key);
		return;
	}

	if (keypick && key != NCKEY_ESC)
		return;
	if (key == NCKEY_ESC) {
		if (keypick) {
			keypick = 0;
			return;
		}
		if (picker) {
			picker = 0;
			return;
		}
		g_ui.db_overlay = false;
		return;
	}
	if (key == 'p' || key == 'P') {
		picker = !picker;
		return;
	}
	if (key == NCKEY_UP) {
		move_step(-1);
		return;
	}
	if (key == NCKEY_DOWN) {
		move_step(1);
		return;
	}
	if (key == 'n' || key == 'N') {
		add_key();
		return;
	}
	if (key == NCKEY_DEL || key == 'x' || key == 'X') {
		if (focus == 0)
			delete_key();
		else
			cut_step();
		return;
	}
	if (key == '1') {
		db_start();
		return;
	}
	if (key == '2') {
		db_quit();
		return;
	}
	if (key == '3') {
		db_install();
		return;
	}
	if (key == 'w' || key == 'W') {
		save_file();
		return;
	}
	if (key == '\t') {
		if (focus == 0)
			focus = 1;
		else if (focus == 1) {
			side ^= 1;
			step = 0;
		} else
			focus = 0;
		clamp_step();
		return;
	}

	if (focus == 0) {
		if (key == 'j' || key == NCKEY_DOWN) {
			if (nkeys)
				keyi = (keyi + 1) % nkeys;
			step = 0;
			return;
		}
		if (key == 'k' || key == NCKEY_UP) {
			if (nkeys)
				keyi = (keyi + nkeys - 1) % nkeys;
			step = 0;
			return;
		}
	}

	if (keyi < 0 || keyi >= nkeys)
		return;
	k = &keys[keyi];
	slot = NULL;
	steps_of(k, &n);
	if (n > 0 && step < n)
		slot = step_at(k, step);

	if (key == 'j') {
		focus = 1;
		if (n)
			step = (step + 1) % n;
		see_step();
		return;
	}
	if (key == 'k') {
		focus = 1;
		if (n)
			step = (step + n - 1) % n;
		see_step();
		return;
	}
	if (key == 'a' || key == 'A') {
		int *pn = side == 0 ? &k->n_down : &k->n_up;
		char *dest = side == 0 ? k->down[*pn] : k->up[*pn];
		if (*pn < DBE_STEPS) {
			snprintf(dest, 48, "color ffffff 200");
			step = (*pn)++;
			focus = 1;
			dirty = 1;
		}
		return;
	}
	if (key == 'x' || key == 'X') {
		cut_step();
		return;
	}
	if ((key == 'h' || key == NCKEY_LEFT) && slot) {
		cycle_step(slot);
		dirty = 1;
		return;
	}
	if ((key == 'l' || key == NCKEY_RIGHT) && slot) {
		cycle_step(slot);
		dirty = 1;
		return;
	}
	if (key == '-' && slot) {
		bump_step(slot, -50);
		dirty = 1;
		return;
	}
	if ((key == '=' || key == '+') && slot) {
		bump_step(slot, 50);
		dirty = 1;
		return;
	}
	if ((key == 'e' || key == 'E') && slot) {
		focus = 3;
		typing_ctron = 0;
		tin_set(&typed, slot);
		return;
	}
	if (key == 'c' || key == 'C') {
		focus = 3;
		typing_ctron = 1;
		tin_set(&typed, k->ctron);
		return;
	}
	if (key == 'r' || key == 'R') {
		focus = 3;
		typing_ctron = 2;
		tin_set(&typed, k->name);
		return;
	}
	if (key == 'f' || key == 'F') {
		add_step_text("fade 2000");
		return;
	}
}

static int doubled(int id)
{
	struct timespec ts;
	static long long last_ms;
	static int last_id = -1;
	long long ms;
	int yes;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	ms = (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
	yes = (id == last_id && ms - last_ms < 450);
	last_ms = ms;
	last_id = id;
	return yes;
}

void editor_daeboard_act(int id)
{
	int dbl = doubled(id);

	if (keypick) {
		if (id >= ID_PICKKEY && id < ID_PICKKEY + NKEYS_CAT)
			choose_key(keycatalog[id - ID_PICKKEY].name);
		return;
	}
	if (picker) {
		if (id >= ID_SWATCH && id < ID_SWATCH + 9 && keyi >= 0 && keyi < nkeys) {
			int n = 0;
			char *slot;
			steps_of(&keys[keyi], &n);
			if (n == 0)
				add_step_text("color ffffff 0");
			steps_of(&keys[keyi], &n);
			if (step < n) {
				slot = step_at(&keys[keyi], step);
				paint_color(slot, swatches[id - ID_SWATCH].hex);
			}
			picker = 0;
		}
		return;
	}
	if (id == ID_START) { db_start(); return; }
	if (id == ID_STOP) { db_quit(); return; }
	if (id == ID_INSTALL) { db_install(); return; }
	if (id == ID_SAVE) { save_file(); return; }
	if (id == ID_SIDE_DOWN) { side = 0; step = 0; focus = 1; return; }
	if (id == ID_SIDE_UP) { side = 1; step = 0; focus = 1; return; }
	if (id == ID_ADD_COLOR) { add_step_text("color ffffff 200"); picker = 1; return; }
	if (id == ID_ADD_FADE) { add_step_text("fade 2000"); return; }
	if (id == ID_ADD_BREATHE) { add_step_text("breathe 0"); return; }
	if (id == ID_CUT) { cut_step(); return; }
	if (id == ID_ADD_KEY) { add_key(); return; }
	if (id == ID_DEL_KEY) { delete_key(); return; }
	if (id == ID_CHANGE_KEY) { change_key(); return; }
	if (id >= ID_PRESET && id < ID_PRESET + 14) { apply_preset(id - ID_PRESET); return; }
	if (id >= ID_KEY && id < ID_KEY + DBE_KEYS) {
		keyi = id - ID_KEY;
		if (keyi >= nkeys)
			keyi = nkeys - 1;
		step = 0;
		focus = 0;
		return;
	}
	if (id >= ID_MVU && id < ID_MVU + COL + DBE_STEPS) {
		side = id >= ID_MVU + COL;
		step = id - ID_MVU - (side ? COL : 0);
		move_step(-1);
		return;
	}
	if (id >= ID_MVD && id < ID_MVD + COL + DBE_STEPS) {
		side = id >= ID_MVD + COL;
		step = id - ID_MVD - (side ? COL : 0);
		move_step(1);
		return;
	}
	if (id >= ID_HEX && id < ID_HEX + COL + DBE_STEPS) {
		side = id >= ID_HEX + COL;
		step = id - ID_HEX - (side ? COL : 0);
		focus = 1;
		if (dbl && keyi >= 0 && keyi < nkeys) {
			char label[24], value[48], hex[8];
			int nstep = 0;
			steps_of(&keys[keyi], &nstep);
			if (step < nstep) {
				step_label(step_at(&keys[keyi], step), label, 24, value, 48, hex);
				focus = 3;
				typing_ctron = 3;
				tin_clear(&typed);
				if (hex[0])
					tin_set(&typed, hex);
			}
		} else {
			picker = 1;
		}
		return;
	}
	if (id >= ID_STEP && id < ID_STEP + COL + DBE_STEPS) {
		side = id >= ID_STEP + COL;
		step = id - ID_STEP - (side ? COL : 0);
		focus = 1;
		return;
	}
}

