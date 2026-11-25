#ifndef CTRON_UI_INTERNAL_H
#define CTRON_UI_INTERNAL_H

/* Shared plumbing for the v0.12.0 Konsept C TUI:
 *   - Top 1-line header + live summary (inline tabs on narrow terminals)
 *   - Slim left rail (VIEWS 1..5 + compact LIVE summary, collapses < 96 cols)
 *   - Full-height right workspace hosting the active view
 *   - Unified 2-row footer (status/progress/last log + contextual keybar)
 *   - Floating single-line modals (Settings ',', Log 'L', Help '?', Cores 'c',
 *     Fan Presets 'm', Daeboard 'b') — Esc always backs out or closes. */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <stdatomic.h>

#include <notcurses/notcurses.h>

#include "../fan.h"
#include "../hw.h"
#include "../modes.h"
#include "../profile.h"
#include "../settings.h"
#include "../tinput.h"

typedef struct { int x, y, w, h; } rect_t;

typedef enum {
    VIEW_POWER = 0,
    VIEW_FANS,
    VIEW_SYSTEM,     /* Display, Battery & Lighting */
    VIEW_SNAPSHOTS,
    VIEW_TELEM,
    VIEW_COUNT
} ui_view_t;

extern const char *const VIEW_NAMES[VIEW_COUNT];

