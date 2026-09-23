#ifndef CTRON_CONTROL_H
#define CTRON_CONTROL_H

#include <stdbool.h>

#include "hw.h"

/* Write layer. Every function applies a user-requested change through the
 * privilege chain (asusctl -> direct sysfs -> sudo -n) and updates the
 * in-memory state on success. Nothing here is called from poll loops. */

/* ---- platform -------------------------------------------------------- */

int ctrl_set_profile(hw_state_t *hw, hw_profile_t p);
int ctrl_set_epp(hw_state_t *hw, hw_epp_t e);
int ctrl_set_cpu_max_mhz(hw_state_t *hw, int mhz);
int ctrl_set_cpu_boost(hw_state_t *hw, bool on);

/* ---- power ------------------------------------------------------------ */

/* Watts. Values are clamped to the resolved limits; invariant
 * fppt >= sppt >= spl is enforced. */
void ctrl_ppt_limits(const hw_state_t *hw,
                     int *spl_min, int *spl_max,
                     int *sppt_min, int *sppt_max,
                     int *fppt_min, int *fppt_max);
/* Clamp each value into its window, then enforce fppt >= sppt >= spl —
 * exactly what the write path will do. Shared with the POWER staging
 * so what is staged is what gets written. */
void ctrl_ppt_order(int *spl, int *sppt, int *fppt,
                    int smin, int smax, int pmin, int pmax,
                    int fmin, int fmax);
int ctrl_set_ppt(hw_state_t *hw, int spl, int sppt, int fppt);
/* Remove the watt limits (write the platform maxima, remembering the
 * current values) and put them back afterwards. */
int ctrl_ppt_off(hw_state_t *hw);
int ctrl_ppt_restore(hw_state_t *hw);
int ctrl_set_nv_boost(hw_state_t *hw, int watts);
int ctrl_set_nv_temp(hw_state_t *hw, int celsius);
int ctrl_set_panel_od(hw_state_t *hw, bool on);
int ctrl_set_battery_limit(hw_state_t *hw, int pct);
int ctrl_battery_oneshot(void);

/* ---- display ---------------------------------------------------------- */

int ctrl_set_hz(hw_state_t *hw, int hz);   /* nearest supported mode */

/* ---- keyboard / aura --------------------------------------------------- */

int ctrl_set_kbd(hw_state_t *hw, hw_kbd_t lvl);
int ctrl_set_aura(hw_state_t *hw, int effect_idx, int color_idx);
int ctrl_set_aura_hex(hw_state_t *hw, const char *hex);

extern const char *const AURA_EFFECTS[];
extern const int AURA_EFFECT_COUNT;
extern const char *const AURA_COLOR_NAMES[];
extern const char *const AURA_COLOR_HEX[];
extern const int AURA_COLOR_COUNT;
int ctrl_aura_effect_idx(const char *name);

/* ---- fans --------------------------------------------------------------- */

/* Write in-memory curves to the EC (sysfs) and to asusctl (persistence). */
int ctrl_fan_write(hw_state_t *hw);
int ctrl_fan_set_enabled(hw_state_t *hw, bool cpu_on, bool gpu_on);
/* preset: 0 stock, 1 silent, 2 cool, 3 full (writes immediately) */
int ctrl_fan_preset(hw_state_t *hw, int preset);

#endif /* CTRON_CONTROL_H */
