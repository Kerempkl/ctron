#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "control.h"
#include "fan.h"
#include "hw.h"
#include "modes.h"
#include "profile.h"
#include "settings.h"
#include "util.h"
#include "display/display.h"

static int failures = 0;

#define CHECK(cond, name)                                                   \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL %s (line %d)\n", name, __LINE__);         \
            failures++;                                                     \
        }                                                                   \
    } while (0)

static void check_fan_csv(void)
{
    fan_curve_t fc = {0};
    CHECK(fan_from_csv(&fc, "70,40,40", "180,80,90") == 3, "csv count");
    CHECK(fc.temp_c[0] == 40, "csv sorted[0]");
    CHECK(fc.temp_c[1] == 41, "csv unique[1]");
    CHECK(fc.temp_c[2] == 70, "csv sorted[2]");

    char t[64], p[64];
    fan_to_csv(&fc, t, sizeof(t), p, sizeof(p));
    CHECK(!strcmp(t, "40,41,70"), "csv roundtrip temps");
    CHECK(!strcmp(p, "80,90,180"), "csv roundtrip pwms");
}

static void check_fan_edit(void)
{
    fan_curve_t fc = {0};
    fan_default(&fc);
    CHECK(fc.n == FAN_POINTS, "default n");

    CHECK(fan_add_point(&fc, -1, -1) == -1, "add rejected on full curve");
    CHECK(fc.n == FAN_POINTS, "full curve stays at max");

    fan_del_point(&fc, fc.n - 1);
    int idx = fan_add_point(&fc, -1, -1);
    CHECK(idx >= 0, "add after delete");
    CHECK(fc.temp_c[idx] > 20 && fc.temp_c[idx] < 105, "auto temp in range");

    idx = fan_set_point(&fc, 0, 95, 250);
    CHECK(fc.temp_c[idx] == 95 || fc.n == 1, "set abs");
    for (int i = 1; i < fc.n; i++)
        CHECK(fc.temp_c[i] > fc.temp_c[i - 1], "strictly increasing");
}

static void check_settings_roundtrip(void)
{
    char tmp[] = "/tmp/ctron2-test-XXXXXX";
    CHECK(mkdtemp(tmp) != NULL, "mkdtemp");
    setenv("CTRON_CONFIG", tmp, 1);

    hw_state_t hw;
    memset(&hw, 0, sizeof(hw));
    fan_from_csv(&hw.fan_cpu, "40,60,80", "80,140,220");
    fan_from_csv(&hw.fan_gpu, "45,75", "60,200");
    hw.fan_cpu_on = true;
    hw.fan_gpu_on = false;
    hw.cpu_mhz_limit = 4500;
    CHECK(settings_save(&hw) == 0, "settings save");

    hw_state_t loaded;
    memset(&loaded, 0, sizeof(loaded));
    fan_default(&loaded.fan_cpu);
    CHECK(settings_load(&loaded) == 0, "settings load");
    CHECK(loaded.fan_cpu.n == 3, "fan cpu n");
    CHECK(loaded.fan_cpu.temp_c[0] == 40, "fan cpu t0");
    CHECK(loaded.fan_cpu.pwm[2] == 220, "fan cpu p2");
    CHECK(loaded.fan_gpu.n == 2, "fan gpu n");
    CHECK(loaded.fan_gpu.pwm[1] == 200, "fan gpu p1");
    CHECK(loaded.fan_cpu_on, "fan cpu flag");
    CHECK(!loaded.fan_gpu_on, "fan gpu flag");
    CHECK(loaded.cpu_mhz_limit == 4500, "cpu limit");
}

static void check_modes(void)
{
    mode_def_t modes[MODES_MAX];
    int n = modes_load(modes, MODES_MAX);
    CHECK(n >= 5, "default modes seeded");
    int t = mode_find(modes, n, "turbo");
    CHECK(t >= 0, "turbo exists");
    CHECK(strstr(modes[t].steps, "profile performance") != NULL, "turbo steps");

    /* persist roundtrip */
    snprintf(modes[0].name, MODE_NAME_MAX, "zz-test");
    snprintf(modes[0].steps, MODE_STEPS_MAX, "profile quiet, fan stock");
    CHECK(modes_save(modes, n) == 0, "modes save");
    mode_def_t again[MODES_MAX];
    int n2 = modes_load(again, MODES_MAX);
    CHECK(n2 == n, "modes count stable");
    int z = mode_find(again, n2, "zz-test");
    CHECK(z >= 0, "edited mode persists");
    CHECK(!strcmp(again[z].steps, "profile quiet, fan stock"), "edited steps persist");
}