typedef struct ui_ctx {
    hw_state_t *hw;
    bool running;

    /* background apply worker (the only thread besides the UI one):
     * heavy applies (snapshot / POWER / SYSTEM / mode) run their WRITE
     * phase off-thread so the TUI keeps rendering; the UI thread skips
     * hw_refresh while busy (single-writer) and shows a status line. */
    atomic_bool apply_busy;
    atomic_bool quit_pending;   /* second q while busy: quit when done */
    bool apply_spawned;         /* a thread was created and needs join */
    bool apply_warned;          /* one-shot hints while busy */
    pthread_t apply_tid;
    int apply_kind;             /* APPLY_* below */
    int apply_fails;            /* worker -> finalize result */
    bool apply_ok;
    char apply_what[40];
    char apply_arg[48];         /* snapshot/mode name or index as text */
    char apply_sum[192];        /* diff summary or error text */
    long apply_start_ms;        /* CLOCK_MONOTONIC, 0 = none */
    hw_state_t *apply_hw;       /* real hw the worker writes; while busy,
                                 * hw points at a frozen copy */

    /* primary view & floating overlays */
    ui_view_t view;
    ui_view_t prev_view;        /* remembered for 't' toggle */
    bool settings_overlay;      /* ',': preferences + CLI shortcuts modal */
    bool log_overlay;           /* 'L': floating event log viewer (24 entries) */
    int log_scroll;             /* top visible log index (0 = newest) */
    bool help_overlay;          /* '?': keymap help modal */
    bool db_overlay;            /* 'b': daeboard editor / installer */
    bool fe_presets_overlay;    /* 'm': fan presets modal */
    bool pw_in_cores;           /* true when focus is in inline CORE LIMITS pane */

    /* telemetry view (5th tab: grid + detail pages) */
    int telem_sel;              /* grid cursor (section idx) */
    int telem_detail;           /* -1 grid, else section shown full-page */
    bool telem_since;           /* detail stats: 60 s window vs since-reset */
    long telem_ack_ms;          /* 'r' reset flash: CLOCK_MONOTONIC ms, 0 none */

    rect_t rc_rail;             /* left rail (w == 0 when collapsed < 96 cols) */
    rect_t rc_ws;               /* main workspace area */
    rect_t rc_overlay;          /* floating modal rectangle */

    /* snapshots & saved fan curves view (VIEW_SNAPSHOTS) */
    char profs[MAX_PROFILES][PROFILE_NAME_MAX];
    int prof_n, prof_sel;
    int prof_top;
    char fcurves[MAX_FCURVES][FCURVE_NAME_MAX];
    int fc_n, fc_sel;
    int fc_top;
    bool prof_in_fcurves;       /* false = top SNAPSHOTS, true = bottom FAN CURVES */
    int prof_input;             /* 0 none, 1 save, 2 rename, 3 note */
    int prof_del_arm;           /* double-d confirm: armed sel, or -1 */
    int prof_upd_arm;           /* double-u confirm: overwrite with live, or -1 */
    tinput_t prof_name;
    tinput_t prof_note;
    profile_meta_t prof_meta;
    int prof_meta_sel;
    bool prof_meta_ok;
    bool prof_in_edit;          /* true when focus is in right-hand Editor */
    int prof_edit_sel;          /* 0..SF_COUNT-1 field row inside Snapshot Editor */
    int prof_edit_top;          /* scroll offset for Snapshot Editor */
    bool prof_edit_dirty;       /* unsaved edits in prof_edit */
    profile_meta_t prof_edit;   /* editable working copy of selected snapshot */
    fcurve_meta_t fc_edit;      /* editable working copy of selected fan curve */
    int fc_edit_sel;            /* 0..17 row inside Fan Curve Editor */
    int fc_edit_top;            /* scroll offset for Fan Curve Editor */
    bool fc_edit_dirty;         /* unsaved edits in fc_edit */
    tinput_t prof_val_in;       /* exact value input inside right-hand Editor */
    bool prof_val_typing;       /* true while typing exact field value */
    bool prof_in_cores;         /* Snapshot Editor shows the per-core grid */
    int prof_cf_staged[HW_CPU_MAX]; /* the grid's snapshot pool: kept apart
                                     * from cf_staged so a snapshot edit is
                                     * never a pending POWER write */
    profile_meta_t prof_core_undo; /* prof_edit as it was when the grid opened */

    /* user shortcut modes (POWER Quick mode row + Settings CLI SHORTCUTS) */
    mode_def_t modes[MODES_MAX];
    int mode_n;
    int ctl_mode_idx;
    char ctl_applied[MODE_NAME_MAX];
    unsigned ctl_mode_mask;
    mode_snap_t ctl_snap;
    bool ctl_mode_pick;

    /* fan editor (VIEW_FANS) */
    int fe_gpu, fe_sel, fe_input; /* fe_input: 0 none, 1 temp, 2 pwm, 3 save name */
    bool fe_grid;                 /* 'i': value guides on the graph */
    tinput_t fe_x, fe_y, fe_save_name;
    char fe_loaded[FCURVE_NAME_MAX]; /* active saved curve label in editor */
    rect_t fe_graph;
    int fe_pr_sel;                /* 0..3 built-ins + 4..4+fc_n-1 saved curves */
    int fe_pr_top;
    bool fe_pr_target_cpu;
    bool fe_pr_target_gpu;

    /* power view (VIEW_POWER) */
    int pw_sel;
    int pw_preset_sel;            /* 0..2: Q45/B60/P80 */
    int pw_top;
    int pwv_spl, pwv_sppt, pwv_fppt;
    int pwv_gpuclock, pwv_mhz;
    int pwv_profile;
    int pwv_asusd_ac, pwv_asusd_bat;
    int pwv_epp;
    bool pwv_cpuboost, pwv_ppt_off;
    bool pw_dirty;
    bool pw_quit_warned;          /* q pressed once with staged edits pending */
    unsigned pw_touched;          /* PW_T_* bits */
    char pw_msg[160];             /* apply toast (shared by POWER & SYSTEM) */
    long pw_msg_ms;               /* CLOCK_MONOTONIC ms, 0 = none */
    bool pw_msg_fail;
    tinput_t pw_input;
    bool pw_typing;

    /* per-core frequency editor overlay (POWER 'c') */
    int cf_sel;
    int cf_staged[HW_CPU_MAX];
    tinput_t cf_input;
    int cf_input_active;

    /* display, battery & lighting view (VIEW_SYSTEM) */
    int sys_sel;
    int sys_top;
    int sys_hz_idx;
    bool sys_panel_od;
    int sys_bat;
    int sys_kbd_idx;
    int lt_eff, lt_col;
    bool sys_dirty;
    unsigned sys_touched;         /* SYS_T_* bits */
    tinput_t sys_bat_input;
    bool sys_bat_typing;
    tinput_t lt_hex;
    int lt_hex_active;

    /* settings overlay (',') */
    int set_sel;
    bool set_in_modes;
    int set_mode_sel;
    int set_mode_top;
    int set_del_arm;              /* double-d confirm: armed set_mode_sel, or -1 */
    int md_field;                 /* -1 closed, 0 name, 1 steps */
    tinput_t md_name, md_steps;
} ui_ctx_t;

