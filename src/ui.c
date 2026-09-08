#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <wchar.h>
#include <locale.h>
#include <ctype.h>
#include "ui.h"
#include "settings.h"
#include "hardware.h"
#include "profile.h"
#include <notcurses/notcurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define WIN_W 52
#define WIN_H 25

typedef enum {
    TAB_PERF = 0,
    TAB_POWER,
    TAB_FAN,
    TAB_AURA,
    TAB_ACV,
    TAB_SLOTS,
    TAB_COUNT
} ui_tab_t;

typedef struct {
    int x1, y1, x2, y2;
    int id;
} click_target_t;

#define MAX_TARGETS 128
static click_target_t s_targets[MAX_TARGETS];
static int s_target_count = 0;

static void clear_targets(void) {
    s_target_count = 0;
}

static void register_target(int x, int y, int w, int h, int id) {
    if (s_target_count < MAX_TARGETS) {
        s_targets[s_target_count].x1 = x;
        s_targets[s_target_count].y1 = y;
        s_targets[s_target_count].x2 = x + w - 1;
        s_targets[s_target_count].y2 = y + h - 1;
        s_targets[s_target_count].id = id;
        s_target_count++;
    }
}

static int find_target(int x, int y) {
    for (int i = 0; i < s_target_count; i++) {
        if (x >= s_targets[i].x1 && x <= s_targets[i].x2 &&
            y >= s_targets[i].y1 && y <= s_targets[i].y2) {
            return s_targets[i].id;
        }
    }
    return -1;
}

#define FAN_TMIN 20
#define FAN_TMAX 105
#define ACT_FAN_GRAPH 399
#define ACT_FAN_SEL_BASE 380 /* 380..387 = point chips */
static int s_gx, s_gy, s_gw, s_gh;
static int s_sel_pt = -1;
static int s_btn1_down = 0;
static struct timespec s_btn1_t;

