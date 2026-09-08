#ifndef TUF_SETTINGS_H
#define TUF_SETTINGS_H

#include "hardware.h"

void settings_dir(char *out, size_t maxlen);
int settings_present(void);
int settings_init(void);
int settings_load(hardware_state_t *hw);
int settings_save(const hardware_state_t *hw);
int settings_write_stub(void);

#endif // TUF_SETTINGS_H
