#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "ui_internal.h"
#include "../control.h"
#include "../util.h"

#include <stdio.h>
#include <string.h>

/* Right-top workspace. The left column decides what is shown here via
 * ws_set_view(); workspace-local keys F/P/L/S/E switch views directly. */

enum {
    ACT_WS_TAB_FAN = 1,
    ACT_WS_TAB_POWER,
    ACT_WS_TAB_LIGHT,
    ACT_WS_TAB_SETTINGS,
    ACT_WS_TAB_HELP,
    /* power view rows */
    ACT_WS_PW_BASE = 20,   /* + row index */
    /* light view rows */
    ACT_WS_LT_BASE = 40,   /* + row index */
};

static const char *const WS_NAMES[WSV_COUNT] = {
    "FAN CURVE", "POWER", "LIGHT", "HELP"
};

void ws_set_view(ws_view_t v)
{
    g_ui.ws_view = v;
}

/* ---- POWER view -------------------------------------------------------- */

enum {
    PW_SPL = 0,
    PW_SPPT,
    PW_FPPT,
    PW_PRESET_Q,
    PW_PRESET_B,
    PW_PRESET_P,
    PW_PPT_LIMITS,
    PW_NVBOOST,
    PW_NVTEMP,
    PW_PANEL_OD,
    PW_CPUBOOST,
    PW_ROWS
};

static void pw_apply_row(int row)
{
    hw_state_t *hw = g_ui.hw;
    switch (row) {
    case PW_SPL:
        ctrl_set_ppt(hw, (hw->ppt_spl > 0 ? hw->ppt_spl : 45) + 5,
                     hw->ppt_sppt, hw->ppt_fppt);
        break;
    case PW_SPPT:
        ctrl_set_ppt(hw, hw->ppt_spl,
                     (hw->ppt_sppt > 0 ? hw->ppt_sppt : 55) + 5, hw->ppt_fppt);
        break;
    case PW_FPPT:
        ctrl_set_ppt(hw, hw->ppt_spl, hw->ppt_sppt,
                     (hw->ppt_fppt > 0 ? hw->ppt_fppt : 55) + 5);
        break;
    case PW_PRESET_Q:
        ctrl_set_ppt(hw, 45, 55, 55);
        break;
    case PW_PRESET_B:
        ctrl_set_ppt(hw, 60, 75, 75);
        break;
    case PW_PRESET_P:
        ctrl_set_ppt(hw, 80, 80, 80);
        break;
    case PW_PPT_LIMITS:
        if (hw->ppt_off)
            ctrl_ppt_restore(hw);
        else
            ctrl_ppt_off(hw);
        break;
    case PW_NVBOOST:
        ctrl_set_nv_boost(hw, (hw->nv_boost > 0 ? hw->nv_boost : 5) + 5);
        break;
    case PW_NVTEMP:
        ctrl_set_nv_temp(hw, (hw->nv_temp > 0 ? hw->nv_temp : 75) + 1);
        break;
    case PW_PANEL_OD:
        ctrl_set_panel_od(hw, !hw->panel_od);
        break;
    case PW_CPUBOOST:
        ctrl_set_cpu_boost(hw, !hw->cpu_boost);
        break;
    default:
        break;
    }
}

static void pw_dec_row(int row)
{
    hw_state_t *hw = g_ui.hw;
    switch (row) {
    case PW_SPL:
        ctrl_set_ppt(hw, (hw->ppt_spl > 0 ? hw->ppt_spl : 45) - 5,
                     hw->ppt_sppt, hw->ppt_fppt);
        break;
    case PW_SPPT:
        ctrl_set_ppt(hw, hw->ppt_spl,
                     (hw->ppt_sppt > 0 ? hw->ppt_sppt : 55) - 5, hw->ppt_fppt);
        break;
    case PW_FPPT:
        ctrl_set_ppt(hw, hw->ppt_spl, hw->ppt_sppt,
                     (hw->ppt_fppt > 0 ? hw->ppt_fppt : 55) - 5);
        break;
    case PW_NVBOOST:
        ctrl_set_nv_boost(hw, (hw->nv_boost > 5 ? hw->nv_boost : 10) - 5);
        break;
    case PW_NVTEMP:
        ctrl_set_nv_temp(hw, (hw->nv_temp > 75 ? hw->nv_temp : 76) - 1);
        break;
    case PW_PPT_LIMITS:
        /* toggle row: both directions do the same */
        if (hw->ppt_off)
            ctrl_ppt_restore(hw);
        else
            ctrl_ppt_off(hw);
        break;
    default:
        break;
    }
}

