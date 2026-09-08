#ifndef TUF_UI_H
#define TUF_UI_H

#include "hardware.h"

/* TUI in the current terminal. Optional; CLI is the program. */
int ui_run(hardware_state_t *hw);

#endif // TUF_UI_H
