#ifndef TUF_SETTINGS_H
#define TUF_SETTINGS_H

#include "hardware.h"

int settings_init(void);
int settings_load(hardware_state_t *hw);
int settings_save(const hardware_state_t *hw);

#endif // TUF_SETTINGS_H