static void draw_power(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    hw_state_t *hw = g_ui.hw;
    int x = r->x + 2, w = r->w - 4;

    char spl[16], sppt[16], fppt[16], nvb[16], nvt[16];
    if (hw->ppt_spl > 0)  snprintf(spl,  sizeof(spl),  "%d W",  hw->ppt_spl);
    else                  snprintf(spl,  sizeof(spl),  "--");
    if (hw->ppt_sppt > 0) snprintf(sppt, sizeof(sppt), "%d W",  hw->ppt_sppt);
    else                  snprintf(sppt, sizeof(sppt), "--");
    if (hw->ppt_fppt > 0) snprintf(fppt, sizeof(fppt), "%d W",  hw->ppt_fppt);
    else                  snprintf(fppt, sizeof(fppt), "--");
    if (hw->nv_boost > 0) snprintf(nvb, sizeof(nvb), "%d W", hw->nv_boost);
    else                  snprintf(nvb, sizeof(nvb), "--");
    if (hw->nv_temp > 0)  snprintf(nvt, sizeof(nvt), "%d °C", hw->nv_temp);
    else                  snprintf(nvt, sizeof(nvt), "--");

    const char *vals[PW_ROWS] = {
        spl, sppt, fppt,
        "apply", "apply", "apply",
        hw->ppt_off ? "removed (max)" : "on",
        nvb, nvt,
        hw->panel_od ? "on" : "off",
        hw->cpu_boost ? "on" : "off",
    };
    static const char *const labels[PW_ROWS] = {
        "SPL (sustained)", "SPPT (slow boost)", "FPPT (fast boost)",
        "Preset Q45", "Preset B60", "Preset P80",
        "PPT limits",
        "NV dynamic boost", "NV temp target",
        "Panel overdrive", "CPU boost",
    };

    ui_putln(n, x, r->y + 1, w, "h/l adjust · Enter apply (presets/toggles)", pal->muted, false);
    int rows = r->h - 3;
    for (int i = 0; i < PW_ROWS && i < rows; i++) {
        int y = r->y + 2 + i;
        bool sel = (i == g_ui.pw_sel);
        ui_row(n, x, y, w, labels[i], vals[i], sel);
        tgt_register(x, y, w, 1, TGT(TGT_PANEL_WORKSPACE, ACT_WS_PW_BASE + i));
    }
}

/* ---- LIGHT view -------------------------------------------------------- */

enum {
    LT_KBD = 0,
    LT_EFFECT,
    LT_COLOR,
    LT_HEX,
    LT_ROWS
};

static void lt_apply_row(int row)
{
    hw_state_t *hw = g_ui.hw;
    switch (row) {
    case LT_KBD:
        ctrl_set_kbd(hw, (hw_kbd_t)g_ui.ctl_kbd_idx);
        break;
    case LT_EFFECT:
    case LT_COLOR:
        ctrl_set_aura(hw, g_ui.lt_eff, g_ui.lt_col);
        break;
    case LT_HEX:
        if (strlen(g_ui.lt_hex.buf) == 6)
            ctrl_set_aura_hex(hw, g_ui.lt_hex.buf);
        else
            ut_log("hex: need 6 digits");
        break;
    default:
        break;
    }
}

static void draw_light(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int x = r->x + 2, w = r->w - 4;

    static const char *const KBDS[] = { "off", "low", "med", "high" };
    char hexline[128];
    if (g_ui.lt_hex_active)
        snprintf(hexline, sizeof(hexline), "#%s_", g_ui.lt_hex.buf);
    else
        snprintf(hexline, sizeof(hexline), "#%s (type 0-9a-f, Enter applies)",
                 g_ui.lt_hex.buf);

    const char *vals[LT_ROWS] = {
        KBDS[ut_clamp_i(g_ui.ctl_kbd_idx, 0, 3)],
        AURA_EFFECTS[ut_clamp_i(g_ui.lt_eff, 0, AURA_EFFECT_COUNT - 1)],
        AURA_COLOR_NAMES[ut_clamp_i(g_ui.lt_col, 0, AURA_COLOR_COUNT - 1)],
        hexline,
    };
    static const char *const labels[LT_ROWS] = {
        "Kbd brightness", "Aura effect", "Aura color", "Custom hex",
    };

    ui_putln(n, x, r->y + 1, w, "h/l change · Enter apply · x to type hex", pal->muted, false);
    int rows = r->h - 3;
    for (int i = 0; i < LT_ROWS && i < rows; i++) {
        int y = r->y + 2 + i;
        bool sel = (i == g_ui.lt_sel);
        ui_row(n, x, y, w, labels[i], vals[i], sel);
        tgt_register(x, y, w, 1, TGT(TGT_PANEL_WORKSPACE, ACT_WS_LT_BASE + i));
    }

    /* swatch */
    uint64_t ch = 0;
    unsigned int hr = 0, hg = 0, hb = 0;
    if (strlen(g_ui.lt_hex.buf) == 6)
        sscanf(g_ui.lt_hex.buf, "%02x%02x%02x", &hr, &hg, &hb);
    ncchannels_set_fg_rgb(&ch, (hr << 16) | (hg << 8) | hb);
    ncplane_set_channels(n, ch);
    ncplane_putstr_yx(n, r->y + 2 + LT_HEX, x + 40, "██████");
}

