#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ui_internal.h"
#include "../control.h"
#include "../util.h"

#include <stdio.h>
#include <string.h>

/* Left-bottom panel: quick control rows. h/l rotate the value, Enter/space
 * applies it, some rows jump to the matching workspace view.
 *
 * Rows:
 *   0 mode      rotate user modes, Enter applies the bundle
 *   1 profile   Quiet/Balanced/Performance
 *   2 fan       Stock/Silent/Cool/Full/Off
 *   3 fan curve opens the fan editor in the workspace
 *   4 hz        rotate supported refresh rates
 *   5 battery   ±5 with h/l, Enter applies
 *   6 kbd       off/low/med/high
 *   7 settings  opens the settings view */

#define CTL_ROWS 8

static const char *const FAN_OPTS[] = { "Stock", "Silent", "Cool", "Full", "Off" };
#define FAN_OPTS_N 5

static const char *fan_val(void)
{
    return FAN_OPTS[ut_clamp_i(g_ui.ctl_fan_idx, 0, FAN_OPTS_N - 1)];
}

static const char *hz_val(void)
{
    hw_state_t *hw = g_ui.hw;
    if (hw->hz_count == 0)
        return "--";
    g_ui.ctl_hz_idx = ut_clamp_i(g_ui.ctl_hz_idx, 0, hw->hz_count - 1);
    static char buf[16];
    snprintf(buf, sizeof(buf), "%d Hz", hw->hz_modes[g_ui.ctl_hz_idx]);
    return buf;
}

static const char *mode_val(void)
{
    if (g_ui.mode_n == 0)
        return "(none)";
    g_ui.ctl_mode_idx = ut_clamp_i(g_ui.ctl_mode_idx, 0, g_ui.mode_n - 1);
    return g_ui.modes[g_ui.ctl_mode_idx].name;
}

static const char *battery_val(void)
{
    static char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", g_ui.ctl_bat);
    return buf;
}

static void apply_row(int row)
{
    hw_state_t *hw = g_ui.hw;
    switch (row) {
    case 0:
        if (g_ui.mode_n > 0) {
            char err[128];
            if (mode_apply(hw, g_ui.modes[g_ui.ctl_mode_idx].steps, err, sizeof(err)) == 0)
                ut_log("mode '%s' applied", g_ui.modes[g_ui.ctl_mode_idx].name);
            else
                ut_log("mode '%s': %s", g_ui.modes[g_ui.ctl_mode_idx].name,
                       err[0] ? err : "failed");
            hw_refresh_live(hw);
        }
        break;
    case 1:
        ctrl_set_profile(hw, (hw_profile_t)g_ui.ctl_prof_idx);
        break;
    case 2:
        if (g_ui.ctl_fan_idx == 4)
            ctrl_fan_set_enabled(hw, false, false);
        else
            ctrl_fan_preset(hw, g_ui.ctl_fan_idx);
        break;
    case 3:
        g_ui.focus = FOC_WORKSPACE;
        ws_set_view(WSV_FAN);
        break;
    case 4:
        if (hw->hz_count > 0)
            ctrl_set_hz(hw, hw->hz_modes[g_ui.ctl_hz_idx]);
        break;
    case 5:
        ctrl_set_battery_limit(hw, g_ui.ctl_bat);
        break;
    case 6:
        ctrl_set_kbd(hw, (hw_kbd_t)g_ui.ctl_kbd_idx);
        break;
    case 7:
        g_ui.focus = FOC_WORKSPACE;
        ws_set_view(WSV_SETTINGS);
        break;
    default:
        break;
    }
}

