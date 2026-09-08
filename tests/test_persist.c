#include "hardware.h"
#include "settings.h"
#include "profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int fail = 0;

static void expect_eq(const char *name, int a, int b)
{
    if (a != b) {
        fprintf(stderr, "FAIL %s: %d != %d\n", name, a, b);
        fail++;
    }
}

static void fill_curve(fan_curve_t *fc, int n, const int *t, const int *p)
{
    fc->n = n;
    for (int i = 0; i < n; i++) {
        fc->temp_c[i] = t[i];
        fc->pwm[i] = p[i];
    }
}

static void zero_hw(hardware_state_t *hw)
{
    memset(hw, 0, sizeof(*hw));
}

int main(void)
{
    char tmp[] = "/tmp/ctron-persist-XXXXXX";
    if (!mkdtemp(tmp)) {
        perror("mkdtemp");
        return 1;
    }
    setenv("CTRON_CONFIG", tmp, 1);
    setenv("HOME", tmp, 1);

    /* csv round-trip + unique temps */
    {
        fan_curve_t fc = {0};
        expect_eq("from_csv", hw_fan_from_csv(&fc, "70,40,40", "180,80,90"), 3);
        expect_eq("sorted0", fc.temp_c[0], 40);
        expect_eq("unique1", fc.temp_c[1], 41);
        expect_eq("sorted2", fc.temp_c[2], 70);
        char t[64], p[64];
        hw_fan_to_csv(&fc, t, sizeof(t), p, sizeof(p));
        if (strcmp(t, "40,41,70") != 0) {
            fprintf(stderr, "FAIL csv temps %s\n", t);
            fail++;
        }
    }

    hardware_state_t hw;
    zero_hw(&hw);
    int ct[] = {40, 55, 70, 85};
    int cp[] = {80, 120, 180, 255};
    int gt[] = {42, 60, 75};
    int gp[] = {40, 90, 200};
    fill_curve(&hw.fan_cpu, 4, ct, cp);
    fill_curve(&hw.fan_gpu, 3, gt, gp);
    hw.fan_cpu_on = true;
    hw.fan_gpu_on = true;
    hw.active_profile = PROF_BALANCED;
    hw.battery_charge_limit = 80;
    hw.display_cur_hz = 144;

    if (settings_save(&hw) != 0) {
        fprintf(stderr, "FAIL settings_save\n");
        return 1;
    }

    hardware_state_t hw2;
    zero_hw(&hw2);
    /* missing keys would keep zeros; file has keys */
    if (settings_load(&hw2) != 0) {
        fprintf(stderr, "FAIL settings_load\n");
        return 1;
    }
    expect_eq("load cpu n", hw2.fan_cpu.n, 4);
    expect_eq("load cpu t0", hw2.fan_cpu.temp_c[0], 40);
    expect_eq("load cpu p3", hw2.fan_cpu.pwm[3], 255);
    expect_eq("load gpu n", hw2.fan_gpu.n, 3);
    expect_eq("load gpu t2", hw2.fan_gpu.temp_c[2], 75);
    expect_eq("load gpu p2", hw2.fan_gpu.pwm[2], 200);

    /* .ctr rides with Power */
    acv_profile_filter_t filt = {0, 1, 0, 0};
    snprintf(hw.profile_name_input, sizeof(hw.profile_name_input), "TST");
    if (profile_export("TST", &hw, &filt) != 0) {
        fprintf(stderr, "FAIL profile_export\n");
        return 1;
    }
    {
        char ctr[512];
        snprintf(ctr, sizeof(ctr), "%s/profiles/TST.ctr", tmp);
        if (access(ctr, F_OK) != 0) {
            fprintf(stderr, "FAIL missing %s\n", ctr);
            return 1;
        }
    }

    hardware_state_t hw3;
    zero_hw(&hw3);
    hw3.battery_charge_limit = 80;
    hw3.display_cur_hz = 144;
    if (profile_import("TST", &hw3, &filt) != 0) {
        fprintf(stderr, "FAIL profile_import\n");
        return 1;
    }
    expect_eq("acv cpu n", hw3.fan_cpu.n, 4);
    expect_eq("acv cpu t1", hw3.fan_cpu.temp_c[1], 55);
    expect_eq("acv gpu n", hw3.fan_gpu.n, 3);
    expect_eq("acv did not write n=0 empty", hw3.fan_cpu.n > 0, 1);

    /* missing keys: load must not wipe a hwmon-shaped curve */
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/settings.ini", tmp);
        FILE *f = fopen(path, "w");
        fprintf(f, "fan_cpu_on = 1\n");
        fclose(f);
        hardware_state_t keep;
        zero_hw(&keep);
        keep.fan_cpu.n = 8;
        keep.fan_cpu.temp_c[0] = 42;
        keep.fan_cpu.pwm[0] = 0;
        settings_load(&keep);
        expect_eq("missing keys keep n", keep.fan_cpu.n, 8);
        expect_eq("missing keys keep t", keep.fan_cpu.temp_c[0], 42);
    }

    if (fail) {
        fprintf(stderr, "%d failures (config %s)\n", fail, tmp);
        return 1;
    }
    printf("persist tests ok (%s)\n", tmp);
    return 0;
}