/* ---- HELP view --------------------------------------------------------- */

static void draw_help(struct ncplane *n, const rect_t *r)
{
    const palette_t *pal = ui_palette(g_prefs.theme);
    int x = r->x + 2, w = r->w - 4;
    const char *lines[] = {
        "GLOBAL",
        "  q            quit (saves settings)",
        "  1..4 / Tab   focus: profiles, controls, workspace, telemetry",
        "  esc / s      settings overlay · ? this help",
        "",
        "CONTROLS / LISTS",
        "  j k          move · h l change value · Enter apply",
        "",
        "FAN EDITOR",
        "  c g          switch cpu/gpu curve",
        "  + -          add/remove point · w write to EC",
        "  h l j k      nudge selected point (temp / pwm)",
        "  t p          type exact temp/pwm, Enter sets",
        "  o            toggle curve on/off · click graph to add/select",
        "",
        "PROFILES",
        "  Enter apply · s save current · d delete · n new name",
        "",
        "PRIVILEGES",
        "  writes go asusctl -> direct sysfs -> sudo -n;",
        "  without passwordless sudo some nodes report failure in the log.",
    };
    int y = r->y + 1;
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]) && y < r->y + r->h - 1; i++, y++)
        ui_putln(n, x, y, w, lines[i],
                 lines[i][0] && lines[i][1] == ' ' ? pal->text : pal->accent,
                 lines[i][0] && lines[i][1] != ' ');
}

/* ---- dispatcher -------------------------------------------------------- */

void panel_workspace_draw(struct ncplane *n, const rect_t *r)
{
    ui_box(n, r, WS_NAMES[ut_clamp_i(g_ui.ws_view, 0, WSV_COUNT - 1)],
           g_ui.focus == FOC_WORKSPACE);

    /* view tabs on the border row */
    int tx = r->x + r->w - 42;
    if (tx < r->x + 16)
        tx = r->x + 16;
    struct { const char *label; ws_view_t v; int id; } tabs[] = {
        { " FAN ", WSV_FAN, ACT_WS_TAB_FAN },
        { " POWER ", WSV_POWER, ACT_WS_TAB_POWER },
        { " LIGHT ", WSV_LIGHT, ACT_WS_TAB_LIGHT },
        { " ? ", WSV_HELP, ACT_WS_TAB_HELP },
    };
    for (size_t i = 0; i < sizeof(tabs) / sizeof(tabs[0]); i++) {
        ui_btn(n, tx, r->y, tabs[i].label, g_ui.ws_view == tabs[i].v, false,
               TGT(TGT_PANEL_WORKSPACE, tabs[i].id));
        tx += (int)strlen(tabs[i].label);
    }
    /* SET opens the fullscreen settings overlay */
    ui_btn(n, tx + 1, r->y, " SET ", g_ui.settings_overlay, false,
           TGT(TGT_PANEL_WORKSPACE, ACT_WS_TAB_SETTINGS));

    switch (g_ui.ws_view) {
    case WSV_FAN:      editor_fan_draw(n, r); break;
    case WSV_POWER:    draw_power(n, r); break;
    case WSV_LIGHT:    draw_light(n, r); break;
    case WSV_HELP:     draw_help(n, r); break;
    default: break;
    }
}