static void plot_line(struct ncplane *n, int x0, int y0, int x1, int y1, uint64_t ch)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    ncplane_set_channels(n, ch);
    for (;;) {
        if (x0 >= s_gx && x0 < s_gx + s_gw && y0 >= s_gy && y0 < s_gy + s_gh)
            ncplane_putstr_yx(n, y0, x0, "·");
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static int fan_x(const fan_curve_t *fc, int i)
{
    int t = fc->temp_c[i];
    if (t < FAN_TMIN) t = FAN_TMIN;
    if (t > FAN_TMAX) t = FAN_TMAX;
    if (s_gw <= 1) return s_gx;
    return s_gx + (t - FAN_TMIN) * (s_gw - 1) / (FAN_TMAX - FAN_TMIN);
}

static int fan_y(const fan_curve_t *fc, int i)
{
    int p = fc->pwm[i];
    if (p < 0) p = 0;
    if (p > 255) p = 255;
    if (s_gh <= 1) return s_gy;
    return s_gy + s_gh - 1 - p * (s_gh - 1) / 255;
}

static int fan_nearest(const fan_curve_t *fc, int mx, int my)
{
    int best = 0, bestd = 1 << 30;
    int np = fc->n > 0 && fc->n <= FAN_POINTS ? fc->n : 0;
    for (int i = 0; i < np; i++) {
        int dx = fan_x(fc, i) - mx;
        int dy = fan_y(fc, i) - my;
        int d = dx * dx + dy * dy;
        if (d < bestd) { bestd = d; best = i; }
    }
    return best;
}

static void cell_to_xy(int mx, int my, int *temp, int *pwm)
{
    int x = mx - s_gx;
    int y = my - s_gy;
    if (s_gw > 1) {
        if (x < 0) x = 0;
        if (x >= s_gw) x = s_gw - 1;
        *temp = FAN_TMIN + x * (FAN_TMAX - FAN_TMIN) / (s_gw - 1);
    } else {
        *temp = FAN_TMIN;
    }
    if (s_gh > 1) {
        if (y < 0) y = 0;
        if (y >= s_gh) y = s_gh - 1;
        *pwm = 255 - y * 255 / (s_gh - 1);
    } else {
        *pwm = 0;
    }
    if (*temp < FAN_TMIN) *temp = FAN_TMIN;
    if (*temp > FAN_TMAX) *temp = FAN_TMAX;
    if (*pwm < 0) *pwm = 0;
    if (*pwm > 255) *pwm = 255;
}

static void fan_fill_xy(const fan_curve_t *fc, int idx, char *xb, char *yb)
{
    if (fc && fc->n > 0 && idx >= 0 && idx < fc->n) {
        snprintf(xb, 8, "%d", fc->temp_c[idx]);
        snprintf(yb, 8, "%d", fc->pwm[idx]);
    } else {
        snprintf(xb, 8, "40");
        snprintf(yb, 8, "80");
    }
}

static void draw_fan_graph(struct ncplane *n, int ox, int oy, int w,
                           const fan_curve_t *fc, const theme_palette_t *pal)
{
    s_gx = ox + 5;
    s_gy = oy;
    s_gw = (w > 14) ? w - 10 : 20;
    s_gh = 7;
    uint64_t ch_ax = 0, ch_ln = 0, ch_pt = 0, ch_lb = 0;
    ncchannels_set_fg_rgb(&ch_ax, pal->border_dim);
    ncchannels_set_bg_rgb(&ch_ax, pal->bg_primary);
    ncchannels_set_fg_rgb(&ch_ln, pal->fg_secondary);
    ncchannels_set_bg_rgb(&ch_ln, pal->bg_primary);
    ncchannels_set_fg_rgb(&ch_pt, pal->fg_accent);
    ncchannels_set_bg_rgb(&ch_pt, pal->bg_primary);
    ncchannels_set_fg_rgb(&ch_lb, pal->text_muted);
    ncchannels_set_bg_rgb(&ch_lb, pal->bg_primary);

    ncplane_set_channels(n, ch_ax);
    for (int y = 0; y < s_gh; y++) {
        ncplane_putstr_yx(n, s_gy + y, s_gx - 1, "│");
        for (int x = 0; x < s_gw; x++)
            ncplane_putstr_yx(n, s_gy + y, s_gx + x, " ");
    }
    ncplane_putstr_yx(n, s_gy + s_gh, s_gx - 1, "└");
    for (int x = 0; x < s_gw; x++)
        ncplane_putstr_yx(n, s_gy + s_gh, s_gx + x, "─");
    ncplane_set_channels(n, ch_lb);
    ncplane_putstr_yx(n, s_gy, s_gx - 4, "255");
    ncplane_putstr_yx(n, s_gy + s_gh - 1, s_gx - 3, "0");
    ncplane_putstr_yx(n, s_gy + s_gh, s_gx, "20C");
    ncplane_putstr_yx(n, s_gy + s_gh, s_gx + s_gw - 4, "105");

    int np = fc->n > 0 && fc->n <= FAN_POINTS ? fc->n : 0;
    for (int i = 0; i < np - 1; i++)
        plot_line(n, fan_x(fc, i), fan_y(fc, i), fan_x(fc, i + 1), fan_y(fc, i + 1), ch_ln);
    ncplane_set_channels(n, ch_pt);
    ncplane_set_styles(n, NCSTYLE_BOLD);
    for (int i = 0; i < np; i++)
        ncplane_putstr_yx(n, fan_y(fc, i), fan_x(fc, i),
                          (i == s_sel_pt) ? "◆" : "●");
    ncplane_set_styles(n, NCSTYLE_NONE);
    register_target(s_gx - 1, s_gy, s_gw + 2, s_gh + 1, ACT_FAN_GRAPH);
}

// Action IDs
enum {
    ACT_NONE = 0,
    ACT_TAB_PERF,
    ACT_TAB_POWER,
    ACT_TAB_FAN,
    ACT_TAB_AURA,
    ACT_TAB_ACV,
    ACT_TAB_SLOTS,

    // Perf Tab
    ACT_PROF_QUIET,
    ACT_PROF_BALANCED,
    ACT_PROF_PERF,
    ACT_EPP_POWER,
    ACT_EPP_BAL_PWR,
    ACT_EPP_BAL_PRF,
    ACT_EPP_PERF,
    ACT_FREQ_DOWN,
    ACT_FREQ_UP,
    ACT_TEMP_DOWN,
    ACT_TEMP_UP,
    ACT_TEMP_TOGGLE,

    // Power Tab
    ACT_HZ_60,
    ACT_HZ_MAX,
    ACT_BAT_60,
    ACT_BAT_80,
    ACT_BAT_100,
    ACT_BAT_DOWN,
    ACT_BAT_UP,
    ACT_FAN_SILENT,
    ACT_FAN_STOCK,
    ACT_FAN_COOL,
    ACT_FAN_FULL,
    ACT_FAN_TOGGLE,
    ACT_PPT_QUIET,
    ACT_PPT_BAL,
    ACT_PPT_PERF,
    ACT_PPT_DOWN,
    ACT_PPT_UP,
    ACT_NV_BOOST_DOWN,
    ACT_NV_BOOST_UP,
    ACT_NV_TEMP_DOWN,
    ACT_NV_TEMP_UP,
    ACT_PANEL_OD,
    ACT_CPU_BOOST,
    ACT_BAT_ONESHOT,
    ACT_FAN_EDIT_CPU,
    ACT_FAN_EDIT_GPU,
    ACT_FAN_NODE_PREV,
    ACT_FAN_NODE_NEXT,
    ACT_FAN_T_DOWN,
    ACT_FAN_T_UP,
    ACT_FAN_P_DOWN,
    ACT_FAN_P_UP,
    ACT_FAN_WRITE,
    ACT_FAN_ADD,
    ACT_FAN_DEL,
    ACT_FOCUS_FAN_COORD,
    ACT_FOCUS_FAN_Y,
    ACT_FAN_COORD_APPLY,

    // Aura Tab
    ACT_AURA_EFF_PREV,
    ACT_AURA_EFF_NEXT,
    ACT_KBD_OFF,
    ACT_KBD_LOW,
    ACT_KBD_MED,
    ACT_KBD_HIGH,
    ACT_FOCUS_HEX_INPUT,
    ACT_APPLY_HEX,
    ACT_APPLY_THEME_COLOR,
    ACT_COLOR_PRESET_BASE = 100, // 100 .. 107 (Ice, Red, Grn, Blu, Gold, Pnk, Org, Wht)

    // ACV Tab: Themes & Tint
    ACT_THEME_ICE = 200,
    ACT_THEME_TACTICAL,
    ACT_THEME_EMERALD,
    ACT_THEME_CRIMSON,
    ACT_THEME_STEALTH,
    ACT_THEME_SYNC,
    ACT_TINT_0,
    ACT_TINT_1,
    ACT_TINT_2,
    ACT_TINT_3,
    ACT_TINT_4,
    ACT_TOGGLE_SYNC_KBD,
    ACT_TOGGLE_SYNC_WAYBAR,

    // ACV Tab: Profiles
    ACT_FOCUS_PROFILE_NAME,
    ACT_GEN_RANDOM_NAME,
    ACT_FILTER_EXP_PERF,
    ACT_FILTER_EXP_PWR,
    ACT_FILTER_EXP_AURA,
    ACT_FILTER_EXP_THEME,
    ACT_EXPORT_PROFILE,
    ACT_EXPORT_ALL_SKIP,

    ACT_SLOT_PROFILE_BASE = 300, // 300 .. 331
    ACT_FILTER_IMP_PERF = 340,
    ACT_FILTER_IMP_PWR,
    ACT_FILTER_IMP_AURA,
    ACT_FILTER_IMP_THEME,
    ACT_IMPORT_PROFILE,
    ACT_IMPORT_ALL_SKIP,
    ACT_DELETE_PROFILE
};

static void set_ch(uint64_t *channels, uint32_t fg, uint32_t bg) {
    ncchannels_set_fg_rgb(channels, fg);
    ncchannels_set_bg_rgb(channels, bg);
}

static void draw_btn(struct ncplane *n, int x, int y, const char *label, bool active, bool focused, int id, const theme_palette_t *pal) {
    int len = strlen(label);
    uint64_t ch = 0;

    if (focused && active) {
        set_ch(&ch, 0x000000, pal->fg_accent);
        ncplane_set_channels(n, ch);
        ncplane_set_styles(n, NCSTYLE_BOLD | NCSTYLE_UNDERLINE);
    } else if (active) {
        set_ch(&ch, 0x000000, pal->fg_accent);
        ncplane_set_channels(n, ch);
        ncplane_set_styles(n, NCSTYLE_BOLD);
    } else if (focused) {
        set_ch(&ch, pal->fg_accent, pal->bg_card);
        ncplane_set_channels(n, ch);
        ncplane_set_styles(n, NCSTYLE_BOLD | NCSTYLE_UNDERLINE);
    } else {
        set_ch(&ch, pal->text_norm, pal->bg_card);
        ncplane_set_channels(n, ch);
        ncplane_set_styles(n, NCSTYLE_NONE);
    }

    ncplane_putstr_yx(n, y, x, label);
    ncplane_set_styles(n, NCSTYLE_NONE);
    register_target(x, y, len, 1, id);
}

static void draw_app_frame(struct ncplane *n, int ox, int oy, int w, int h, ui_tab_t tab, const theme_palette_t *pal, int tint_level) {
    uint64_t ch_border = 0, ch_header = 0, ch_tab_act = 0, ch_tab_inact = 0, ch_bg = 0;
    set_ch(&ch_border, pal->border_dim, pal->bg_primary);
    set_ch(&ch_header, pal->fg_accent, pal->bg_primary);
    set_ch(&ch_tab_act, 0x000000, pal->fg_accent);
    set_ch(&ch_tab_inact, pal->text_muted, pal->bg_card);

    if (tint_level == 0) {
        // 0% Clear Glass: Transparent background cells
        set_ch(&ch_bg, pal->text_norm, 0x000000);
        ncchannels_set_bg_alpha(&ch_bg, NCALPHA_TRANSPARENT);
    } else {
        set_ch(&ch_bg, pal->text_norm, pal->bg_primary);
    }

    // Inside window fill
    ncplane_set_channels(n, ch_bg);
    for (int y = 0; y < h; y++) {
        ncplane_cursor_move_yx(n, oy + y, ox);
        for (int x = 0; x < w; x++) {
            ncplane_putchar(n, ' ');
        }
    }

    // Outer border
    ncplane_set_channels(n, ch_border);
    ncplane_putstr_yx(n, oy, ox, "┌");
    for (int x = 1; x < w - 1; x++) ncplane_putstr(n, "─");
    ncplane_putstr(n, "┐");

    for (int y = 1; y < h - 1; y++) {
        ncplane_putstr_yx(n, oy + y, ox, "│");
        ncplane_putstr_yx(n, oy + y, ox + w - 1, "│");
    }

    ncplane_putstr_yx(n, oy + h - 1, ox, "└");
    for (int x = 1; x < w - 1; x++) ncplane_putstr(n, "─");
    ncplane_putstr(n, "┘");

    // Divider under tabs
    ncplane_putstr_yx(n, oy + 2, ox, "├");
    for (int x = 1; x < w - 1; x++) ncplane_putstr(n, "─");
    ncplane_putstr(n, "┤");

    // Footer divider
    ncplane_putstr_yx(n, oy + h - 3, ox, "├");
    for (int x = 1; x < w - 1; x++) ncplane_putstr(n, "─");
    ncplane_putstr(n, "┤");

    // Title banner
    ncplane_set_channels(n, ch_header);
    ncplane_set_styles(n, NCSTYLE_BOLD);
    char title[128];
    snprintf(title, sizeof(title), " CTRON  [Arcioth & Kerempkl] ");
    if (strlen(title) > (size_t)(w - 4)) title[w - 4] = '\0';
    ncplane_putstr_yx(n, oy, ox + 3, title);
    ncplane_set_styles(n, NCSTYLE_NONE);

    const char *tabs[] = { " PRF ", " PWR ", " FAN ", " AUR ", " ACV ", " SLT " };
    int tx = ox + 1;
    for (int i = 0; i < TAB_COUNT; i++) {
        int tlen = strlen(tabs[i]);
        if (i == (int)tab) {
            ncplane_set_channels(n, ch_tab_act);
            ncplane_set_styles(n, NCSTYLE_BOLD);
        } else {
            ncplane_set_channels(n, ch_tab_inact);
            ncplane_set_styles(n, NCSTYLE_NONE);
        }
        ncplane_putstr_yx(n, oy + 1, tx, tabs[i]);
        /* clickable core of the label only — not the padding that sits on the window edge */
        if (tlen > 2)
            register_target(tx + 1, oy + 1, tlen - 2, 1, ACT_TAB_PERF + i);
        tx += tlen;
    }
    ncplane_set_styles(n, NCSTYLE_NONE);
}

int ui_run(hardware_state_t *hw) {
    setlocale(LC_ALL, "");

    struct notcurses_options opts = {0};
    opts.flags = NCOPTION_SUPPRESS_BANNERS | NCOPTION_NO_CLEAR_BITMAPS | NCOPTION_NO_WINCH_SIGHANDLER;
    opts.loglevel = NCLOGLEVEL_SILENT;

    struct notcurses *nc = notcurses_init(&opts, NULL);
    if (!nc) {
        fprintf(stderr, "Failed to initialize Notcurses.\n");
        return -1;
    }

    notcurses_mice_enable(nc, NCMICE_BUTTON_EVENT);
    struct ncplane *stdn = notcurses_stdplane(nc);

    ui_tab_t cur_tab = TAB_PERF;
    int focus_item = 0;
    bool running = true;

    // Profile state
    acv_profile_filter_t exp_filter = { true, true, true, true };
    acv_profile_filter_t imp_filter = { true, true, true, true };
    char profile_slots[MAX_PROFILES][4];
    int profile_count = profile_list(profile_slots, MAX_PROFILES);
    int selected_slot = 0;
    int fan_edit_gpu = 0;
    int fan_edit_idx = 0;
    char fan_xbuf[8] = "40";
    char fan_ybuf[8] = "80";
    fan_fill_xy(&hw->fan_cpu, 0, fan_xbuf, fan_ybuf);

    hw_waybar_sync_enable(hw->sync_with_waybar);
    // Read initial keyboard hex from device
    hw_read_backlight_hex(hw->custom_hex, &hw->custom_rgb);
    snprintf(hw->hex_input, sizeof(hw->hex_input), "%s", hw->custom_hex);
    if (!hw->profile_name_input[0]) profile_gen_random_name(hw->profile_name_input);

    while (running) {
        clear_targets();

        unsigned term_y = 0, term_x = 0;
        ncplane_dim_yx(stdn, &term_y, &term_x);

        int ox = (term_x > WIN_W) ? (int)(term_x - WIN_W) / 2 : 0;
        int oy = (term_y > WIN_H) ? (int)(term_y - WIN_H) / 2 : 0;
        int w = (term_x < WIN_W) ? (int)term_x : WIN_W;
        int h = (term_y < WIN_H) ? (int)term_y : WIN_H;

        theme_palette_t pal = hw_get_palette(hw);

        draw_app_frame(stdn, ox, oy, w, h, cur_tab, &pal, hw->tint_level);

        int cy = oy + 3;
        uint64_t ch_txt = 0, ch_hi = 0;
        set_ch(&ch_txt, pal.text_norm, pal.bg_primary);
        set_ch(&ch_hi, pal.fg_accent, pal.bg_primary);

        switch (cur_tab) {
            case TAB_PERF: {
                ncplane_set_channels(stdn, ch_hi);
                ncplane_set_styles(stdn, NCSTYLE_BOLD);
                ncplane_putstr_yx(stdn, cy++, ox + 3, "ASUS PLATFORM PROFILES:");
                ncplane_set_styles(stdn, NCSTYLE_NONE);

                draw_btn(stdn, ox + 3, cy, " Quiet ", hw->active_profile == PROF_QUIET, focus_item == 0, ACT_PROF_QUIET, &pal);
                draw_btn(stdn, ox + 15, cy, " Balanced ", hw->active_profile == PROF_BALANCED, focus_item == 1, ACT_PROF_BALANCED, &pal);
                draw_btn(stdn, ox + 29, cy, " Performance ", hw->active_profile == PROF_PERFORMANCE, focus_item == 2, ACT_PROF_PERF, &pal);
                cy += 2;

                ncplane_set_channels(stdn, ch_hi);
                ncplane_set_styles(stdn, NCSTYLE_BOLD);
                ncplane_putstr_yx(stdn, cy++, ox + 3, "AMD ENERGY PREFERENCE (EPP):");
                ncplane_set_styles(stdn, NCSTYLE_NONE);

                draw_btn(stdn, ox + 3, cy, " Pwr ", hw->active_epp == EPP_POWER, focus_item == 3, ACT_EPP_POWER, &pal);
                draw_btn(stdn, ox + 11, cy, " Bal-Pwr ", hw->active_epp == EPP_BALANCED_POWER, focus_item == 4, ACT_EPP_BAL_PWR, &pal);
                draw_btn(stdn, ox + 23, cy, " Bal-Perf ", hw->active_epp == EPP_BALANCED_PERF, focus_item == 5, ACT_EPP_BAL_PRF, &pal);
                draw_btn(stdn, ox + 36, cy, " Perf ", hw->active_epp == EPP_PERFORMANCE, focus_item == 6, ACT_EPP_PERF, &pal);
                cy += 2;

                ncplane_set_channels(stdn, ch_hi);
                ncplane_set_styles(stdn, NCSTYLE_BOLD);
                ncplane_putstr_yx(stdn, cy++, ox + 3, "CPU FREQUENCY & THERMALS:");
                ncplane_set_styles(stdn, NCSTYLE_NONE);

                char f_buf[128];
                snprintf(f_buf, sizeof(f_buf), "Clock: %d MHz  |  Temp: %d°C%s",
                         hw->cpu_cur_freq_mhz, hw->cpu_temp_c,
                         (hw->cpu_temp_cap_on && hw->cpu_applied_max_mhz > 0 &&
                          hw->cpu_applied_max_mhz < hw->cpu_target_max_mhz) ? "  THROTTLE" : "");
                ncplane_set_channels(stdn, ch_txt);
                ncplane_putstr_yx(stdn, cy++, ox + 3, f_buf);

                snprintf(f_buf, sizeof(f_buf), "Max Cap: %d MHz", hw->cpu_target_max_mhz);
                ncplane_putstr_yx(stdn, cy, ox + 3, f_buf);
                draw_btn(stdn, ox + 28, cy, " [-100] ", false, focus_item == 7, ACT_FREQ_DOWN, &pal);
                draw_btn(stdn, ox + 38, cy, " [+100] ", false, focus_item == 8, ACT_FREQ_UP, &pal);
                cy += 2;

                snprintf(f_buf, sizeof(f_buf), "Tctl Cap: %d°C", hw->cpu_temp_cap_c);
                ncplane_putstr_yx(stdn, cy, ox + 3, f_buf);
                draw_btn(stdn, ox + 22, cy, hw->cpu_temp_cap_on ? " ON " : " OFF ",
                         hw->cpu_temp_cap_on, focus_item == 9, ACT_TEMP_TOGGLE, &pal);
                draw_btn(stdn, ox + 28, cy, " [-5] ", false, focus_item == 10, ACT_TEMP_DOWN, &pal);
                draw_btn(stdn, ox + 36, cy, " [+5] ", false, focus_item == 11, ACT_TEMP_UP, &pal);
                cy += 2;

                ncplane_set_channels(stdn, ch_hi);
                ncplane_set_styles(stdn, NCSTYLE_BOLD);
                ncplane_putstr_yx(stdn, cy++, ox + 3, "PPT (ASUS FA507 AC 15-80 / DC 15-65 W):");
                ncplane_set_styles(stdn, NCSTYLE_NONE);
                ncplane_set_channels(stdn, ch_txt);
                {
                    char spl[8] = "--", sppt[8] = "--", fppt[8] = "--";
                    if (hw->ppt_spl > 5) snprintf(spl, sizeof(spl), "%dW", hw->ppt_spl);
                    if (hw->ppt_sppt > 5) snprintf(sppt, sizeof(sppt), "%dW", hw->ppt_sppt);
                    if (hw->ppt_fppt > 5) snprintf(fppt, sizeof(fppt), "%dW", hw->ppt_fppt);
                    snprintf(f_buf, sizeof(f_buf), "SPL %s  SPPT %s  FPPT %s", spl, sppt, fppt);
                }
                ncplane_putstr_yx(stdn, cy++, ox + 3, f_buf);
                draw_btn(stdn, ox + 3, cy, " Q45 ", false, focus_item == 12, ACT_PPT_QUIET, &pal);
                draw_btn(stdn, ox + 10, cy, " B60 ", false, focus_item == 13, ACT_PPT_BAL, &pal);
                draw_btn(stdn, ox + 17, cy, " P80 ", false, focus_item == 14, ACT_PPT_PERF, &pal);
                draw_btn(stdn, ox + 24, cy, " [-5] ", false, focus_item == 15, ACT_PPT_DOWN, &pal);
                draw_btn(stdn, ox + 32, cy, " [+5] ", false, focus_item == 16, ACT_PPT_UP, &pal);
                cy += 2;
                break;
            }

            case TAB_POWER: {
                ncplane_set_channels(stdn, ch_hi);
                ncplane_set_styles(stdn, NCSTYLE_BOLD);
                ncplane_putstr_yx(stdn, cy++, ox + 3, "DISPLAY REFRESH RATE:");
                ncplane_set_styles(stdn, NCSTYLE_NONE);

                char dinfo[128];
                snprintf(dinfo, sizeof(dinfo), "Panel: %s (%s) @ %dHz", hw->display_name, hw->display_res, hw->display_cur_hz);
                ncplane_set_channels(stdn, ch_txt);
                ncplane_putstr_yx(stdn, cy++, ox + 3, dinfo);

                draw_btn(stdn, ox + 3, cy, " 60 Hz (Power Save) ", hw->display_cur_hz <= 60, focus_item == 0, ACT_HZ_60, &pal);
                char hz_lbl[32];
                snprintf(hz_lbl, sizeof(hz_lbl), " %d Hz (High Refresh) ", hw->display_max_hz);
                draw_btn(stdn, ox + 26, cy, hz_lbl, hw->display_cur_hz > 60, focus_item == 1, ACT_HZ_MAX, &pal);
                cy += 3;

                ncplane_set_channels(stdn, ch_hi);
                ncplane_set_styles(stdn, NCSTYLE_BOLD);
                ncplane_putstr_yx(stdn, cy++, ox + 3, "BATTERY HEALTH CHARGE LIMIT:");
                ncplane_set_styles(stdn, NCSTYLE_NONE);

                char binfo[128];
                snprintf(binfo, sizeof(binfo), "State: %d%% (%s) | Cap: %d%%", hw->battery_percent, hw->battery_status, hw->battery_charge_limit);
                ncplane_set_channels(stdn, ch_txt);
                ncplane_putstr_yx(stdn, cy++, ox + 3, binfo);

                draw_btn(stdn, ox + 3, cy, " 60% ", hw->battery_charge_limit == 60, focus_item == 2, ACT_BAT_60, &pal);
                draw_btn(stdn, ox + 11, cy, " 80% ", hw->battery_charge_limit == 80, focus_item == 3, ACT_BAT_80, &pal);
                draw_btn(stdn, ox + 19, cy, " 100% ", hw->battery_charge_limit == 100, focus_item == 4, ACT_BAT_100, &pal);
                draw_btn(stdn, ox + 28, cy, " [-5] ", false, focus_item == 5, ACT_BAT_DOWN, &pal);
                draw_btn(stdn, ox + 36, cy, " [+5] ", false, focus_item == 6, ACT_BAT_UP, &pal);
                cy += 2;

                ncplane_set_channels(stdn, ch_hi);
                ncplane_set_styles(stdn, NCSTYLE_BOLD);
                ncplane_putstr_yx(stdn, cy++, ox + 3, "NVIDIA / PANEL:");
                ncplane_set_styles(stdn, NCSTYLE_NONE);
                {
                    char nline[96];
                    snprintf(nline, sizeof(nline), "Boost %dW  GPU %d°C", hw->nv_boost_w, hw->nv_temp_target);
                    ncplane_set_channels(stdn, ch_txt);
                    ncplane_putstr_yx(stdn, cy, ox + 3, nline);
                }
                draw_btn(stdn, ox + 26, cy, " B- ", false, focus_item == 7, ACT_NV_BOOST_DOWN, &pal);
                draw_btn(stdn, ox + 31, cy, " B+ ", false, focus_item == 8, ACT_NV_BOOST_UP, &pal);
                draw_btn(stdn, ox + 36, cy, " T- ", false, focus_item == 9, ACT_NV_TEMP_DOWN, &pal);
                draw_btn(stdn, ox + 41, cy, " T+ ", false, focus_item == 10, ACT_NV_TEMP_UP, &pal);
                cy += 2;
                draw_btn(stdn, ox + 3, cy, hw->panel_od ? " OD ON " : " OD OFF ",
                         hw->panel_od, focus_item == 11, ACT_PANEL_OD, &pal);
                draw_btn(stdn, ox + 14, cy, hw->cpu_boost ? " Boost ON " : " Boost OFF ",
                         hw->cpu_boost, focus_item == 12, ACT_CPU_BOOST, &pal);
                draw_btn(stdn, ox + 28, cy, " Oneshot ", false, focus_item == 13, ACT_BAT_ONESHOT, &pal);
                cy += 2;
                break;
            }

            case TAB_FAN: {
                ncplane_set_channels(stdn, ch_hi);
                ncplane_set_styles(stdn, NCSTYLE_BOLD);
                ncplane_putstr_yx(stdn, cy, ox + 3, "FANS");
                ncplane_set_styles(stdn, NCSTYLE_NONE);
                draw_btn(stdn, ox + 9, cy, "CPU", !fan_edit_gpu, focus_item == 0, ACT_FAN_EDIT_CPU, &pal);
                draw_btn(stdn, ox + 14, cy, "GPU", fan_edit_gpu, focus_item == 1, ACT_FAN_EDIT_GPU, &pal);
                draw_btn(stdn, ox + 20, cy, "+", false, focus_item == 2, ACT_FAN_ADD, &pal);
                draw_btn(stdn, ox + 24, cy, "-", false, focus_item == 3, ACT_FAN_DEL, &pal);
                cy += 1;
                {
                    fan_curve_t *fc = fan_edit_gpu ? &hw->fan_gpu : &hw->fan_cpu;
                    if (fc->n < 0) fc->n = 0;
                    if (fc->n > FAN_POINTS) fc->n = FAN_POINTS;
                    if (fc->n == 0)
                        fan_edit_idx = 0;
                    else if (fan_edit_idx >= fc->n)
                        fan_edit_idx = fc->n - 1;
                    if (fan_edit_idx < 0) fan_edit_idx = 0;
                    s_sel_pt = (fc->n > 0) ? fan_edit_idx : -1;
                    draw_fan_graph(stdn, ox, cy, w, fc, &pal);
                    cy += 9;
                    if (fc->n < 1) {
                        ncplane_set_channels(stdn, ch_txt);
                        ncplane_putstr_yx(stdn, cy++, ox + 3, "click graph or type X Y then +");
                    } else {
                        for (int i = 0; i < fc->n && i < FAN_POINTS; i++) {
                            int col = i % 4;
                            int row = i / 4;
                            char chip[16];
                            snprintf(chip, sizeof(chip), "%d:%d,%d",
                                     i + 1, fc->temp_c[i], fc->pwm[i]);
                            draw_btn(stdn, ox + 3 + col * 12, cy + row, chip,
                                     i == fan_edit_idx, false,
                                     ACT_FAN_SEL_BASE + i, &pal);
                        }
                        cy += (fc->n > 4) ? 2 : 1;
                    }
                    ncplane_set_channels(stdn, ch_txt);
                    ncplane_putstr_yx(stdn, cy, ox + 3, "X");
                    char xbtn[10], ybtn[10];
                    if (hw->active_input_field == 3)
                        snprintf(xbtn, sizeof(xbtn), "[%-3s_]", fan_xbuf);
                    else
                        snprintf(xbtn, sizeof(xbtn), "[%-3s]", fan_xbuf);
                    if (hw->active_input_field == 4)
                        snprintf(ybtn, sizeof(ybtn), "[%-3s_]", fan_ybuf);
                    else
                        snprintf(ybtn, sizeof(ybtn), "[%-3s]", fan_ybuf);
                    draw_btn(stdn, ox + 5, cy, xbtn, hw->active_input_field == 3,
                             focus_item == 4, ACT_FOCUS_FAN_COORD, &pal);
                    ncplane_set_channels(stdn, ch_txt);
                    ncplane_putstr_yx(stdn, cy, ox + 15, "Y");
                    draw_btn(stdn, ox + 17, cy, ybtn, hw->active_input_field == 4,
                             focus_item == 5, ACT_FOCUS_FAN_Y, &pal);
                    draw_btn(stdn, ox + 27, cy, "Set", false, focus_item == 6, ACT_FAN_COORD_APPLY, &pal);
                    draw_btn(stdn, ox + 32, cy, "<", false, focus_item == 7, ACT_FAN_NODE_PREV, &pal);
                    draw_btn(stdn, ox + 36, cy, ">", false, focus_item == 8, ACT_FAN_NODE_NEXT, &pal);
                    cy += 2;
                }
                draw_btn(stdn, ox + 3, cy, "Sil", false, focus_item == 9, ACT_FAN_SILENT, &pal);
                draw_btn(stdn, ox + 9, cy, "Stk", false, focus_item == 10, ACT_FAN_STOCK, &pal);
                draw_btn(stdn, ox + 15, cy, "Col", false, focus_item == 11, ACT_FAN_COOL, &pal);
                draw_btn(stdn, ox + 21, cy, "Ful", false, focus_item == 12, ACT_FAN_FULL, &pal);
                draw_btn(stdn, ox + 27, cy,
                         (hw->fan_cpu_on || hw->fan_gpu_on) ? " ON " : " OFF ",
                         hw->fan_cpu_on || hw->fan_gpu_on, focus_item == 13, ACT_FAN_TOGGLE, &pal);
                draw_btn(stdn, ox + 34, cy, " Write ", false, focus_item == 14, ACT_FAN_WRITE, &pal);
                cy += 2;
                break;
            }

            case TAB_AURA: {
                ncplane_set_channels(stdn, ch_hi);
                ncplane_set_styles(stdn, NCSTYLE_BOLD);
                ncplane_putstr_yx(stdn, cy++, ox + 3, "AURA KEYBOARD LIGHTING:");
                ncplane_set_styles(stdn, NCSTYLE_NONE);

                char eff_buf[64];
                snprintf(eff_buf, sizeof(eff_buf), "Effect: %s", AURA_EFFECT_NAMES[hw->aura_effect_idx]);
                ncplane_set_channels(stdn, ch_txt);
                ncplane_putstr_yx(stdn, cy, ox + 3, eff_buf);
                draw_btn(stdn, ox + 28, cy, " [<] ", false, focus_item == 0, ACT_AURA_EFF_PREV, &pal);
                draw_btn(stdn, ox + 36, cy, " [>] ", false, focus_item == 1, ACT_AURA_EFF_NEXT, &pal);
                cy += 2;

                ncplane_putstr_yx(stdn, cy, ox + 3, "Brightness:");
                draw_btn(stdn, ox + 15, cy, "Off", hw->kbd_brightness == KBD_OFF, focus_item == 2, ACT_KBD_OFF, &pal);
                draw_btn(stdn, ox + 21, cy, "Low", hw->kbd_brightness == KBD_LOW, focus_item == 3, ACT_KBD_LOW, &pal);
                draw_btn(stdn, ox + 27, cy, "Med", hw->kbd_brightness == KBD_MED, focus_item == 4, ACT_KBD_MED, &pal);
                draw_btn(stdn, ox + 33, cy, "High", hw->kbd_brightness == KBD_HIGH, focus_item == 5, ACT_KBD_HIGH, &pal);
                cy += 2;

                // Color Presets (Prefixes)
                ncplane_set_channels(stdn, ch_hi);
                ncplane_set_styles(stdn, NCSTYLE_BOLD);
                ncplane_putstr_yx(stdn, cy++, ox + 3, "COLOR PRESETS:");
                ncplane_set_styles(stdn, NCSTYLE_NONE);

                const char *p_lbls[] = { "Ice", "Red", "Grn", "Blu", "Gold", "Pnk", "Org", "Wht" };
                int px = ox + 3;
                for (int i = 0; i < 8; i++) {
                    draw_btn(stdn, px, cy, p_lbls[i], hw->aura_color_idx == i, focus_item == 6 + i, ACT_COLOR_PRESET_BASE + i, &pal);
                    px += strlen(p_lbls[i]) + 2;
                }
                cy += 2;

                // Custom Hex Code Entry through Keyboard
                ncplane_set_channels(stdn, ch_hi);
                ncplane_set_styles(stdn, NCSTYLE_BOLD);
                ncplane_putstr_yx(stdn, cy++, ox + 3, "CUSTOM HEX ENTRY (Type 0-9, A-F):");
                ncplane_set_styles(stdn, NCSTYLE_NONE);

                char h_entry[32];
                if (hw->active_input_field == 1) {
                    snprintf(h_entry, sizeof(h_entry), " [ #%-6s_ ] ", hw->hex_input);
                } else {
                    snprintf(h_entry, sizeof(h_entry), " [ #%-6s ] ", hw->hex_input);
                }
                draw_btn(stdn, ox + 3, cy, h_entry, hw->active_input_field == 1, focus_item == 14, ACT_FOCUS_HEX_INPUT, &pal);

                // Swatch preview
                uint64_t ch_sw = 0;
                set_ch(&ch_sw, hw->custom_rgb, pal.bg_card);
                ncplane_set_channels(stdn, ch_sw);
                ncplane_putstr_yx(stdn, cy, ox + 20, " [ ████ ] ");

                draw_btn(stdn, ox + 32, cy, " Apply Hex ", false, focus_item == 15, ACT_APPLY_HEX, &pal);
                cy += 2;

                draw_btn(stdn, ox + 3, cy, " Match ACV Theme to Backlight ", hw->theme == THEME_BACKLIGHT_SYNC, focus_item == 16, ACT_APPLY_THEME_COLOR, &pal);
                cy += 2;

                char w_btn[40];
                snprintf(w_btn, sizeof(w_btn), " Waybar Sync: [%s] ", hw->sync_with_waybar ? "ON" : "OFF");
                draw_btn(stdn, ox + 3, cy, w_btn, hw->sync_with_waybar, focus_item == 17, ACT_TOGGLE_SYNC_WAYBAR, &pal);
                cy += 2;
                break;
            }

            case TAB_ACV: {
                // Theme & Inside Window Tint
                ncplane_set_channels(stdn, ch_hi);
                ncplane_set_styles(stdn, NCSTYLE_BOLD);
                ncplane_putstr_yx(stdn, cy++, ox + 3, "THEME PRESET:");
                ncplane_set_styles(stdn, NCSTYLE_NONE);

                draw_btn(stdn, ox + 3, cy, " Ice ", hw->theme == THEME_TUF_ICE, focus_item == 0, ACT_THEME_ICE, &pal);
                draw_btn(stdn, ox + 10, cy, " Tactical ", hw->theme == THEME_TACTICAL, focus_item == 1, ACT_THEME_TACTICAL, &pal);
                draw_btn(stdn, ox + 22, cy, " Emerald ", hw->theme == THEME_EMERALD, focus_item == 2, ACT_THEME_EMERALD, &pal);
                draw_btn(stdn, ox + 33, cy, " Crimson ", hw->theme == THEME_CRIMSON, focus_item == 3, ACT_THEME_CRIMSON, &pal);
                cy += 2;

                draw_btn(stdn, ox + 3, cy, " Stealth ", hw->theme == THEME_STEALTH, focus_item == 4, ACT_THEME_STEALTH, &pal);
                draw_btn(stdn, ox + 14, cy, " Backlight Match ", hw->theme == THEME_BACKLIGHT_SYNC, focus_item == 5, ACT_THEME_SYNC, &pal);
                cy += 2;

                // Inside Window Tint: Exactly 2 modes
                ncplane_set_channels(stdn, ch_hi);
                ncplane_set_styles(stdn, NCSTYLE_BOLD);
                ncplane_putstr_yx(stdn, cy++, ox + 3, "INSIDE WINDOW TINT (Transparency):");
                ncplane_set_styles(stdn, NCSTYLE_NONE);

                draw_btn(stdn, ox + 3, cy, " Clear Glass (Transparent) ", hw->tint_level == 0, focus_item == 6, ACT_TINT_0, &pal);
                draw_btn(stdn, ox + 32, cy, " Solid Opaque ", hw->tint_level == 1, focus_item == 7, ACT_TINT_1, &pal);
                cy += 2;

                // Integrations & Sync Section
                ncplane_set_channels(stdn, ch_hi);
                ncplane_set_styles(stdn, NCSTYLE_BOLD);
                ncplane_putstr_yx(stdn, cy++, ox + 3, "INTEGRATIONS & SYNC:");
                ncplane_set_styles(stdn, NCSTYLE_NONE);

                char wb_btn[32];
                snprintf(wb_btn, sizeof(wb_btn), " Waybar Sync: [%s] ", hw->sync_with_waybar ? "ON" : "OFF");
                draw_btn(stdn, ox + 3, cy, wb_btn, hw->sync_with_waybar, focus_item == 8, ACT_TOGGLE_SYNC_WAYBAR, &pal);

                char kb_btn[32];
                snprintf(kb_btn, sizeof(kb_btn), " Backlight Sync: [%s] ", hw->sync_with_backlight ? "ON" : "OFF");
                draw_btn(stdn, ox + 24, cy, kb_btn, hw->sync_with_backlight, focus_item == 9, ACT_TOGGLE_SYNC_KBD, &pal);
                cy += 2;

                // Profiles Export Section
                ncplane_set_channels(stdn, ch_hi);
                ncplane_set_styles(stdn, NCSTYLE_BOLD);
                ncplane_putstr_yx(stdn, cy++, ox + 3, "EXPORT PROFILE (.ctr):");
                ncplane_set_styles(stdn, NCSTYLE_NONE);

                // Export Row
                char ptag_btn[32];
                if (hw->active_input_field == 2) {
                    snprintf(ptag_btn, sizeof(ptag_btn), "Tag: [%-3s_]", hw->profile_name_input);
                } else {
                    snprintf(ptag_btn, sizeof(ptag_btn), "Tag: [%-3s]", hw->profile_name_input);
                }
                draw_btn(stdn, ox + 3, cy, ptag_btn, hw->active_input_field == 2, focus_item == 10, ACT_FOCUS_PROFILE_NAME, &pal);
                draw_btn(stdn, ox + 16, cy, "New Tag", false, focus_item == 11, ACT_GEN_RANDOM_NAME, &pal);

                // Filter checkboxes
                char ck[16];
                snprintf(ck, sizeof(ck), "[%c]Pwr", exp_filter.include_perf ? 'x' : ' ');
                draw_btn(stdn, ox + 26, cy, ck, exp_filter.include_perf, focus_item == 12, ACT_FILTER_EXP_PERF, &pal);
                snprintf(ck, sizeof(ck), "[%c]Bat", exp_filter.include_power ? 'x' : ' ');
                draw_btn(stdn, ox + 34, cy, ck, exp_filter.include_power, focus_item == 13, ACT_FILTER_EXP_PWR, &pal);
                snprintf(ck, sizeof(ck), "[%c]Led", exp_filter.include_aura ? 'x' : ' ');
                draw_btn(stdn, ox + 42, cy, ck, exp_filter.include_aura, focus_item == 14, ACT_FILTER_EXP_AURA, &pal);
                cy += 2;

                draw_btn(stdn, ox + 3, cy, " Export .ctr ", false, focus_item == 15, ACT_EXPORT_PROFILE, &pal);
                draw_btn(stdn, ox + 18, cy, " Export All (Skip) ", false, focus_item == 16, ACT_EXPORT_ALL_SKIP, &pal);
                cy += 2;

                draw_btn(stdn, ox + 3, cy, " [>] Manage & Apply Saved Profiles (Tab 6) -> ", false, focus_item == 17, ACT_TAB_SLOTS, &pal);
                cy += 2;
                break;
            }

            case TAB_SLOTS: {
                ncplane_set_channels(stdn, ch_hi);
                ncplane_set_styles(stdn, NCSTYLE_BOLD);
                ncplane_putstr_yx(stdn, cy++, ox + 3, "SAVED PROFILE SLOTS (~/.config/vhelper/profiles):");
                ncplane_set_styles(stdn, NCSTYLE_NONE);

                if (profile_count == 0) {
                    ncplane_set_channels(stdn, ch_txt);
                    ncplane_putstr_yx(stdn, cy++, ox + 3, "(No saved profiles found. Export one in Tab 5!)");
                } else {
                    // Grid of slots: up to 12 slots, 4 per row
                    int sx = ox + 3;
                    for (int i = 0; i < profile_count && i < 12; i++) {
                        if (i > 0 && i % 4 == 0) {
                            cy += 2;
                            sx = ox + 3;
                        }
                        char sbtn[32];
                        snprintf(sbtn, sizeof(sbtn), " %-3s ", profile_slots[i]);
                        draw_btn(stdn, sx, cy, sbtn, selected_slot == i, focus_item == i, ACT_SLOT_PROFILE_BASE + i, &pal);
                        sx += strlen(sbtn) + 2;
                    }
                    cy += 2;

                    // Details of selected profile
                    if (selected_slot >= 0 && selected_slot < profile_count) {
                        char perf_sum[64] = {0}, pwr_sum[64] = {0}, aura_sum[64] = {0};
                        profile_get_summary(profile_slots[selected_slot], perf_sum, pwr_sum, aura_sum);

                        ncplane_set_channels(stdn, ch_hi);
                        ncplane_set_styles(stdn, NCSTYLE_BOLD);
                        char sel_lbl[64];
                        snprintf(sel_lbl, sizeof(sel_lbl), "SELECTED PROFILE: %s.ctr", profile_slots[selected_slot]);
                        ncplane_putstr_yx(stdn, cy++, ox + 3, sel_lbl);
                        ncplane_set_styles(stdn, NCSTYLE_NONE);

                        set_ch(&ch_txt, pal.text_norm, pal.bg_primary);
                        ncplane_set_channels(stdn, ch_txt);
                        char d_buf[128];
                        snprintf(d_buf, sizeof(d_buf), "• Perf : %s", perf_sum);
                        ncplane_putstr_yx(stdn, cy++, ox + 3, d_buf);
                        snprintf(d_buf, sizeof(d_buf), "• Power: %s", pwr_sum);
                        ncplane_putstr_yx(stdn, cy++, ox + 3, d_buf);
                        snprintf(d_buf, sizeof(d_buf), "• Aura : %s", aura_sum);
                        ncplane_putstr_yx(stdn, cy++, ox + 3, d_buf);
                        cy++;

                        // Filter checkboxes for import
                        char ick[16];
                        snprintf(ick, sizeof(ick), "[%c]Pwr", imp_filter.include_perf ? 'x' : ' ');
                        draw_btn(stdn, ox + 3, cy, ick, imp_filter.include_perf, focus_item == 12, ACT_FILTER_IMP_PERF, &pal);
                        snprintf(ick, sizeof(ick), "[%c]Bat", imp_filter.include_power ? 'x' : ' ');
                        draw_btn(stdn, ox + 11, cy, ick, imp_filter.include_power, focus_item == 13, ACT_FILTER_IMP_PWR, &pal);
                        snprintf(ick, sizeof(ick), "[%c]Led", imp_filter.include_aura ? 'x' : ' ');
                        draw_btn(stdn, ox + 19, cy, ick, imp_filter.include_aura, focus_item == 14, ACT_FILTER_IMP_AURA, &pal);
                        snprintf(ick, sizeof(ick), "[%c]Thm", imp_filter.include_theme ? 'x' : ' ');
                        draw_btn(stdn, ox + 27, cy, ick, imp_filter.include_theme, focus_item == 15, ACT_FILTER_IMP_THEME, &pal);
                        cy += 2;

                        draw_btn(stdn, ox + 3, cy, " Apply Selected ", false, focus_item == 16, ACT_IMPORT_PROFILE, &pal);
                        draw_btn(stdn, ox + 21, cy, " Apply All ", false, focus_item == 17, ACT_IMPORT_ALL_SKIP, &pal);
                        draw_btn(stdn, ox + 34, cy, " Delete ", false, focus_item == 18, ACT_DELETE_PROFILE, &pal);
                        cy += 2;
                    }
                }
                break;
            }

            default:
                break;
        }

        // Footer hint
        uint64_t ch_footer = 0;
        set_ch(&ch_footer, pal.text_muted, pal.bg_primary);
        ncplane_set_channels(stdn, ch_footer);
        {
            const char *lg = log_get(0);
            char foot[56];
            snprintf(foot, sizeof(foot), "%s", lg && lg[0] ? lg : "q quit");
            ncplane_putstr_yx(stdn, oy + h - 2, ox + 3, foot);
        }

        notcurses_render(nc);

        // Input poll
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_nsec += 250 * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000L;
        }

        struct ncinput ni = {0};
        uint32_t key = notcurses_get(nc, &ts, &ni);

        if (key == 0 || key == (uint32_t)-1 || key == NCKEY_RESIZE) {
            hw_poll_telemetry(hw);
            continue;
        }

        // Drop rogue CSI sequences or focus reporting codes
        if (key == '[' || key == ']' || key == 0x1b) {
            continue;
        }

        int triggered_action = ACT_NONE;

        // Mouse: first BUTTON1 press only. Drag / leave / edge never switch tabs.
        if (nckey_mouse_p(key)) {
            int mx = ni.x, my = ni.y;
            unsigned dimy = 0, dimx = 0, cdy = 0, cdx = 0;
            ncplane_dim_yx(stdn, &dimy, &dimx);
            ncplane_pixel_geom(stdn, NULL, NULL, &cdy, &cdx, NULL, NULL);
            /* Pixel protocol is much larger than the cell grid. Do not treat
             * a 1-based last-cell (x==dimx) as pixels — that mapped the
             * window edge onto the tab row. */
            if (cdx > 1 && mx >= (int)dimx * 2) mx = mx / (int)cdx;
            if (cdy > 1 && my >= (int)dimy * 2) my = my / (int)cdy;
            if (dimx > 0 && mx >= (int)dimx) mx = (int)dimx - 1;
            if (dimy > 0 && my >= (int)dimy) my = (int)dimy - 1;
            if (mx < 0) mx = 0;
            if (my < 0) my = 0;

            if (s_btn1_down) {
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                long ms = (now.tv_sec - s_btn1_t.tv_sec) * 1000L
                        + (now.tv_nsec - s_btn1_t.tv_nsec) / 1000000L;
                if (ms > 450)
                    s_btn1_down = 0;
            }

            int in_graph = (cur_tab == TAB_FAN && s_gw > 0 &&
                            mx >= s_gx && mx < s_gx + s_gw &&
                            my >= s_gy && my < s_gy + s_gh);
            int on_edge = (mx <= ox || my <= oy || mx >= ox + w - 1 || my >= oy + h - 1);

            if (key != NCKEY_BUTTON1) {
                /* scroll / right-click: ignore */
            } else if (ni.evtype == NCTYPE_RELEASE) {
                s_btn1_down = 0;
            } else if (s_btn1_down || ni.evtype == NCTYPE_REPEAT) {
                /* held-button motion across the tab row must not switch pages */
            } else if (on_edge) {
                s_btn1_down = 1;
                clock_gettime(CLOCK_MONOTONIC, &s_btn1_t);
            } else {
                s_btn1_down = 1;
                clock_gettime(CLOCK_MONOTONIC, &s_btn1_t);
                if (in_graph) {
                    fan_curve_t *fc = fan_edit_gpu ? &hw->fan_gpu : &hw->fan_cpu;
                    int t = 0, p = 0;
                    cell_to_xy(mx, my, &t, &p);
                    int near = (fc->n > 0) ? fan_nearest(fc, mx, my) : -1;
                    int nd = 99;
                    if (near >= 0) {
                        int dx = fan_x(fc, near) - mx;
                        int dy = fan_y(fc, near) - my;
                        nd = dx * dx + dy * dy;
                    }
                    if (near >= 0 && nd <= 2) {
                        fan_edit_idx = near;
                    } else if (fc->n < FAN_POINTS) {
                        int ni = hw_fan_add_point(hw, fan_edit_gpu, t, p);
                        if (ni >= 0) fan_edit_idx = ni;
                    } else {
                        int ni = hw_fan_set_abs(hw, fan_edit_gpu, fan_edit_idx, t, p);
                        if (ni >= 0) fan_edit_idx = ni;
                    }
                    s_sel_pt = fan_edit_idx;
                    fan_fill_xy(fc, fan_edit_idx, fan_xbuf, fan_ybuf);
                    hw->active_input_field = 3;
                } else {
                    int act = find_target(mx, my);
                    if (act >= ACT_TAB_PERF && act <= ACT_TAB_SLOTS) {
                        /* tab row only, inset from the frame so leave-events miss */
                        if (my != oy + 1 || mx <= ox + 1 || mx >= ox + w - 2)
                            act = -1;
                    }
                    if (act > 0 && act != ACT_FAN_GRAPH) {
                        triggered_action = act;
                        if (act == ACT_FOCUS_HEX_INPUT) {
                            hw->active_input_field = 1;
                        } else if (act == ACT_FOCUS_PROFILE_NAME) {
                            hw->active_input_field = 2;
                        } else if (act == ACT_FOCUS_FAN_COORD) {
                            hw->active_input_field = 3;
                        } else if (act == ACT_FOCUS_FAN_Y) {
                            hw->active_input_field = 4;
                        } else {
                            hw->active_input_field = 0;
                        }
                    } else {
                        hw->active_input_field = 0;
                    }
                }
            }
        } else if (key == '[' || key == ']') {
            /* Kitty FocusOut / CSI leftovers */
        } else if (ni.evtype != NCTYPE_RELEASE) {
            // Text input mode for Custom Hex (Tab 3)
            if (hw->active_input_field == 1) {
                if (isxdigit((unsigned char)key)) {
                    size_t len = strlen(hw->hex_input);
                    if (len >= 6) {
                        hw->hex_input[0] = tolower((unsigned char)key);
                        hw->hex_input[1] = '\0';
                    } else {
                        hw->hex_input[len] = tolower((unsigned char)key);
                        hw->hex_input[len + 1] = '\0';
                    }
                    if (strlen(hw->hex_input) == 6) {
                        unsigned int c = 0;
                        if (sscanf(hw->hex_input, "%06x", &c) == 1) hw->custom_rgb = c;
                    }
                    continue;
                } else if (key == NCKEY_BACKSPACE || key == 127 || key == 8) {
                    size_t len = strlen(hw->hex_input);
                    if (len > 0) hw->hex_input[len - 1] = '\0';
                    continue;
                } else if (key == NCKEY_ENTER || key == '\r' || key == '\n') {
                    hw->active_input_field = 0;
                    triggered_action = ACT_APPLY_HEX;
                } else if (key == NCKEY_ESC) {
                    hw->active_input_field = 0;
                    continue;
                } else {
                    // Any navigation key (Tab, j, k, 1..5) unfocuses input and navigates
                    hw->active_input_field = 0;
                }
            }
            // Text input mode for Profile 3-Letter Tag (Tab 5)
            else if (hw->active_input_field == 2) {
                if (isalnum((unsigned char)key)) {
                    size_t len = strlen(hw->profile_name_input);
                    if (len >= 3) {
                        hw->profile_name_input[0] = toupper((unsigned char)key);
                        hw->profile_name_input[1] = '\0';
                    } else {
                        hw->profile_name_input[len] = toupper((unsigned char)key);
                        hw->profile_name_input[len + 1] = '\0';
                    }
                    continue;
                } else if (key == NCKEY_BACKSPACE || key == 127 || key == 8) {
                    size_t len = strlen(hw->profile_name_input);
                    if (len > 0) hw->profile_name_input[len - 1] = '\0';
                    continue;
                } else if (key == NCKEY_ENTER || key == '\r' || key == '\n') {
                    hw->active_input_field = 0;
                    continue;
                } else if (key == NCKEY_ESC) {
                    hw->active_input_field = 0;
                    continue;
                } else {
                    // Any navigation key unfocuses input and navigates
                    hw->active_input_field = 0;
                }
            }
            else if (hw->active_input_field == 3 || hw->active_input_field == 4) {
                char *buf = (hw->active_input_field == 3) ? fan_xbuf : fan_ybuf;
                size_t cap = 7;
                if (key >= '0' && key <= '9') {
                    size_t len = strlen(buf);
                    if (len < cap - 1) {
                        buf[len] = (char)key;
                        buf[len + 1] = '\0';
                    }
                    continue;
                } else if (key == ',' || key == ' ' || key == NCKEY_TAB) {
                    if (hw->active_input_field == 3)
                        hw->active_input_field = 4;
                    else
                        hw->active_input_field = 3;
                    continue;
                } else if (key == NCKEY_BACKSPACE || key == 127 || key == 8) {
                    size_t len = strlen(buf);
                    if (len > 0) buf[len - 1] = '\0';
                    continue;
                } else if (key == NCKEY_ENTER || key == '\r' || key == '\n') {
                    triggered_action = ACT_FAN_COORD_APPLY;
                } else if (key == NCKEY_ESC) {
                    hw->active_input_field = 0;
                    continue;
                } else {
                    hw->active_input_field = 0;
                }
            }

            // Global Keybindings
            if (triggered_action != ACT_NONE) {
                /* input field already chose an action (e.g. Enter on X,Y) */
            } else if (key == 'q' || key == 'Q') {
                running = false;
            } else if (key == '1') {
                cur_tab = TAB_PERF; focus_item = 0;
            } else if (key == '2') {
                cur_tab = TAB_POWER; focus_item = 0;
            } else if (key == '3') {
                cur_tab = TAB_FAN; focus_item = 0;
            } else if (key == '4') {
                cur_tab = TAB_AURA; focus_item = 0;
            } else if (key == '5') {
                cur_tab = TAB_ACV; focus_item = 0;
            } else if (key == '6') {
                cur_tab = TAB_SLOTS; focus_item = 0; profile_count = profile_list(profile_slots, MAX_PROFILES);
            } else if (key == 'H') {
                cur_tab = (cur_tab - 1 + TAB_COUNT) % TAB_COUNT;
                focus_item = 0;
            } else if (key == 'L') {
                cur_tab = (cur_tab + 1) % TAB_COUNT;
                focus_item = 0;
            } else if (key == 'j' || key == NCKEY_DOWN) {
                focus_item++;
            } else if (key == 'k' || key == NCKEY_UP) {
                if (focus_item > 0) focus_item--;
            } else if (key == NCKEY_TAB) {
                if (ni.shift) {
                    if (focus_item > 0) focus_item--;
                } else {
                    focus_item++;
                }
            }
            // Vim adjusters
            else if (key == 'h' || key == NCKEY_LEFT) {
                if (cur_tab == TAB_PERF) {
                    if (focus_item >= 12 && focus_item <= 16) triggered_action = ACT_PPT_DOWN;
                    else if (focus_item >= 9 && focus_item <= 11) triggered_action = ACT_TEMP_DOWN;
                    else triggered_action = ACT_FREQ_DOWN;
                } else if (cur_tab == TAB_POWER) {
                    triggered_action = ACT_BAT_DOWN;
                } else if (cur_tab == TAB_FAN) {
                    if (hw->active_input_field == 3 || focus_item == 4)
                        triggered_action = ACT_FAN_T_DOWN;
                    else
                        triggered_action = ACT_FAN_P_DOWN;
                } else if (cur_tab == TAB_ACV) {
                    if (hw->tint_level > 0) hw->tint_level = 0;
                }
            } else if (key == 'l' || key == NCKEY_RIGHT) {
                if (cur_tab == TAB_PERF) {
                    if (focus_item >= 12 && focus_item <= 16) triggered_action = ACT_PPT_UP;
                    else if (focus_item >= 9 && focus_item <= 11) triggered_action = ACT_TEMP_UP;
                    else triggered_action = ACT_FREQ_UP;
                } else if (cur_tab == TAB_POWER) {
                    triggered_action = ACT_BAT_UP;
                } else if (cur_tab == TAB_FAN) {
                    if (hw->active_input_field == 3 || focus_item == 4)
                        triggered_action = ACT_FAN_T_UP;
                    else
                        triggered_action = ACT_FAN_P_UP;
                } else if (cur_tab == TAB_ACV) {
                    if (hw->tint_level < 1) hw->tint_level = 1;
                }
            }
            // Enter or Space: Activate focused control
            else if (key == NCKEY_ENTER || key == ' ') {
                if (cur_tab == TAB_PERF) {
                    if (focus_item == 0) triggered_action = ACT_PROF_QUIET;
                    else if (focus_item == 1) triggered_action = ACT_PROF_BALANCED;
                    else if (focus_item == 2) triggered_action = ACT_PROF_PERF;
                    else if (focus_item == 3) triggered_action = ACT_EPP_POWER;
                    else if (focus_item == 4) triggered_action = ACT_EPP_BAL_PWR;
                    else if (focus_item == 5) triggered_action = ACT_EPP_BAL_PRF;
                    else if (focus_item == 6) triggered_action = ACT_EPP_PERF;
                    else if (focus_item == 7) triggered_action = ACT_FREQ_DOWN;
                    else if (focus_item == 8) triggered_action = ACT_FREQ_UP;
                    else if (focus_item == 9) triggered_action = ACT_TEMP_TOGGLE;
                    else if (focus_item == 10) triggered_action = ACT_TEMP_DOWN;
                    else if (focus_item == 11) triggered_action = ACT_TEMP_UP;
                    else if (focus_item == 12) triggered_action = ACT_PPT_QUIET;
                    else if (focus_item == 13) triggered_action = ACT_PPT_BAL;
                    else if (focus_item == 14) triggered_action = ACT_PPT_PERF;
                    else if (focus_item == 15) triggered_action = ACT_PPT_DOWN;
                    else if (focus_item == 16) triggered_action = ACT_PPT_UP;
                } else if (cur_tab == TAB_POWER) {
                    if (focus_item == 0) triggered_action = ACT_HZ_60;
                    else if (focus_item == 1) triggered_action = ACT_HZ_MAX;
                    else if (focus_item == 2) triggered_action = ACT_BAT_60;
                    else if (focus_item == 3) triggered_action = ACT_BAT_80;
                    else if (focus_item == 4) triggered_action = ACT_BAT_100;
                    else if (focus_item == 5) triggered_action = ACT_BAT_DOWN;
                    else if (focus_item == 6) triggered_action = ACT_BAT_UP;
                    else if (focus_item == 7) triggered_action = ACT_NV_BOOST_DOWN;
                    else if (focus_item == 8) triggered_action = ACT_NV_BOOST_UP;
                    else if (focus_item == 9) triggered_action = ACT_NV_TEMP_DOWN;
                    else if (focus_item == 10) triggered_action = ACT_NV_TEMP_UP;
                    else if (focus_item == 11) triggered_action = ACT_PANEL_OD;
                    else if (focus_item == 12) triggered_action = ACT_CPU_BOOST;
                    else if (focus_item == 13) triggered_action = ACT_BAT_ONESHOT;
                } else if (cur_tab == TAB_FAN) {
                    if (focus_item == 0) triggered_action = ACT_FAN_EDIT_CPU;
                    else if (focus_item == 1) triggered_action = ACT_FAN_EDIT_GPU;
                    else if (focus_item == 2) triggered_action = ACT_FAN_ADD;
                    else if (focus_item == 3) triggered_action = ACT_FAN_DEL;
                    else if (focus_item == 4) triggered_action = ACT_FOCUS_FAN_COORD;
                    else if (focus_item == 5) triggered_action = ACT_FOCUS_FAN_Y;
                    else if (focus_item == 6) triggered_action = ACT_FAN_COORD_APPLY;
                    else if (focus_item == 7) triggered_action = ACT_FAN_NODE_PREV;
                    else if (focus_item == 8) triggered_action = ACT_FAN_NODE_NEXT;
                    else if (focus_item == 9) triggered_action = ACT_FAN_SILENT;
                    else if (focus_item == 10) triggered_action = ACT_FAN_STOCK;
                    else if (focus_item == 11) triggered_action = ACT_FAN_COOL;
                    else if (focus_item == 12) triggered_action = ACT_FAN_FULL;
                    else if (focus_item == 13) triggered_action = ACT_FAN_TOGGLE;
                    else if (focus_item == 14) triggered_action = ACT_FAN_WRITE;
                } else if (cur_tab == TAB_AURA) {
                    if (focus_item == 0) triggered_action = ACT_AURA_EFF_PREV;
                    else if (focus_item == 1) triggered_action = ACT_AURA_EFF_NEXT;
                    else if (focus_item == 2) triggered_action = ACT_KBD_OFF;
                    else if (focus_item == 3) triggered_action = ACT_KBD_LOW;
                    else if (focus_item == 4) triggered_action = ACT_KBD_MED;
                    else if (focus_item == 5) triggered_action = ACT_KBD_HIGH;
                    else if (focus_item >= 6 && focus_item <= 13) triggered_action = ACT_COLOR_PRESET_BASE + (focus_item - 6);
                    else if (focus_item == 14) triggered_action = ACT_FOCUS_HEX_INPUT;
                    else if (focus_item == 15) triggered_action = ACT_APPLY_HEX;
                    else if (focus_item == 16) triggered_action = ACT_APPLY_THEME_COLOR;
                    else if (focus_item == 17) triggered_action = ACT_TOGGLE_SYNC_WAYBAR;
                } else if (cur_tab == TAB_ACV) {
                    if (focus_item == 0) triggered_action = ACT_THEME_ICE;
                    else if (focus_item == 1) triggered_action = ACT_THEME_TACTICAL;
                    else if (focus_item == 2) triggered_action = ACT_THEME_EMERALD;
                    else if (focus_item == 3) triggered_action = ACT_THEME_CRIMSON;
                    else if (focus_item == 4) triggered_action = ACT_THEME_STEALTH;
                    else if (focus_item == 5) triggered_action = ACT_THEME_SYNC;
                    else if (focus_item == 6) triggered_action = ACT_TINT_0;
                    else if (focus_item == 7) triggered_action = ACT_TINT_1;
                    else if (focus_item == 8) triggered_action = ACT_TOGGLE_SYNC_WAYBAR;
                    else if (focus_item == 9) triggered_action = ACT_TOGGLE_SYNC_KBD;
                    else if (focus_item == 10) triggered_action = ACT_FOCUS_PROFILE_NAME;
                    else if (focus_item == 11) triggered_action = ACT_GEN_RANDOM_NAME;
                    else if (focus_item == 12) triggered_action = ACT_FILTER_EXP_PERF;
                    else if (focus_item == 13) triggered_action = ACT_FILTER_EXP_PWR;
                    else if (focus_item == 14) triggered_action = ACT_FILTER_EXP_AURA;
                    else if (focus_item == 15) triggered_action = ACT_EXPORT_PROFILE;
                    else if (focus_item == 16) triggered_action = ACT_EXPORT_ALL_SKIP;
                    else if (focus_item == 17) triggered_action = ACT_TAB_SLOTS;
                } else if (cur_tab == TAB_SLOTS) {
                    if (focus_item < profile_count) {
                        selected_slot = focus_item;
                    } else if (focus_item == 12) triggered_action = ACT_FILTER_IMP_PERF;
                    else if (focus_item == 13) triggered_action = ACT_FILTER_IMP_PWR;
                    else if (focus_item == 14) triggered_action = ACT_FILTER_IMP_AURA;
                    else if (focus_item == 15) triggered_action = ACT_FILTER_IMP_THEME;
                    else if (focus_item == 16) triggered_action = ACT_IMPORT_PROFILE;
                    else if (focus_item == 17) triggered_action = ACT_IMPORT_ALL_SKIP;
                    else if (focus_item == 18) triggered_action = ACT_DELETE_PROFILE;
                }
            }
        }

        // Action execution
        if (triggered_action != ACT_NONE) {
            // Tabs
            if (triggered_action == ACT_TAB_PERF) { cur_tab = TAB_PERF; focus_item = 0; }
            else if (triggered_action == ACT_TAB_POWER) { cur_tab = TAB_POWER; focus_item = 0; }
            else if (triggered_action == ACT_TAB_FAN) { cur_tab = TAB_FAN; focus_item = 0; }
            else if (triggered_action == ACT_TAB_AURA) { cur_tab = TAB_AURA; focus_item = 0; }
            else if (triggered_action == ACT_TAB_ACV) { cur_tab = TAB_ACV; focus_item = 0; }
            else if (triggered_action == ACT_TAB_SLOTS) { cur_tab = TAB_SLOTS; focus_item = 0; profile_count = profile_list(profile_slots, MAX_PROFILES); }

            // Profiles
            else if (triggered_action == ACT_PROF_QUIET) hw_set_profile(hw, PROF_QUIET);
            else if (triggered_action == ACT_PROF_BALANCED) hw_set_profile(hw, PROF_BALANCED);
            else if (triggered_action == ACT_PROF_PERF) hw_set_profile(hw, PROF_PERFORMANCE);

            // EPP
            else if (triggered_action == ACT_EPP_POWER) hw_set_epp(hw, EPP_POWER);
            else if (triggered_action == ACT_EPP_BAL_PWR) hw_set_epp(hw, EPP_BALANCED_POWER);
            else if (triggered_action == ACT_EPP_BAL_PRF) hw_set_epp(hw, EPP_BALANCED_PERF);
            else if (triggered_action == ACT_EPP_PERF) hw_set_epp(hw, EPP_PERFORMANCE);

            // Freq
            else if (triggered_action == ACT_FREQ_DOWN) hw_set_cpu_max_freq(hw, hw->cpu_target_max_mhz - 100);
            else if (triggered_action == ACT_FREQ_UP) hw_set_cpu_max_freq(hw, hw->cpu_target_max_mhz + 100);
            else if (triggered_action == ACT_TEMP_DOWN) hw_set_temp_cap(hw, hw->cpu_temp_cap_c - 5);
            else if (triggered_action == ACT_TEMP_UP) hw_set_temp_cap(hw, hw->cpu_temp_cap_c + 5);
            else if (triggered_action == ACT_TEMP_TOGGLE)
                hw_set_temp_cap_enabled(hw, !hw->cpu_temp_cap_on);

            // Display Hz
            else if (triggered_action == ACT_HZ_60) hw_set_display_hz(hw, 60);
            else if (triggered_action == ACT_HZ_MAX) hw_set_display_hz(hw, hw->display_max_hz);

            // Battery limit
            else if (triggered_action == ACT_BAT_60) hw_set_battery_limit(hw, 60);
            else if (triggered_action == ACT_BAT_80) hw_set_battery_limit(hw, 80);
            else if (triggered_action == ACT_BAT_100) hw_set_battery_limit(hw, 100);
            else if (triggered_action == ACT_BAT_DOWN)
                hw_set_battery_limit(hw, hw->battery_charge_limit - 5);
            else if (triggered_action == ACT_BAT_UP)
                hw_set_battery_limit(hw, hw->battery_charge_limit + 5);
            else if (triggered_action == ACT_FAN_SILENT) hw_fan_preset(hw, 1);
            else if (triggered_action == ACT_FAN_STOCK) hw_fan_preset(hw, 0);
            else if (triggered_action == ACT_FAN_COOL) hw_fan_preset(hw, 2);
            else if (triggered_action == ACT_FAN_FULL) hw_fan_preset(hw, 3);
            else if (triggered_action == ACT_FAN_TOGGLE)
                hw_fan_enable(hw, !(hw->fan_cpu_on || hw->fan_gpu_on),
                              !(hw->fan_cpu_on || hw->fan_gpu_on));
            else if (triggered_action == ACT_PPT_QUIET) {
                if (hw->battery_ac_connected) hw_set_ppt(hw, 45, 55, 55);
                else hw_set_ppt(hw, 35, 45, 45);
            } else if (triggered_action == ACT_PPT_BAL) {
                if (hw->battery_ac_connected) hw_set_ppt(hw, 60, 75, 75);
                else hw_set_ppt(hw, 45, 54, 54);
            } else if (triggered_action == ACT_PPT_PERF) {
                if (hw->battery_ac_connected) hw_set_ppt(hw, 80, 80, 80);
                else hw_set_ppt(hw, 65, 65, 65);
            } else if (triggered_action == ACT_PPT_DOWN) {
                int s = hw->ppt_spl > 5 ? hw->ppt_spl : (hw->battery_ac_connected ? 60 : 45);
                hw_set_ppt(hw, s - 5, s, s);
            } else if (triggered_action == ACT_PPT_UP) {
                int s = hw->ppt_spl > 5 ? hw->ppt_spl : (hw->battery_ac_connected ? 60 : 45);
                hw_set_ppt(hw, s + 5, s + 10, s + 10);
            } else if (triggered_action == ACT_NV_BOOST_DOWN)
                hw_set_nv_boost(hw, hw->nv_boost_w - 5);
            else if (triggered_action == ACT_NV_BOOST_UP)
                hw_set_nv_boost(hw, hw->nv_boost_w + 5);
            else if (triggered_action == ACT_NV_TEMP_DOWN)
                hw_set_nv_temp(hw, hw->nv_temp_target - 1);
            else if (triggered_action == ACT_NV_TEMP_UP)
                hw_set_nv_temp(hw, hw->nv_temp_target + 1);
            else if (triggered_action == ACT_PANEL_OD)
                hw_set_panel_od(hw, !hw->panel_od);
            else if (triggered_action == ACT_CPU_BOOST)
                hw_set_cpu_boost(hw, !hw->cpu_boost);
            else if (triggered_action == ACT_BAT_ONESHOT)
                hw_battery_oneshot(hw);
            else if (triggered_action == ACT_FAN_EDIT_CPU) {
                fan_edit_gpu = 0;
                {
                    fan_curve_t *fc = &hw->fan_cpu;
                    if (fc->n > 0 && fan_edit_idx >= fc->n) fan_edit_idx = fc->n - 1;
                    if (fan_edit_idx < 0) fan_edit_idx = 0;
                    s_sel_pt = (fc->n > 0) ? fan_edit_idx : -1;
                    fan_fill_xy(fc, fan_edit_idx, fan_xbuf, fan_ybuf);
                }
            } else if (triggered_action == ACT_FAN_EDIT_GPU) {
                fan_edit_gpu = 1;
                {
                    fan_curve_t *fc = &hw->fan_gpu;
                    if (fc->n > 0 && fan_edit_idx >= fc->n) fan_edit_idx = fc->n - 1;
                    if (fan_edit_idx < 0) fan_edit_idx = 0;
                    s_sel_pt = (fc->n > 0) ? fan_edit_idx : -1;
                    fan_fill_xy(fc, fan_edit_idx, fan_xbuf, fan_ybuf);
                }
            } else if (triggered_action == ACT_FAN_NODE_PREV) {
                if (fan_edit_idx > 0) fan_edit_idx--;
                {
                    fan_curve_t *fc = fan_edit_gpu ? &hw->fan_gpu : &hw->fan_cpu;
                    s_sel_pt = fan_edit_idx;
                    fan_fill_xy(fc, fan_edit_idx, fan_xbuf, fan_ybuf);
                }
            } else if (triggered_action == ACT_FAN_NODE_NEXT) {
                fan_curve_t *fc = fan_edit_gpu ? &hw->fan_gpu : &hw->fan_cpu;
                if (fan_edit_idx + 1 < fc->n) fan_edit_idx++;
                s_sel_pt = fan_edit_idx;
                fan_fill_xy(fc, fan_edit_idx, fan_xbuf, fan_ybuf);
            } else if (triggered_action == ACT_FAN_T_DOWN) {
                int ni = hw_fan_nudge_point(hw, fan_edit_gpu, fan_edit_idx, -1, 0);
                if (ni >= 0) fan_edit_idx = ni;
                fan_fill_xy(fan_edit_gpu ? &hw->fan_gpu : &hw->fan_cpu,
                            fan_edit_idx, fan_xbuf, fan_ybuf);
            } else if (triggered_action == ACT_FAN_T_UP) {
                int ni = hw_fan_nudge_point(hw, fan_edit_gpu, fan_edit_idx, 1, 0);
                if (ni >= 0) fan_edit_idx = ni;
                fan_fill_xy(fan_edit_gpu ? &hw->fan_gpu : &hw->fan_cpu,
                            fan_edit_idx, fan_xbuf, fan_ybuf);
            } else if (triggered_action == ACT_FAN_P_DOWN) {
                int ni = hw_fan_nudge_point(hw, fan_edit_gpu, fan_edit_idx, 0, -5);
                if (ni >= 0) fan_edit_idx = ni;
                fan_fill_xy(fan_edit_gpu ? &hw->fan_gpu : &hw->fan_cpu,
                            fan_edit_idx, fan_xbuf, fan_ybuf);
            } else if (triggered_action == ACT_FAN_P_UP) {
                int ni = hw_fan_nudge_point(hw, fan_edit_gpu, fan_edit_idx, 0, 5);
                if (ni >= 0) fan_edit_idx = ni;
                fan_fill_xy(fan_edit_gpu ? &hw->fan_gpu : &hw->fan_cpu,
                            fan_edit_idx, fan_xbuf, fan_ybuf);
            } else if (triggered_action == ACT_FAN_WRITE)
                hw_fan_apply(hw);
            else if (triggered_action == ACT_FAN_ADD) {
                fan_curve_t *fc = fan_edit_gpu ? &hw->fan_gpu : &hw->fan_cpu;
                if (fc->n >= FAN_POINTS) {
                    log_add("Fan: 8 points max");
                } else {
                    int t = fan_xbuf[0] ? atoi(fan_xbuf) : -1;
                    int p = fan_ybuf[0] ? atoi(fan_ybuf) : -1;
                    int ni = hw_fan_add_point(hw, fan_edit_gpu, t, p);
                    if (ni >= 0) fan_edit_idx = ni;
                    s_sel_pt = fan_edit_idx;
                    fan_fill_xy(fc, fan_edit_idx, fan_xbuf, fan_ybuf);
                    hw->active_input_field = 3;
                    log_add("Fan: pt %d at x=%d y=%d", fan_edit_idx + 1,
                            fc->temp_c[fan_edit_idx], fc->pwm[fan_edit_idx]);
                }
            } else if (triggered_action == ACT_FAN_DEL) {
                fan_curve_t *fc = fan_edit_gpu ? &hw->fan_gpu : &hw->fan_cpu;
                if (fc->n < 1) {
                    log_add("Fan: no points");
                } else {
                    hw_fan_del_point(hw, fan_edit_gpu, fan_edit_idx);
                    if (fan_edit_idx >= fc->n) fan_edit_idx = fc->n - 1;
                    if (fan_edit_idx < 0) fan_edit_idx = 0;
                    s_sel_pt = (fc->n > 0) ? fan_edit_idx : -1;
                    fan_fill_xy(fc, fan_edit_idx, fan_xbuf, fan_ybuf);
                    log_add("Fan: %d points", fc->n);
                }
            } else if (triggered_action >= ACT_FAN_SEL_BASE &&
                       triggered_action < ACT_FAN_SEL_BASE + FAN_POINTS) {
                fan_curve_t *fc = fan_edit_gpu ? &hw->fan_gpu : &hw->fan_cpu;
                fan_edit_idx = triggered_action - ACT_FAN_SEL_BASE;
                if (fan_edit_idx >= fc->n) fan_edit_idx = fc->n - 1;
                if (fan_edit_idx < 0) fan_edit_idx = 0;
                s_sel_pt = fan_edit_idx;
                fan_fill_xy(fc, fan_edit_idx, fan_xbuf, fan_ybuf);
                hw->active_input_field = 3;
            } else if (triggered_action == ACT_FOCUS_FAN_COORD) {
                hw->active_input_field = 3;
            } else if (triggered_action == ACT_FOCUS_FAN_Y) {
                hw->active_input_field = 4;
            } else if (triggered_action == ACT_FAN_COORD_APPLY) {
                int t = atoi(fan_xbuf);
                int p = atoi(fan_ybuf);
                int ni = hw_fan_set_abs(hw, fan_edit_gpu, fan_edit_idx, t, p);
                if (ni >= 0) fan_edit_idx = ni;
                fan_curve_t *fc = fan_edit_gpu ? &hw->fan_gpu : &hw->fan_cpu;
                fan_fill_xy(fc, fan_edit_idx, fan_xbuf, fan_ybuf);
                s_sel_pt = fan_edit_idx;
                log_add("Fan pt %d = x=%d°C y=%d", fan_edit_idx + 1,
                        fc->temp_c[fan_edit_idx], fc->pwm[fan_edit_idx]);
                hw->active_input_field = 0;
            }

            // Aura keyboard
            else if (triggered_action == ACT_AURA_EFF_PREV) {
                int next = (hw->aura_effect_idx - 1 + AURA_EFFECT_COUNT) % AURA_EFFECT_COUNT;
                hw_set_aura(hw, next, hw->aura_color_idx);
            } else if (triggered_action == ACT_AURA_EFF_NEXT) {
                int next = (hw->aura_effect_idx + 1) % AURA_EFFECT_COUNT;
                hw_set_aura(hw, next, hw->aura_color_idx);
            } else if (triggered_action == ACT_KBD_OFF) hw_set_kbd_brightness(hw, KBD_OFF);
            else if (triggered_action == ACT_KBD_LOW) hw_set_kbd_brightness(hw, KBD_LOW);
            else if (triggered_action == ACT_KBD_MED) hw_set_kbd_brightness(hw, KBD_MED);
            else if (triggered_action == ACT_KBD_HIGH) hw_set_kbd_brightness(hw, KBD_HIGH);

            // Color Presets (Prefixes)
            else if (triggered_action >= ACT_COLOR_PRESET_BASE && triggered_action < ACT_COLOR_PRESET_BASE + 8) {
                int c_idx = triggered_action - ACT_COLOR_PRESET_BASE;
                hw->aura_color_idx = c_idx;
                hw_set_aura(hw, hw->aura_effect_idx, c_idx);
                snprintf(hw->hex_input, sizeof(hw->hex_input), "%s", AURA_COLOR_HEX[c_idx]);
                hw->custom_rgb = AURA_COLOR_RGB[c_idx];
                snprintf(hw->custom_hex, sizeof(hw->custom_hex), "%s", AURA_COLOR_HEX[c_idx]);
                if (hw->sync_with_waybar) hw_sync_waybar_color(hw->custom_hex);
            }
            // Custom Hex Entry
            else if (triggered_action == ACT_FOCUS_HEX_INPUT) {
                hw->active_input_field = 1;
            } else if (triggered_action == ACT_APPLY_HEX) {
                if (strlen(hw->hex_input) >= 6) {
                    hw_set_aura_hex(hw, hw->hex_input);
                    if (hw->theme == THEME_BACKLIGHT_SYNC) {
                        hw->custom_rgb = ((unsigned int)strtoul(hw->hex_input, NULL, 16));
                    }
                }
            } else if (triggered_action == ACT_APPLY_THEME_COLOR) {
                hw_read_backlight_hex(hw->custom_hex, &hw->custom_rgb);
                hw->theme = THEME_BACKLIGHT_SYNC;
            }

            // ACV Tab: Themes
            else if (triggered_action == ACT_THEME_ICE) hw->theme = THEME_TUF_ICE;
            else if (triggered_action == ACT_THEME_TACTICAL) hw->theme = THEME_TACTICAL;
            else if (triggered_action == ACT_THEME_EMERALD) hw->theme = THEME_EMERALD;
            else if (triggered_action == ACT_THEME_CRIMSON) hw->theme = THEME_CRIMSON;
            else if (triggered_action == ACT_THEME_STEALTH) hw->theme = THEME_STEALTH;
            else if (triggered_action == ACT_THEME_SYNC) {
                hw_read_backlight_hex(hw->custom_hex, &hw->custom_rgb);
                hw->theme = THEME_BACKLIGHT_SYNC;
            }

            // Tint levels (2 modes: 0=Clear Glass, 1=Solid Opaque)
            else if (triggered_action == ACT_TINT_0) hw->tint_level = 0;
            else if (triggered_action == ACT_TINT_1) hw->tint_level = 1;

            // Integrations & Sync
            else if (triggered_action == ACT_TOGGLE_SYNC_WAYBAR) {
                hw->sync_with_waybar = !hw->sync_with_waybar;
                hw_waybar_sync_enable(hw->sync_with_waybar);
                if (hw->sync_with_waybar) hw_sync_waybar_color(hw->custom_hex);
                log_add("Waybar Sync: %s", hw->sync_with_waybar ? "ON" : "OFF");
            } else if (triggered_action == ACT_TOGGLE_SYNC_KBD) {
                hw->sync_with_backlight = !hw->sync_with_backlight;
                log_add("Backlight Sync: %s", hw->sync_with_backlight ? "ON" : "OFF");
            }

            // Profiles: Naming & Filters
            else if (triggered_action == ACT_FOCUS_PROFILE_NAME) {
                hw->active_input_field = 2;
            } else if (triggered_action == ACT_GEN_RANDOM_NAME) {
                profile_gen_random_name(hw->profile_name_input);
            } else if (triggered_action == ACT_FILTER_EXP_PERF) {
                exp_filter.include_perf = !exp_filter.include_perf;
            } else if (triggered_action == ACT_FILTER_EXP_PWR) {
                exp_filter.include_power = !exp_filter.include_power;
            } else if (triggered_action == ACT_FILTER_EXP_AURA) {
                exp_filter.include_aura = !exp_filter.include_aura;
            } else if (triggered_action == ACT_EXPORT_PROFILE) {
                profile_export(hw->profile_name_input, hw, &exp_filter);
                profile_count = profile_list(profile_slots, MAX_PROFILES);
                profile_gen_random_name(hw->profile_name_input);
            } else if (triggered_action == ACT_EXPORT_ALL_SKIP) {
                profile_export(hw->profile_name_input, hw, NULL);
                profile_count = profile_list(profile_slots, MAX_PROFILES);
                profile_gen_random_name(hw->profile_name_input);
            } else if (triggered_action >= ACT_SLOT_PROFILE_BASE && triggered_action < ACT_SLOT_PROFILE_BASE + profile_count) {
                selected_slot = triggered_action - ACT_SLOT_PROFILE_BASE;
            } else if (triggered_action == ACT_IMPORT_PROFILE) {
                if (profile_count > 0 && selected_slot < profile_count) {
                    profile_import(profile_slots[selected_slot], hw, &imp_filter);
                }
            } else if (triggered_action == ACT_IMPORT_ALL_SKIP) {
                if (profile_count > 0 && selected_slot < profile_count) {
                    profile_import(profile_slots[selected_slot], hw, NULL);
                }
            } else if (triggered_action == ACT_DELETE_PROFILE) {
                if (profile_count > 0 && selected_slot < profile_count) {
                    profile_delete(profile_slots[selected_slot]);
                    profile_count = profile_list(profile_slots, MAX_PROFILES);
                    if (selected_slot >= profile_count && profile_count > 0) {
                        selected_slot = profile_count - 1;
                    }
                }
            }

            settings_save(hw);
        }
    }

    settings_save(hw);
    notcurses_stop(nc);
    return 0;
}
