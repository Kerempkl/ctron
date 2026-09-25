#ifndef CTRON_UI_INTERNAL_H
#define CTRON_UI_INTERNAL_H

/* Shared plumbing for the TUI panels. Every panel is an independent
 * widget with three entry points (draw / key / mouse action); the layout
 * engine in ui.c only hands rectangles out. Re-arranging the screen later
 * means changing ui_layout(), not the panels. */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <notcurses/notcurses.h>

#include "../fan.h"
#include "../hw.h"
#include "../modes.h"
#include "../profile.h"
#include "../settings.h"

typedef struct { int x, y, w, h; } rect_t;

typedef enum {
    FOC_PROFILES = 0,
    FOC_CONTROLS,
    FOC_WORKSPACE,
    FOC_TELEM,
    FOC_COUNT
} focus_t;

typedef enum {
    WSV_FAN = 0,
    WSV_POWER,
    WSV_LIGHT,
    WSV_HELP,
    WSV_COUNT
} ws_view_t;

typedef struct { char buf[80]; size_t len; } tinput_t;

typedef struct ui_ctx {
    hw_state_t *hw;
    bool running;

    focus_t focus;
    ws_view_t ws_view;
    bool settings_overlay; /* ESC: fullscreen settings over everything */
    bool db_overlay;       /* dedicated daeboard editor / installer */
    rect_t rc_overlay;     /* settings overlay rectangle (below top bar) */

    rect_t rc_prof, rc_ctl, rc_ws, rc_telem;

    /* profiles panel */
    char profs[MAX_PROFILES][PROFILE_NAME_MAX];
    int prof_n, prof_sel;
    tinput_t prof_name;
    bool prof_typing;

    /* controls panel */
    int ctl_sel;
    int ctl_mode_idx, ctl_prof_idx, ctl_fan_idx, ctl_hz_idx, ctl_kbd_idx;
    int ctl_bat;
    mode_def_t modes[MODES_MAX];
    int mode_n;

    /* fan editor */
    int fe_gpu, fe_sel, fe_input; /* fe_input: 0 none, 1 temp, 2 pwm */
    tinput_t fe_x, fe_y;
    rect_t fe_graph;

    /* power view */
    int pw_sel;
    /* staged edits — h/arrows only mutate these; nothing is written
     * until Apply (Enter / w / button) */
    int pwv_spl, pwv_sppt, pwv_fppt;   /* W */
    int pwv_nvboost, pwv_nvtemp, pwv_mhz;
    int pwv_profile;                   /* hw_profile_t */
    int pwv_epp;                       /* hw_epp_t */
    bool pwv_panel_od, pwv_cpuboost, pwv_ppt_off;
    bool pw_dirty;
    bool pw_quit_warned;   /* q pressed once with staged edits pending */

    /* light view */
    int lt_sel, lt_eff, lt_col;
    tinput_t lt_hex;
    int lt_hex_active;

    /* settings view */
    int set_sel;
    int set_mode_sel;
    int md_field; /* mode editor field: 0 name, 1 steps */
    tinput_t md_name, md_steps;
} ui_ctx_t;

extern ui_ctx_t g_ui;

/* ---- palette ----------------------------------------------------------- */

typedef struct {
    uint32_t accent, accent2, bg, card, border, text, muted;
} palette_t;

const palette_t *ui_palette(int theme); /* indexes g_prefs.theme */
extern const char *const THEME_NAMES[];
#define THEME_COUNT 5

/* ---- draw helpers (single std plane) ------------------------------------ */

void ui_box(struct ncplane *n, const rect_t *r, const char *title, bool focused);
/* Floating btop-style window (shadow + double frame + interior fill). */
void ui_window(struct ncplane *n, const rect_t *r, const char *title);
void ui_btn(struct ncplane *n, int x, int y, const char *label,
            bool active, bool focused, int id);
typedef struct {
    const char *label;
    bool active;
    int id;      /* encoded TGT(...) value */
} ui_btndef_t;
/* Draw buttons left-to-right from x with one space between; buttons that
 * do not fit within w columns are skipped. Returns the next free column. */
int ui_btn_row(struct ncplane *n, int y, int x, int w,
               const ui_btndef_t *btns, int count);
void ui_row(struct ncplane *n, int x, int y, int w,
            const char *label, const char *value, bool selected);
void ui_putln(struct ncplane *n, int x, int y, int w,
              const char *s, uint32_t fg, bool bold);
void ui_trunc(char *s, int maxlen_chars);

/* ---- mouse targets ------------------------------------------------------ */

#define TGT_PANEL_MASK 0x7f000000
#define TGT_ID_MASK    0x00ffffff
#define TGT(panel, id) (((panel) << 24) | ((id) & 0x00ffffff))

enum {
    TGT_NONE = 0,
    TGT_PANEL_TOPBAR,
    TGT_PANEL_PROFILES,
    TGT_PANEL_CONTROLS,
    TGT_PANEL_WORKSPACE,
    TGT_PANEL_SETTINGS,
    TGT_PANEL_DAEBOARD,
};

void tgt_clear(void);
void tgt_register(int x, int y, int w, int h, int encoded);
int  tgt_find(int x, int y); /* encoded target or 0 */

/* ---- text input ---------------------------------------------------------- */

void tin_set(tinput_t *t, const char *s);
void tin_clear(tinput_t *t);
/* Returns true when the key was consumed by the input field. */
bool tin_key(tinput_t *t, uint32_t key);

/* ---- panels -------------------------------------------------------------- */

void panel_profiles_draw(struct ncplane *n, const rect_t *r);
void panel_profiles_key(uint32_t key);
void panel_profiles_act(int id);

void panel_controls_draw(struct ncplane *n, const rect_t *r);
void panel_controls_key(uint32_t key);
void panel_controls_act(int id);

void panel_workspace_draw(struct ncplane *n, const rect_t *r);
void panel_workspace_key(uint32_t key);
void panel_workspace_act(int id);
/* (Re)stage power-view values from the live hw state. Unknown/stale
 * reads keep the current staged value. Callers: TUI start, mode/profile
 * apply, power Apply/Revert. */
void pw_sync_from_hw(void);

void panel_telemetry_draw(struct ncplane *n, const rect_t *r);

/* Settings overlay (ESC): fullscreen, covers all panels; the mode editor
 * runs inside it (md_field >= 0). */
void panel_settings_draw(struct ncplane *n, const rect_t *r);
void panel_settings_key(uint32_t key);
void panel_settings_act(int id);

/* Open the settings overlay (from the SET tab / controls row / 's'). */
void settings_open(void);

void editor_daeboard_open(void);
void editor_daeboard_draw(struct ncplane *n, const rect_t *r);
void editor_daeboard_key(uint32_t key);
void editor_daeboard_act(int id);

void editor_fan_draw(struct ncplane *n, const rect_t *r);
void editor_fan_key(uint32_t key);
void editor_fan_act(int id, int mx, int my);
/* Graph rectangle after a draw pass (mouse hit testing). */
const rect_t *editor_fan_graph(void);

/* Shared workspace helpers. */
void ws_set_view(ws_view_t v);
void ws_start_mode_edit(const mode_def_t *m); /* NULL = new mode */

#endif /* CTRON_UI_INTERNAL_H */