void panel_controls_draw(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    hw_state_t *hw = g_ui.hw;

    ui_box(n, r, "CONTROLS", g_ui.focus == FOC_CONTROLS);
    int x = r->x + 2, w = r->w - 4;
    if (w < 10)
        return;

    ui_putln(n, x, r->y + 1, w, "h/l change · Enter apply", pal->muted, false);

    static const char *const PROFS[] = { "Quiet", "Balanced", "Performance" };
    static const char *const KBDS[] = { "off", "low", "med", "high" };

    const char *vals[CTL_ROWS];
    vals[0] = mode_val();
    vals[1] = PROFS[ut_clamp_i(g_ui.ctl_prof_idx, 0, 2)];
    vals[2] = fan_val();
    vals[3] = "open editor >";
    vals[4] = hz_val();
    vals[5] = battery_val();
    vals[6] = KBDS[ut_clamp_i(g_ui.ctl_kbd_idx, 0, 3)];
    vals[7] = "open >";

    static const char *const labels[CTL_ROWS] = {
        "Mode", "Profile", "Fan", "Fan curve", "Refresh", "Battery", "Kbd", "Settings"
    };

    int rows = r->h - 3;
    for (int i = 0; i < CTL_ROWS && i < rows; i++) {
        int y = r->y + 2 + i;
        bool sel = (i == g_ui.ctl_sel && g_ui.focus == FOC_CONTROLS);
        ui_row(n, x, y, w, labels[i], vals[i], sel);
        tgt_register(x, y, w, 1, TGT(TGT_PANEL_CONTROLS, i + 1));
    }
    (void)hw;
}

void panel_controls_key(uint32_t key)
{
    hw_state_t *hw = g_ui.hw;
    switch (key) {
    case 'j':
    case NCKEY_DOWN:
        if (g_ui.ctl_sel + 1 < CTL_ROWS)
            g_ui.ctl_sel++;
        break;
    case 'k':
    case NCKEY_UP:
        if (g_ui.ctl_sel > 0)
            g_ui.ctl_sel--;
        break;
    case 'h':
    case NCKEY_LEFT:
        switch (g_ui.ctl_sel) {
        case 0:
            if (g_ui.mode_n > 0)
                g_ui.ctl_mode_idx = (g_ui.ctl_mode_idx + g_ui.mode_n - 1) % g_ui.mode_n;
            break;
        case 1:
            g_ui.ctl_prof_idx = (g_ui.ctl_prof_idx + 2) % 3;
            break;
        case 2:
            g_ui.ctl_fan_idx = (g_ui.ctl_fan_idx + FAN_OPTS_N - 1) % FAN_OPTS_N;
            break;
        case 4:
            if (hw->hz_count > 0)
                g_ui.ctl_hz_idx = (g_ui.ctl_hz_idx + hw->hz_count - 1) % hw->hz_count;
            break;
        case 5:
            g_ui.ctl_bat = ut_clamp_i(g_ui.ctl_bat - 5, 20, 100);
            break;
        case 6:
            g_ui.ctl_kbd_idx = (g_ui.ctl_kbd_idx + 3) % 4;
            break;
        default:
            break;
        }
        break;
    case 'l':
    case NCKEY_RIGHT:
        switch (g_ui.ctl_sel) {
        case 0:
            if (g_ui.mode_n > 0)
                g_ui.ctl_mode_idx = (g_ui.ctl_mode_idx + 1) % g_ui.mode_n;
            break;
        case 1:
            g_ui.ctl_prof_idx = (g_ui.ctl_prof_idx + 1) % 3;
            break;
        case 2:
            g_ui.ctl_fan_idx = (g_ui.ctl_fan_idx + 1) % FAN_OPTS_N;
            break;
        case 4:
            if (hw->hz_count > 0)
                g_ui.ctl_hz_idx = (g_ui.ctl_hz_idx + 1) % hw->hz_count;
            break;
        case 5:
            g_ui.ctl_bat = ut_clamp_i(g_ui.ctl_bat + 5, 20, 100);
            break;
        case 6:
            g_ui.ctl_kbd_idx = (g_ui.ctl_kbd_idx + 1) % 4;
            break;
        default:
            break;
        }
        break;
    case NCKEY_ENTER:
    case '\r':
    case '\n':
    case ' ':
        apply_row(g_ui.ctl_sel);
        break;
    default:
        break;
    }
}

void panel_controls_act(int id)
{
    int row = id - 1;
    if (row < 0 || row >= CTL_ROWS)
        return;
    if (row == g_ui.ctl_sel)
        apply_row(row); /* click on the selected row applies */
    else
        g_ui.ctl_sel = row;
}