void panel_workspace_key(uint32_t key)
{
    /* hex typing for the LIGHT view */
    if (g_ui.ws_view == WSV_LIGHT && g_ui.lt_hex_active) {
        if (key == NCKEY_ESC) {
            g_ui.lt_hex_active = 0;
            return;
        }
        if (key == NCKEY_ENTER || key == '\r' || key == '\n') {
            g_ui.lt_hex_active = 0;
            lt_apply_row(LT_HEX);
            return;
        }
        tin_key(&g_ui.lt_hex, key);
        return;
    }

    switch (key) {
    case 'f':
    case 'F': ws_set_view(WSV_FAN); return;
    case 'p':
    case 'P': ws_set_view(WSV_POWER); return;
    case 'l':
    case 'L': ws_set_view(WSV_LIGHT); return;
    case 'e':
    case 'E': settings_open(); return;
    default:
        break;
    }

    switch (g_ui.ws_view) {
    case WSV_FAN:
        editor_fan_key(key);
        return;
    case WSV_POWER: {
        switch (key) {
        case 'j': case NCKEY_DOWN: g_ui.pw_sel = (g_ui.pw_sel + 1) % PW_ROWS; return;
        case 'k': case NCKEY_UP:   g_ui.pw_sel = (g_ui.pw_sel + PW_ROWS - 1) % PW_ROWS; return;
        case 'h': case NCKEY_LEFT: pw_dec_row(g_ui.pw_sel); return;
        case 'l': case NCKEY_RIGHT:
        case NCKEY_ENTER: case '\r': case '\n': case ' ':
            pw_apply_row(g_ui.pw_sel);
            return;
        default: return;
        }
    }
    case WSV_LIGHT: {
        switch (key) {
        case 'j': case NCKEY_DOWN: g_ui.lt_sel = (g_ui.lt_sel + 1) % LT_ROWS; return;
        case 'k': case NCKEY_UP:   g_ui.lt_sel = (g_ui.lt_sel + LT_ROWS - 1) % LT_ROWS; return;
        case 'h': case NCKEY_LEFT:
            switch (g_ui.lt_sel) {
            case LT_KBD:    g_ui.ctl_kbd_idx = (g_ui.ctl_kbd_idx + 3) % 4; break;
            case LT_EFFECT: g_ui.lt_eff = (g_ui.lt_eff + AURA_EFFECT_COUNT - 1) % AURA_EFFECT_COUNT; break;
            case LT_COLOR:  g_ui.lt_col = (g_ui.lt_col + AURA_COLOR_COUNT - 1) % AURA_COLOR_COUNT; break;
            default: break;
            }
            return;
        case 'l': case NCKEY_RIGHT:
            switch (g_ui.lt_sel) {
            case LT_KBD:    g_ui.ctl_kbd_idx = (g_ui.ctl_kbd_idx + 1) % 4; break;
            case LT_EFFECT: g_ui.lt_eff = (g_ui.lt_eff + 1) % AURA_EFFECT_COUNT; break;
            case LT_COLOR:  g_ui.lt_col = (g_ui.lt_col + 1) % AURA_COLOR_COUNT; break;
            default: break;
            }
            return;
        case 'x': case 'X':
            g_ui.lt_hex_active = 1;
            tin_clear(&g_ui.lt_hex);
            return;
        case NCKEY_ENTER: case '\r': case '\n': case ' ':
            lt_apply_row(g_ui.lt_sel);
            return;
        default: return;
        }
    }
    default:
        return;
    }
}

void panel_workspace_act(int id)
{
    switch (id) {
    case ACT_WS_TAB_FAN:      ws_set_view(WSV_FAN); return;
    case ACT_WS_TAB_POWER:    ws_set_view(WSV_POWER); return;
    case ACT_WS_TAB_LIGHT:    ws_set_view(WSV_LIGHT); return;
    case ACT_WS_TAB_SETTINGS: settings_open(); return;
    case ACT_WS_TAB_HELP:     ws_set_view(WSV_HELP); return;
    default:
        break;
    }

    if (id >= ACT_WS_PW_BASE && id < ACT_WS_PW_BASE + PW_ROWS) {
        int row = id - ACT_WS_PW_BASE;
        if (row == g_ui.pw_sel)
            pw_apply_row(row);
        else
            g_ui.pw_sel = row;
        return;
    }
    if (id >= ACT_WS_LT_BASE && id < ACT_WS_LT_BASE + LT_ROWS) {
        int row = id - ACT_WS_LT_BASE;
        if (row == g_ui.lt_sel) {
            if (row == LT_HEX)
                g_ui.lt_hex_active = 1;
            else
                lt_apply_row(row);
        } else {
            g_ui.lt_sel = row;
        }
        return;
    }
}