extern ui_ctx_t g_ui;

/* pw_touched bits */
enum {
    PW_T_PROFILE   = 1 << 0,
    PW_T_EPP       = 1 << 1,
    PW_T_PPT       = 1 << 2,
    PW_T_PPT_OFF   = 1 << 3,
    PW_T_GPUCLOCK  = 1 << 4,
    PW_T_CPUBOOST  = 1 << 7,
    PW_T_CPUFREQ   = 1 << 8,
    PW_T_AC_AUTO   = 1 << 9,
    PW_T_BAT_AUTO  = 1 << 10,
};

/* sys_touched bits */
enum {
    SYS_T_HZ       = 1 << 0,
    SYS_T_PANEL_OD = 1 << 1,
    SYS_T_BAT      = 1 << 2,
    SYS_T_KBD      = 1 << 3,
    SYS_T_AURA     = 1 << 4,
};

/* ---- palette ----------------------------------------------------------- */

typedef struct {
    bool default_bg;   /* preserve terminal transparency / default bg */
    bool light;        /* light background (inverts active pill fg) */
    uint32_t accent, accent2, warn, ok, err;
    uint32_t bg, card, border, text, muted;
} palette_t;

const palette_t *ui_palette(int theme);
extern const char *const THEME_NAMES[];
#define THEME_COUNT 5

/* ---- draw helpers (single std plane) ------------------------------------ */

uint64_t ui_ch(uint32_t fg, bool sel_bg);
void ui_modal_end(void);
void ui_hline(struct ncplane *n, int x, int y, int w, uint32_t fg);
void ui_vline(struct ncplane *n, int x, int y, int h, uint32_t fg);
void ui_box(struct ncplane *n, const rect_t *r, const char *title, bool focused);
void ui_window(struct ncplane *n, const rect_t *r, const char *title);
void ui_btn(struct ncplane *n, int x, int y, const char *label,
            bool active, bool focused, int id);
typedef struct {
    const char *label;
    bool active;
    int id;
} ui_btndef_t;
int ui_btn_row(struct ncplane *n, int y, int x, int w,
               const ui_btndef_t *btns, int count);
void ui_row(struct ncplane *n, int x, int y, int w,
            const char *label, const char *value, bool selected);
void ui_putln(struct ncplane *n, int x, int y, int w,
              const char *s, uint32_t fg, bool bold);
void ui_trunc(char *s, int maxlen_chars);

typedef struct {
    const char *key;
    const char *label;
    bool hi;
} ui_hint_t;
void ui_keybar(struct ncplane *n, int x, int y, int w,
               const ui_hint_t *hints, int count);

/* ---- mouse targets ------------------------------------------------------ */

#define TGT_PANEL_MASK 0x7f000000
#define TGT_ID_MASK    0x00ffffff
#define TGT(panel, id) (((panel) << 24) | ((id) & 0x00ffffff))

enum {
    TGT_NONE = 0,
    TGT_PANEL_RAIL,
    TGT_PANEL_PROFILES,
    TGT_PANEL_WORKSPACE,
    TGT_PANEL_SETTINGS,
    TGT_PANEL_DAEBOARD,
    TGT_PANEL_COREFREQ,
    TGT_PANEL_FAN_PRESETS,
    TGT_PANEL_TELEM,
    TGT_PANEL_LOG,
};