static void check_profiles(void)
{
    hw_state_t hw;
    memset(&hw, 0, sizeof(hw));
    snprintf(hw.model, sizeof(hw.model), "test");
    hw.profile = HW_PERFORMANCE;
    hw.bat_limit = 80;
    fan_from_csv(&hw.fan_cpu, "40,70", "90,200");

    CHECK(profile_export("test-round", &hw) == 0, "export");

    char list[MAX_PROFILES][PROFILE_NAME_MAX];
    int n = profile_list(list, MAX_PROFILES);
    bool seen = false;
    for (int i = 0; i < n; i++)
        if (!strcmp(list[i], "test-round"))
            seen = true;
    CHECK(seen, "exported file listed");

    char sum[128];
    CHECK(profile_summary("test-round", sum, sizeof(sum)) == 0, "summary");
    CHECK(strstr(sum, "Performance") != NULL, "summary has profile");

    CHECK(profile_delete("test-round") == 0, "delete");
    CHECK(profile_delete("test-round") != 0, "double delete fails");
}

/* kscreen-doctor output parser, offline. */
extern int disp_kde_test_parse(const char *text, char *name, size_t nn,
                               int *hz_cur, int *hz_out, int hz_max);

static void check_kde_parser(void)
{
    /* shaped like real kscreen-doctor -o output (ANSI already stripped) */
    const char *sample =
        "Output: 1 eDP-2 38c0a9e2-cc2a-4185\n"
        "\tenabled\n"
        "\tconnected\n"
        "Modes:  1:2560x1600@165.00*!  2:2560x1600@60.00  3:1920x1200@59.88  4:640x480@60.00 \n"
        "Geometry: 0,0 1707x1067\n";

    char name[64] = {0};
    int hz[16];
    int rc = disp_kde_test_parse(sample, name, sizeof(name), NULL, hz, 16);
    /* 165.00*, 60.00, 59.88, 60.00 -> 59.88 rounds to 60 and dedups */
    CHECK(rc == 2, "kde modes dedup+round");
    CHECK(!strcmp(name, "eDP-2"), "kde name");
    CHECK(hz[0] == 60, "kde sorted[0]");
    CHECK(hz[rc - 1] == 165, "kde sorted[last]");

    int cur = -1;
    disp_kde_test_parse(sample, name, sizeof(name), &cur, hz, 16);
    CHECK(cur == 165, "kde current hz (marked with *)");
}

static void check_power_fmt(void)
{
    char b[32];
    hw_fmt_power(b, sizeof(b), false, 0);
    CHECK(!strcmp(b, "--"), "power unknown");
    hw_fmt_power(b, sizeof(b), true, 31558);
    CHECK(!strcmp(b, "dis 31.6W"), "discharge watts");
    hw_fmt_power(b, sizeof(b), true, -12040);
    CHECK(!strcmp(b, "chg 12.0W"), "charge watts");
    hw_fmt_power(b, sizeof(b), true, 40);
    CHECK(!strcmp(b, "0.0W"), "idle watts");
}

static void check_ppt_order(void)
{
    /* staging SPL above the others pulls the chain up, like the write */
    int spl = 80, sppt = 60, fppt = 60;
    ctrl_ppt_order(&spl, &sppt, &fppt, 15, 90, 35, 120, 35, 120);
    CHECK(spl == 80 && sppt == 80 && fppt == 80, "order pulls up");

    /* out-of-window values clamp to the firmware limits */
    spl = 5; sppt = 60; fppt = 200;
    ctrl_ppt_order(&spl, &sppt, &fppt, 15, 90, 35, 120, 35, 120);
    CHECK(spl == 15 && sppt == 60 && fppt == 120, "clamp to limits");

    /* clamping happens before ordering: the chain stays sorted */
    spl = 45; sppt = 30; fppt = 20;
    ctrl_ppt_order(&spl, &sppt, &fppt, 15, 90, 35, 120, 35, 120);
    CHECK(spl == 45 && sppt == 45 && fppt == 45, "clamp then order");

    /* an ordered triple inside the windows passes untouched */
    spl = 45; sppt = 55; fppt = 55;
    ctrl_ppt_order(&spl, &sppt, &fppt, 15, 90, 35, 120, 35, 120);
    CHECK(spl == 45 && sppt == 55 && fppt == 55, "ordered stays");
}

int main(void)
{
    check_fan_csv();
    check_fan_edit();
    check_settings_roundtrip();
    check_modes();
    check_profiles();
    check_kde_parser();
    check_power_fmt();
    check_ppt_order();

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("core tests ok\n");
    return 0;
}
