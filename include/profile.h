#ifndef ACV_PROFILE_H
#define ACV_PROFILE_H

#include "hardware.h"
#include <stdbool.h>

#define MAX_PROFILES 64

typedef struct {
    bool include_perf;
    bool include_power;
    bool include_aura;
    bool include_theme;
} acv_profile_filter_t;

void profile_gen_random_name(char out[4]);
int profile_list(char list[][4], int max_count);
int profile_export(const char *name3, const hardware_state_t *hw, const acv_profile_filter_t *filter);
int profile_import(const char *name3, hardware_state_t *hw, const acv_profile_filter_t *filter);
int profile_delete(const char *name3);
int profile_get_summary(const char *name3, char out_perf[64], char out_pwr[64], char out_aura[64]);

#endif // ACV_PROFILE_H