void tgt_clear(void);
void tgt_register(int x, int y, int w, int h, int encoded);
int  tgt_find(int x, int y);

/* ---- text input (notcurses key bridge over src/tinput.h) ----------------- */

bool tin_is_edit_key(uint32_t key);
bool tin_key(tinput_t *t, uint32_t key);

/* ---- views & panels ------------------------------------------------------ */

void ui_set_view(ui_view_t v);

void panel_profiles_reload(void);
void panel_profiles_draw(struct ncplane *n, const rect_t *r);
void panel_profiles_key(uint32_t key);
void panel_profiles_act(int id);
/* Snapshot-mode core grid (editor_corefreq calls these): reload puts the
 * undo copy back, close folds the staged threads into prof_edit when
 * `keep` and always clears the grid state. No-ops when the grid is shut. */
void panel_profiles_cores_reload(void);
void panel_profiles_cores_close(int keep);

void ctl_capture_mode(const char *name, const char *steps);

void panel_workspace_draw(struct ncplane *n, const rect_t *r);
void panel_workspace_key(uint32_t key);
void panel_workspace_act(int id);

void pw_sync_from_hw(void);
void pw_recompute_dirty(void);
int  pw_staged_count(void);
const char *pw_selected_desc(void);
void pw_apply_writes(hw_state_t *hw, int *fails_out, char *sum, size_t sumn);

void sys_sync_from_hw(void);
void sys_recompute_dirty(void);
int  sys_staged_count(void);
const char *sys_selected_desc(void);
void sys_apply_writes(hw_state_t *hw, int *fails_out, char *sum, size_t sumn);

void panel_telem_full_draw(struct ncplane *n, const rect_t *r);
void panel_telem_full_key(uint32_t key);
void panel_telem_full_act(int id);
void telem_poll(struct hw_state *hw, long now_ms);

void panel_settings_draw(struct ncplane *n, const rect_t *r);
void panel_settings_key(uint32_t key);
void panel_settings_act(int id);
void settings_open(void);
void ws_start_mode_edit(const mode_def_t *m);

void editor_corefreq_open(void);
/* Leaving the active view: drops POWER focus or a snapshot grid (with
 * its unsaved staging — the edit it belongs to is discarded too). */
void editor_corefreq_close(void);
void editor_corefreq_draw(struct ncplane *n, const rect_t *r);
void editor_corefreq_key(uint32_t key);
void editor_corefreq_act(int id);
int  cf_staged_n(void);
void cf_capture_staged(hw_state_t *hw, int ids[HW_CPU_MAX],
                       int vals[HW_CPU_MAX], int *n_out);
int  cf_apply_list(hw_state_t *hw, const int ids[HW_CPU_MAX],
                   const int vals[HW_CPU_MAX], int n, int *fails_out);
void cf_reset_staging(void);

void editor_daeboard_open(void);
void editor_daeboard_close(void);
void editor_daeboard_draw(struct ncplane *n, const rect_t *r);
void editor_daeboard_key(uint32_t key);
void editor_daeboard_act(int id);

void editor_fan_draw(struct ncplane *n, const rect_t *r);
void editor_fan_key(uint32_t key);
void editor_fan_act(int id, int mx, int my);
void editor_fan_sync_bufs(void);
void editor_fan_presets_open(void);
void editor_fan_presets_draw(struct ncplane *n, const rect_t *r);
void editor_fan_presets_key(uint32_t key);
void editor_fan_presets_act(int id);
#define ACT_FE_BASE 100
#define ACT_FE_END  160
const rect_t *editor_fan_graph(void);

/* Cached saved fan curve accessor shared with editor_fan presets modal */
const fcurve_meta_t *panel_profiles_fcurve_meta(int idx);

void ui_flash(const char *msg);

enum { APPLY_SNAPSHOT, APPLY_FAN_CURVE, APPLY_POWER, APPLY_SYSTEM, APPLY_MODE };
void ui_spawn_apply(int kind, const char *what, const char *arg);

#endif /* CTRON_UI_INTERNAL_H */
