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

/* buf holds the longest editable field (mode steps = MODE_STEPS_MAX);
 * tin_* code keys off sizeof, so instances just cost a bit more. */
typedef struct { char buf[MODE_STEPS_MAX]; size_t len; } tinput_t;

typedef struct ui_ctx {
    hw_state_t *hw;
    bool running;

    focus_t focus;
    ws_view_t ws_view;
    bool settings_overlay; /* ESC: fullscreen settings over everything */
    bool db_overlay;       /* dedicated daeboard editor / installer */
    rect_t rc_overlay;     /* settings overlay rectangle (below top bar) */

    rect_t rc_prof, rc_ctl, rc_ws, rc_telem;

    /* snapshots panel (ex-PROFILES) */
    char profs[MAX_PROFILES][PROFILE_NAME_MAX];
    int prof_n, prof_sel;
    int prof_top;          /* first visible list entry (scroll) */
    int prof_input;        /* typing mode: 0 none, 1 save, 2 rename, 3 note */
    tinput_t prof_name;    /* save/rename name field */
    tinput_t prof_note;    /* note field */
    profile_meta_t prof_meta;   /* cached parse of the selected snapshot */
    int prof_meta_sel;     /* selection the cache was parsed for */
    bool prof_meta_ok;

    /* controls panel */
    int ctl_sel;
    int ctl_mode_idx, ctl_prof_idx, ctl_fan_idx, ctl_hz_idx, ctl_kbd_idx;
    int ctl_bat;
    mode_def_t modes[MODES_MAX];
    int mode_n;
    /* applied-mode state for the Mode row: "" = none this session,
     * else the name plus a snapshot of the fields its steps touch
     * (draw-time drift check marks "name*"). ctl_mode_pick latches
     * after the user rotates/clicks the picker: only then does the
     * row show the candidate after the arrow — startup shows the bare
     * state. */
    char ctl_applied[MODE_NAME_MAX];
    unsigned ctl_mode_mask;
    mode_snap_t ctl_snap;
    bool ctl_mode_pick;

    /* fan editor */
    int fe_gpu, fe_sel, fe_input; /* fe_input: 0 none, 1 temp, 2 pwm */
    bool fe_grid;                 /* 'i': value guides on the graph */
    tinput_t fe_x, fe_y;
    rect_t fe_graph;

    /* power view */
    int pw_sel;
    int pw_preset_sel;     /* 0..2: cursor on the Q45/B60/P80 button row */
    /* staged edits — h/arrows only mutate these; nothing is written
     * until Apply (Enter / w / button) */
    int pwv_spl, pwv_sppt, pwv_fppt;   /* W */
    int pwv_gpuclock, pwv_mhz;         /* MHz; gpuclock 0 = driver default */
    int pwv_profile;                   /* hw_profile_t */
    int pwv_asusd_ac, pwv_asusd_bat;   /* -1 off, 0..2 profile */
    int pwv_epp;                       /* hw_epp_t */
    bool pwv_panel_od, pwv_cpuboost, pwv_ppt_off;
    bool pw_dirty;
    bool pw_quit_warned;   /* q pressed once with staged edits pending */
    unsigned pw_touched;   /* PW_T_* bits: fields the user actually edited */
    char pw_msg[160];      /* apply toast: what changed */
    long pw_msg_ms;        /* CLOCK_MONOTONIC ms when set, 0 = none */
    bool pw_msg_fail;      /* red variant (some writes failed) */

    /* per-core frequency editor overlay (POWER 'c'); cf_sel is a grid
     * position, cf_staged is indexed by real kernel cpu id */
    bool cf_overlay;
    int cf_sel;
    int cf_staged[HW_CPU_MAX];
    tinput_t cf_input;
    int cf_input_active;
    tinput_t pw_input;     /* exact-value typing ('t' on a value row) */
    bool pw_typing;

    /* light view */
    int lt_sel, lt_eff, lt_col;
    tinput_t lt_hex;
    int lt_hex_active;

    /* settings view */
    int set_sel;
    int set_mode_sel;
    int set_mode_top;      /* first visible modes.ini entry (scroll) */
    int md_field; /* mode editor field: 0 name, 1 steps */
    tinput_t md_name, md_steps;
} ui_ctx_t;

extern ui_ctx_t g_ui;

/* pw_touched bits — a field is only written on Apply when the user
 * edited it (or its live value is known and differs). Keeps a stale
 * nb-wmi read (0 W) from turning defaults into unwanted writes. */
enum {
    PW_T_PROFILE   = 1 << 0,
    PW_T_EPP       = 1 << 1,
    PW_T_PPT       = 1 << 2,  /* SPL/SPPT/FPPT as one triple */
    PW_T_PPT_OFF   = 1 << 3,
    PW_T_GPUCLOCK  = 1 << 4,
    PW_T_PANEL_OD  = 1 << 6,
    PW_T_CPUBOOST  = 1 << 7,
    PW_T_CPUFREQ   = 1 << 8,
    PW_T_AC_AUTO   = 1 << 9,
    PW_T_BAT_AUTO  = 1 << 10,
};

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
    TGT_PANEL_COREFREQ,
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
/* Record the just-applied mode as the CONTROLS Mode row state: copies
 * the name and snapshots the fields its steps touch. Call right after a
 * successful mode_apply (both the controls row and the settings
 * overlay path). */
void ctl_capture_mode(const char *name, const char *steps);

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

/* Per-core frequency editor overlay: opened from the POWER view ('c' /
 * Cores button), swallows keys while up (daeboard pattern). */
void editor_corefreq_open(void);
void editor_corefreq_draw(struct ncplane *n, const rect_t *r);
void editor_corefreq_key(uint32_t key);
void editor_corefreq_act(int id);

void editor_daeboard_open(void);
void editor_daeboard_draw(struct ncplane *n, const rect_t *r);
void editor_daeboard_key(uint32_t key);
void editor_daeboard_act(int id);

void editor_fan_draw(struct ncplane *n, const rect_t *r);
void editor_fan_key(uint32_t key);
void editor_fan_act(int id, int mx, int my);
/* Fan-editor click targets: ACT_FE_* ids live in [ACT_FE_BASE,
 * ACT_FE_END) and are forwarded from panel_workspace_act (they are
 * registered under TGT_PANEL_WORKSPACE but must not collide with the
 * workspace's own ACT_WS_* ids). */
#define ACT_FE_BASE 100
#define ACT_FE_END  160
/* Graph rectangle after a draw pass (mouse hit testing). */
const rect_t *editor_fan_graph(void);

/* Shared workspace helpers. */
void ws_set_view(ws_view_t v);
void ws_start_mode_edit(const mode_def_t *m); /* NULL = new mode */

/* Draw msg on the telemetry log row and render immediately — long-op
 * ("applying...") feedback before a blocking call. No-op outside TUI. */
void ui_flash(const char *msg);

#endif /* CTRON_UI_INTERNAL_H */
