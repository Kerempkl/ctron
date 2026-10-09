#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "cmds.h"
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

    /* import roundtrip from a hand-written file (harmless keys only —
     * curves + flags are handled specially, cmd_run is never called):
     * the '=' split leaves the fan side in the KEY, the old parser
     * never matched it and snapshot curves silently never applied */
    {
        char tmp2[] = "/tmp/ctron2-test-XXXXXX";
        CHECK(mkdtemp(tmp2) != NULL, "roundtrip mkdtemp");
        setenv("CTRON_CONFIG", tmp2, 1);
        char pdir[400];
        snprintf(pdir, sizeof(pdir), "%s/profiles", tmp2);
        CHECK(ut_mkdir_p(pdir) == 0, "roundtrip mkdir");
        char pth[460];
        snprintf(pth, sizeof(pth), "%s/rt.ctr", pdir);
        FILE *g = fopen(pth, "w");
        CHECK(g != NULL, "roundtrip open");
        if (g) {
            fputs("# ctron snapshot\n"
                  "[perf]\n"
                  "[power]\n"
                  "fan_cpu = 1\n"
                  "fan_gpu = 1\n"
                  "fan-curve cpu = 40,70 90,200\n"
                  "fan-curve gpu = 45,75 60,220\n"
                  "[light]\n", g);
            fclose(g);
        }
        fan_curve_t keep = hw.fan_cpu;
        fan_default(&hw.fan_cpu);
        fan_default(&hw.fan_gpu);
        hw.fan_cpu_on = hw.fan_gpu_on = false;
        char err2[128];
        CHECK(profile_import("rt", &hw, err2, sizeof(err2)) == 0, "import");
        CHECK(hw.fan_cpu.n == 2 && hw.fan_cpu.temp_c[0] == 40 &&
                  hw.fan_cpu.pwm[1] == 200, "cpu curve restored from file");
        CHECK(hw.fan_gpu.n == 2 && hw.fan_gpu.temp_c[1] == 75 &&
                  hw.fan_gpu.pwm[1] == 220, "gpu curve restored");
        CHECK(hw.fan_cpu_on && hw.fan_gpu_on, "fan flags restored");
        CHECK(hw.fan_staged, "import stages curves for the write");
        hw.fan_cpu = keep;
        unlink(pth);
        rmdir(pdir);
        rmdir(tmp2);
    }
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

static void check_cpu_list_parse(void)
{
    int ids[64];

    int n = hw_cpu_list_parse("0-31", ids, 64);
    CHECK(n == 32, "single range count");
    CHECK(ids[0] == 0 && ids[31] == 31, "single range bounds");

    n = hw_cpu_list_parse("0-15,32-47", ids, 64);
    CHECK(n == 32, "comma list count");
    CHECK(ids[15] == 15 && ids[16] == 32 && ids[31] == 47, "comma list ids");

    n = hw_cpu_list_parse("4", ids, 64);
    CHECK(n == 1 && ids[0] == 4, "single id");

    n = hw_cpu_list_parse("", ids, 64);
    CHECK(n == 0, "empty list");

    n = hw_cpu_list_parse("garbage", ids, 64);
    CHECK(n == 0, "garbage list");
}

static void check_topology_group(void){
    int cc[8], cs[8], cg[8], ccd_n;
    bool odd;
    int n;

    /* SMT pairs across two L3 domains (the FA608PP shape, shrunk) */
    {
        int ids[] = {0, 1, 2, 3};
        const char *sib[] = {"0,2", "1,3", "0,2", "1,3"};
        const char *l3[] = {"0,2", "1,3", "0,2", "1,3"};
        n = hw_topology_group(ids, sib, l3, 4, cc, cs, cg, &ccd_n, &odd);
        CHECK(!odd, "topo smt ok");
        CHECK(n == 2, "topo smt core count");
        CHECK(cc[0] == 0 && cs[0] == 2, "topo core0 pair");
        CHECK(cc[1] == 1 && cs[1] == 3, "topo core1 pair");
        CHECK(cg[0] != cg[1], "topo two ccds");
        CHECK(ccd_n == 2, "topo ccd count");
    }

    /* SMT off: every cpu its own core, single cluster */
    {
        int ids[] = {0, 1, 2};
        const char *sib[] = {"0", "1", "2"};
        const char *l3[] = {"0-2", "0-2", "0-2"};
        n = hw_topology_group(ids, sib, l3, 3, cc, cs, cg, &ccd_n, &odd);
        CHECK(!odd && n == 3, "topo smt-off count");
        CHECK(cs[0] == -1 && cs[2] == -1, "topo smt-off no sib");
        CHECK(ccd_n == 1, "topo smt-off one ccd");
    }

    /* no L3 info at all -> single group */
    {
        int ids[] = {0, 1};
        const char *sib[] = {"0,1", "0,1"};
        const char *l3[] = {NULL, NULL};
        n = hw_topology_group(ids, sib, l3, 2, cc, cs, cg, &ccd_n, &odd);
        CHECK(!odd && n == 1 && ccd_n == 1, "topo no-l3");
    }

    /* offline sibling: the pair lists 16, but 16 is not present */
    {
        int ids[] = {0};
        const char *sib[] = {"0,16"};
        const char *l3[] = {"0"};
        n = hw_topology_group(ids, sib, l3, 1, cc, cs, cg, &ccd_n, &odd);
        CHECK(!odd && n == 1 && cs[0] == -1, "topo offline sib");
    }

    /* more than two present threads per core -> odd fallback */
    {
        int ids[] = {0, 1, 2};
        const char *sib[] = {"0-2", "0-2", "0-2"};
        const char *l3[] = {"0-2", "0-2", "0-2"};
        n = hw_topology_group(ids, sib, l3, 3, cc, cs, cg, &ccd_n, &odd);
        CHECK(odd && n == 0, "topo odd >2 threads");
    }
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

static void check_asusd_enforced(void)
{
    hw_state_t hw = {0};

    /* AC source: what asusd enforces there is what comes back */
    hw.ac_online = true;
    hw.asusd_ac = HW_PERFORMANCE;
    hw.asusd_bat = -1;
    CHECK(hw_asusd_enforced(&hw) == HW_PERFORMANCE, "asusd enforced on AC");

    /* battery source picks the battery branch */
    hw.ac_online = false;
    hw.asusd_bat = HW_QUIET;
    CHECK(hw_asusd_enforced(&hw) == HW_QUIET, "asusd enforced on battery");

    /* takeover off (-1) or unknown (-2) on the current source -> none */
    hw.asusd_bat = -1;
    CHECK(hw_asusd_enforced(&hw) == -1, "asusd off -> none");
    hw.asusd_bat = -2;
    CHECK(hw_asusd_enforced(&hw) == -1, "asusd unknown -> none");

    /* off on the current source but armed on the other -> still none:
     * only the live power source's takeover can revert you */
    hw.asusd_ac = HW_PERFORMANCE;
    CHECK(hw_asusd_enforced(&hw) == -1, "other source armed -> none");

    /* conflict rule: enforced profile differing from the live one */
    hw.ac_online = true;
    hw.profile = HW_QUIET;
    CHECK(hw_asusd_enforced(&hw) == HW_PERFORMANCE, "conflict visible");
}

static void check_asusd_ron_parse(void)
{
    int ac = 9, bat = 9;

    /* both takeovers armed with their profiles */
    hw_asusd_ron_parse(
        "(\n"
        "    change_platform_profile_on_ac: true,\n"
        "    change_platform_profile_on_battery: true,\n"
        "    platform_profile_on_ac: Performance,\n"
        "    platform_profile_on_battery: Quiet,\n"
        ")\n", &ac, &bat);
    CHECK(ac == HW_PERFORMANCE, "ron ac armed");
    CHECK(bat == HW_QUIET, "ron bat armed");

    /* AC off, battery armed — off means -1, not unknown */
    hw_asusd_ron_parse(
        "(\n"
        "    change_platform_profile_on_ac: false,\n"
        "    change_platform_profile_on_battery: true,\n"
        "    platform_profile_on_battery: Balanced,\n"
        ")\n", &ac, &bat);
    CHECK(ac == -1, "ron ac off");
    CHECK(bat == HW_BALANCED, "ron bat armed 2");

    /* no takeover keys at all: both off */
    hw_asusd_ron_parse("(\n    charge_control_end_threshold: 70,\n)\n", &ac, &bat);
    CHECK(ac == -1 && bat == -1, "ron no keys -> off");

    /* armed but the profile name is unreadable: unknown, not off */
    hw_asusd_ron_parse(
        "(\n"
        "    change_platform_profile_on_ac: true,\n"
        "    platform_profile_on_ac: Whatever,\n"
        ")\n", &ac, &bat);
    CHECK(ac == -2, "ron unknown profile");

    /* NULL body: off, no crash */
    hw_asusd_ron_parse(NULL, &ac, &bat);
    CHECK(ac == -1 && bat == -1, "ron null body");
}

static void check_read_file_all(void)
{
    char tmp[] = "/tmp/ctron2-test-XXXXXX";
    CHECK(mkdtemp(tmp) != NULL, "read_all mkdtemp");

    char path[300];
    snprintf(path, sizeof(path), "%s/ron.txt", tmp);
    FILE *f = fopen(path, "w");
    CHECK(f != NULL, "read_all open");
    if (f) {
        fputs("(\n    platform_profile_on_ac: Performance,\n    b: 70,\n)\n", f);
        fclose(f);
    }

    char buf[256];
    CHECK(ut_read_file_all(path, buf, sizeof(buf)) == 0, "read_all ok");
    CHECK(!strcmp(buf, "(\n    platform_profile_on_ac: Performance,\n    b: 70,\n)"),
          "read_all whole body");

    /* single-line file behaves like ut_read_file's trim */
    FILE *g = fopen(path, "w");
    if (g) {
        fputs("hello\n", g);
        fclose(g);
    }
    CHECK(ut_read_file_all(path, buf, sizeof(buf)) == 0, "read_all single");
    CHECK(!strcmp(buf, "hello"), "read_all trimmed");

    /* missing and empty files fail */
    unlink(path);
    CHECK(ut_read_file_all(path, buf, sizeof(buf)) != 0, "read_all missing");
    f = fopen(path, "w");
    if (f)
        fclose(f);
    CHECK(ut_read_file_all(path, buf, sizeof(buf)) != 0, "read_all empty");
    unlink(path);
    rmdir(tmp);
}

static long count_fds(void)
{
    DIR *d = opendir("/proc/self/fd");
    if (!d)
        return -1;
    long n = 0;
    while (readdir(d))
        n++;
    closedir(d);
    return n;
}

static void check_util_write(void)
{
    char tmp[] = "/tmp/ctron2-test-XXXXXX";
    CHECK(mkdtemp(tmp) != NULL, "write mkdtemp");

    char path[300];
    snprintf(path, sizeof(path), "%s/w.txt", tmp);
    CHECK(ut_write_file(path, "42") == 0, "write ok");
    char buf[64];
    CHECK(ut_read_file(path, buf, sizeof(buf)) == 0 && !strcmp(buf, "42"),
          "write roundtrip");
    unlink(path);

    /* fopen succeeds on /dev/full but the write fails — exactly the path
     * where the old code short-circuited past fclose and leaked the fd. */
    long fds0 = count_fds();
    for (int i = 0; i < 50; i++)
        ut_write_file("/dev/full", "42");
    long fds1 = count_fds();
    if (fds0 >= 0 && fds1 >= 0)
        CHECK(fds1 - fds0 <= 4, "failed writes leak no fds");

    /* the sudo fallback shells out; quotes must be rejected BEFORE any
     * exec (both probes use unwritable paths so the fallback is reached) */
    snprintf(path, sizeof(path), "%s/nodir/w.txt", tmp);
    CHECK(ut_priv_write(path, "4'2") != 0, "priv rejects quote in value");
    CHECK(ut_priv_write("/no'such/dir/w", "42") != 0,
          "priv rejects quote in path");

    int saved_pref = g_prefs.write_pref;
    char locked[300];
    FILE *f;
    snprintf(locked, sizeof(locked), "%s/locked.txt", tmp);
    f = fopen(locked, "w");
    CHECK(f != NULL, "nosudo file");
    if (f) {
        fputs("old\n", f);
        fclose(f);
    }
    chmod(locked, 0444);
    g_prefs.write_pref = PREF_NO_SUDO;
    CHECK(ut_priv_write(locked, "new") != 0, "no sudo skips tee");
    CHECK(ut_read_file(locked, buf, sizeof(buf)) == 0 && !strcmp(buf, "old"),
          "no sudo left the file alone");
    g_prefs.write_pref = saved_pref;
    chmod(locked, 0644);
    unlink(locked);

    rmdir(tmp);
}

static void check_mode_split(void)
{
    char got[8][MODE_STEPS_MAX];
    int n = mode_split_steps(
        "profile performance, ppt 45,55,55, fan-curve cpu 40,50 80,90, hz 144",
        got, 8);
    CHECK(n == 4, "split count");
    CHECK(!strcmp(got[0], "profile performance"), "split profile");
    CHECK(!strcmp(got[1], "ppt 45,55,55"), "split ppt commas");
    CHECK(!strcmp(got[2], "fan-curve cpu 40,50 80,90"), "split fan commas");
    CHECK(!strcmp(got[3], "hz 144"), "split hz");
    CHECK(cmd_is_key("fan-curve") == 1, "fan-curve is a key");
    CHECK(cmd_is_key("55") == 0, "a number is not a key");
}

static void check_fan_enable(void)
{
    CHECK(fan_enable_raw(1) == 1, "curve on writes 1");
    CHECK(fan_enable_raw(0) == 2, "curve off writes 2");
    CHECK(fan_raw_is_on(1) == 1, "read 1 is on");
    CHECK(fan_raw_is_on(2) == 0, "read 2 is automatic");
    CHECK(fan_raw_is_on(0) == 0, "read 0 is not on");
    CHECK(fan_raw_is_on(3) == 0, "read 3 is not on");
}

static void check_gpu_temp_gate(void)
{
    CHECK(hw_gpu_temp_allowed(0, "active") == 0, "gpu pref off");
    CHECK(hw_gpu_temp_allowed(1, "suspended") == 0, "gpu suspended");
    CHECK(hw_gpu_temp_allowed(1, "suspending") == 0, "gpu suspending");
    CHECK(hw_gpu_temp_allowed(1, "active") == 1, "gpu active");
    CHECK(hw_gpu_temp_allowed(1, NULL) == 1, "gpu without a runtime node");
}

static void check_cell_join(void)
{
    char marked[64], plain[64];
    char *mv, *pv;
    ut_cell_join(marked, sizeof(marked), "▸ hi ", 8, "V");
    ut_cell_join(plain, sizeof(plain), "  hi ", 8, "V");
    mv = strchr(marked, 'V');
    pv = strchr(plain, 'V');
    CHECK(mv != NULL && pv != NULL, "value marker present");
    if (mv)
        *mv = '\0';
    if (pv)
        *pv = '\0';
    CHECK(ut_cells(marked) == 8 && ut_cells(plain) == 8,
          "selected marker does not shift the value");
}

static void check_mode_touch_mask(void)
{
    unsigned m = mode_touch_mask(
        "profile quiet, fan cool, hz 60, epp power, ppt Q45, battery 80, "
        "kbd low, cpu-boost off, panel-od on, nv-boost 15, nv-temp 80, "
        "freq 3000, gpu-clock 1400, fan-curve cpu 40,50 80,90, "
        "fan-curve gpu 40,50 80,90, "
        "aura static red, ac-profile off, battery-profile quiet, fan-write");
    CHECK(m == (MS_PROFILE | MS_FAN_CPU | MS_FAN_GPU | MS_HZ | MS_EPP |
                MS_PPT | MS_BAT | MS_KBD | MS_BOOST | MS_OD | MS_NVB |
                MS_NVT | MS_FREQ | MS_GPUCLOCK),
          "touch mask full");

    CHECK(mode_touch_mask("fan-curve gpu 40,50 80,90") == MS_FAN_GPU,
          "touch mask fan-curve gpu");
    CHECK(mode_touch_mask("profile quiet") == MS_PROFILE, "touch mask single");
    CHECK(mode_touch_mask("aura static red") == 0, "aura untracked");
    CHECK(mode_touch_mask("ac-profile off, battery-profile quiet") == 0,
          "asusd rows untracked");
    CHECK(mode_touch_mask("fan-write, battery-oneshot") == 0, "flush untracked");
    CHECK(mode_touch_mask("") == 0, "empty steps");
    CHECK(mode_touch_mask(NULL) == 0, "null steps");
}

static void check_mode_drift(void)
{
    hw_state_t hw;
    memset(&hw, 0, sizeof(hw));
    hw.profile = HW_QUIET;
    hw.epp = HW_EPP_POWER;
    hw.ppt_spl = 45;
    hw.ppt_sppt = 55;
    hw.ppt_fppt = 55;
    hw.hz_cur = 60;
    hw.bat_limit = 80;
    hw.kbd = HW_KBD_LOW;
    hw.cpu_boost = false;
    hw.panel_od = false;
    hw.nv_boost = 15;
    hw.nv_temp = 80;
    hw.cpu_mhz_limit = 3000;
    fan_default(&hw.fan_cpu);
    fan_default(&hw.fan_gpu);

    unsigned mask = mode_touch_mask(
        "profile quiet, ppt Q45, hz 60, epp power, battery 80, kbd low, "
        "cpu-boost off, panel-od on, nv-boost 15, nv-temp 80, freq 3000, "
        "gpu-clock 1400, fan cool");
    mode_snap_t snap;
    unsigned kept = mode_snapshot(&hw, mask, &snap);
    CHECK(kept == mask, "snapshot keeps every readable field");
    CHECK(mode_drift_count(&hw, kept, &snap) == 0, "no drift on unchanged");

    hw.profile = HW_BALANCED;
    CHECK(mode_drift_count(&hw, kept, &snap) == 1, "profile drift counts");
    hw.profile = HW_QUIET;

    /* the gpu clock lock is session-tracked: any change drifts */
    hw.gpu_clock_lock = 1400;
    CHECK(mode_drift_count(&hw, kept, &snap) == 1, "gpu lock drift counts");
    hw.gpu_clock_lock = 0;
    CHECK(mode_drift_count(&hw, kept, &snap) == 0, "gpu restore clears");

    /* a stale live read (0) is never drift */
    hw.ppt_spl = 0;
    CHECK(mode_drift_count(&hw, kept, &snap) == 0, "stale ppt not drift");
    hw.ppt_spl = 45;

    /* a field unreadable at apply time is dropped from the mask */
    hw.hz_cur = 0;
    unsigned kept2 = mode_snapshot(&hw, mask, &snap);
    CHECK(!(kept2 & MS_HZ), "hz dropped while unknown");
    hw.hz_cur = 165;
    CHECK(mode_drift_count(&hw, kept2, &snap) == 0, "dropped field no drift");
    hw.hz_cur = 60;

    /* the mask restricts the comparison: battery is untracked here */
    unsigned narrow = mode_snapshot(&hw, MS_PROFILE, &snap);
    CHECK(narrow == MS_PROFILE, "narrow snapshot");
    hw.bat_limit = 100;
    CHECK(mode_drift_count(&hw, narrow, &snap) == 0, "untracked field ignored");
    hw.bat_limit = 80;

    /* fan curve change counts, exact restore clears it again */
    unsigned full = mode_snapshot(&hw, mask, &snap);
    CHECK(full == mask, "snapshot full again");
    CHECK(mode_drift_count(&hw, full, &snap) == 0, "clean again");
    hw.fan_cpu.pwm[3] += 10;
    CHECK(mode_drift_count(&hw, full, &snap) == 1, "fan drift counts");
    hw.fan_cpu.pwm[3] -= 10;
    CHECK(mode_drift_count(&hw, full, &snap) == 0, "restore clears drift");
}

static void check_gpu_clock(void)
{
    /* probe 2026-10-04: -lgc 1400 -> current reads 1395 (driver grid) */
    CHECK(ctrl_gpu_clock_ok(1400, "180,1395,1395") == 1, "samples under lock");
    CHECK(ctrl_gpu_clock_ok(1400, "1395") == 1, "single sample");
    CHECK(ctrl_gpu_clock_ok(1400, "1500") == 0, "sample above lock");
    CHECK(ctrl_gpu_clock_ok(1400, "1402") == 1, "grid-rounding slack");
    CHECK(ctrl_gpu_clock_ok(1400, "1403") == 0, "slack boundary");
    CHECK(ctrl_gpu_clock_ok(1400, "") == 0, "no samples");
    CHECK(ctrl_gpu_clock_ok(1400, "garbage") == 0, "garbage samples");
}

static void check_ppt_ryzen_cmd(void)
{
    char cmd[160];

    /* name-based mapping: SPL->STAPM(-a), SPPT->SLOW(-c), FPPT->FAST(-b),
     * W in -> mW out, fixed flag order */
    ctrl_ppt_ryzen_cmd("/usr/bin/ryzenadj", 30, 50, 40, cmd, sizeof(cmd));
    CHECK(!strcmp(cmd, "sudo -n /usr/bin/ryzenadj -a 30000 -c 50000 -b 40000"),
          "ryzen cmd mapping + mW");

    ctrl_ppt_ryzen_cmd(NULL, 45, 55, 55, cmd, sizeof(cmd));
    CHECK(!strcmp(cmd, "sudo -n ryzenadj -a 45000 -c 55000 -b 55000"),
          "ryzen cmd bare fallback");

    /* only integers ever enter the string — no injection surface */
    ctrl_ppt_ryzen_cmd("/x/r", 0, 0, 0, cmd, sizeof(cmd));
    CHECK(!strcmp(cmd, "sudo -n /x/r -a 0 -c 0 -b 0"), "ryzen cmd zero");
}

static void check_snapshots(void)
{
    char tmp[] = "/tmp/ctron2-test-XXXXXX";
    CHECK(mkdtemp(tmp) != NULL, "snap mkdtemp");
    setenv("CTRON_CONFIG", tmp, 1);

    char safe[PROFILE_NAME_MAX];
    profile_sanitize_name("oyun/turbo 2", safe, sizeof(safe));
    CHECK(!strcmp(safe, "oyun/turbo-2"), "sanitize group slash");
    profile_sanitize_name("a/../b", safe, sizeof(safe));
    CHECK(!strcmp(safe, "a/--/b"), "sanitize dotdot rejected");
    profile_sanitize_name("//x//y//", safe, sizeof(safe));
    CHECK(!strcmp(safe, "x/y"), "sanitize collapse+trim");
    profile_sanitize_name("..", safe, sizeof(safe));
    CHECK(!strcmp(safe, "p"), "sanitize only dotdot");

    hw_state_t hw;
    memset(&hw, 0, sizeof(hw));
    snprintf(hw.model, sizeof(hw.model), "test");
    hw.profile = HW_PERFORMANCE;
    hw.epp = HW_EPP_PERF;
    hw.bat_limit = 80;
    hw.hz_cur = 165;
    hw.cpu_mhz_limit = 5386;
    hw.kbd = HW_KBD_MED;
    hw.fan_cpu_on = hw.fan_gpu_on = true;
    fan_from_csv(&hw.fan_cpu, "40,70", "90,200");
    fan_default(&hw.fan_gpu);

    CHECK(profile_export("grup/t1", &hw) == 0, "export with group");

    char list[MAX_PROFILES][PROFILE_NAME_MAX];
    int n = profile_list(list, MAX_PROFILES);
    bool seen = false;
    for (int i = 0; i < n; i++)
        if (!strcmp(list[i], "grup/t1"))
            seen = true;
    CHECK(seen, "grouped export listed");

    profile_meta_t m;
    CHECK(profile_meta_parse("grup/t1", &m) == 0, "meta parse");
    CHECK(!strcmp(m.profile, "Performance"), "meta profile");
    CHECK(m.hz == 165 && m.bat == 80, "meta hz/bat");
    CHECK(m.fan_cpu_on == 1 && m.fan_gpu_on == 1, "meta fan flags");
    CHECK(m.saved[0] != '\0', "meta saved header");

    char diff[96];
    profile_meta_diff(&hw, &m, diff, sizeof(diff));
    CHECK(!strcmp(diff, "\xe2\x89\xa1 live"), "diff none when equal");
    hw.profile = HW_QUIET;
    profile_meta_diff(&hw, &m, diff, sizeof(diff));
    CHECK(strstr(diff, "profile") != NULL, "diff profile change");
    hw.profile = HW_PERFORMANCE;
    hw.fan_cpu.pwm[0] += 1;
    profile_meta_diff(&hw, &m, diff, sizeof(diff));
    CHECK(strstr(diff, "curve-cpu") != NULL, "diff curve change");
    hw.fan_cpu.pwm[0] -= 1;

    CHECK(profile_set_note("grup/t1", "oyun ayarlari") == 0, "set note");
    CHECK(profile_meta_parse("grup/t1", &m) == 0 &&
              !strcmp(m.note, "oyun ayarlari"), "note roundtrip");
    char sum[128];
    CHECK(profile_summary("grup/t1", sum, sizeof(sum)) == 0 &&
              strstr(sum, "oyun ayarlari") && strstr(sum, "Performance"),
          "summary carries note + fields");

    CHECK(profile_rename("grup/t1", "grup/t2") == 0, "rename");
    n = profile_list(list, MAX_PROFILES);
    bool old_gone = true, new_seen = false;
    for (int i = 0; i < n; i++) {
        if (!strcmp(list[i], "grup/t1"))
            old_gone = false;
        if (!strcmp(list[i], "grup/t2"))
            new_seen = true;
    }
    CHECK(old_gone && new_seen, "rename reflected in list");
    CHECK(profile_rename("grup/t2", "grup/t2") != 0, "rename onto existing fails");

    CHECK(profile_delete("grup/t2") == 0, "delete");
    char pdir[512];
    snprintf(pdir, sizeof(pdir), "%s/profiles/grup", tmp);
    rmdir(pdir);
    snprintf(pdir, sizeof(pdir), "%s/profiles", tmp);
    rmdir(pdir);
    rmdir(tmp);
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
    check_cpu_list_parse();
    check_topology_group();
    check_asusd_enforced();
    check_asusd_ron_parse();
    check_read_file_all();
    check_util_write();
    check_mode_split();
    check_fan_enable();
    check_gpu_temp_gate();
    check_cell_join();
    check_mode_touch_mask();
    check_mode_drift();
    check_ppt_order();
    check_ppt_ryzen_cmd();
    check_gpu_clock();
    check_snapshots();

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("core tests ok\n");
    return 0;
}
