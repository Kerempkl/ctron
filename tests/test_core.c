#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "cmds.h"
#include "control.h"
#include "fan.h"
#include "hw.h"
#include "modes.h"
#include "profile.h"
#include "settings.h"
#include "telem.h"
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

    /* oversized .ctr must not smash the stack through set_note's
     * offset arithmetic (ASan-proven 2026-10-28); the rewrite comes
     * back truncated but parseable, and the note round-trips */
    {
        char tmp3[] = "/tmp/ctron2-test-XXXXXX";
        CHECK(mkdtemp(tmp3) != NULL, "note mkdtemp");
        setenv("CTRON_CONFIG", tmp3, 1);
        char pdir[400];
        snprintf(pdir, sizeof(pdir), "%s/profiles", tmp3);
        CHECK(ut_mkdir_p(pdir) == 0, "note mkdir");
        char pth[460];
        snprintf(pth, sizeof(pth), "%s/notebig.ctr", pdir);
        FILE *g = fopen(pth, "w");
        CHECK(g != NULL, "note open");
        if (g) {
            fputs("# ctron snapshot\n[perf]\n", g);
            for (int i = 0; i < 24; i++)
                fprintf(g, "# pad %04d %0500d\n", i, i); /* ~12 KB */
            fputs("profile = performance\n", g);
            fclose(g);
        }
        CHECK(profile_set_note("notebig", "regression") == 0,
              "set_note on oversized profile");
        profile_meta_t m;
        CHECK(profile_meta_parse("notebig", &m) == 0,
              "oversized profile still parseable");
        CHECK(!strcmp(m.note, "regression"), "note round-trips");
        unlink(pth);
        rmdir(pdir);
        rmdir(tmp3);
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

    /* multi-monitor listing: an external HDMI output comes FIRST — the
     * panel (eDP*) must be chosen and only ITS modes parsed */
    const char *multi =
        "Output: 1 HDMI-A-1 1111-2222\n"
        "\tenabled\n"
        "\tconnected\n"
        "Modes:  1:1920x1080@60.00*  2:1920x1080@120.00 \n"
        "Geometry: 1920,0 1920x1080\n"
        "Output: 2 eDP-1 3333-4444\n"
        "\tenabled\n"
        "\tconnected\n"
        "Modes:  1:2560x1600@165.00*!  2:2560x1600@60.00 \n"
        "Geometry: 0,0 2560x1600\n";
    rc = disp_kde_test_parse(multi, name, sizeof(name), &cur, hz, 16);
    CHECK(!strcmp(name, "eDP-1"), "kde multi: panel picked over first HDMI");
    CHECK(rc == 2 && hz[0] == 60 && hz[1] == 165, "kde multi: panel modes only");
    CHECK(cur == 165, "kde multi: current from panel block");
}

static void check_hypr_parser(void)
{
    extern int disp_hypr_test_parse(const char *json, int *hz_out, int hz_max);

    /* two monitors, external listed FIRST and NOT focused: the focused
     * panel's availableModes must win */
    const char *two =
        "[{ \"name\":\"HDMI-A-1\", \"width\":1920, \"height\":1080, "
        "\"refreshRate\":60.00, \"x\":1920, \"y\":0, \"scale\":1.00, "
        "\"focused\":false, "
        "\"availableModes\":[\"1920x1080@60.00Hz\",\"1920x1080@120.00Hz\"] }, "
        "{ \"name\":\"eDP-1\", \"width\":1920, \"height\":1080, "
        "\"refreshRate\":144.06, \"x\":0, \"y\":0, \"scale\":1.00, "
        "\"focused\":true, "
        "\"availableModes\":[\"1920x1080@60.06Hz\",\"1920x1080@144.06Hz\"] }]";
    int hz[16];
    int rc = disp_hypr_test_parse(two, hz, 16);
    CHECK(rc == 2 && hz[0] == 60 && hz[1] == 144,
          "hypr multi: focused monitor's modes only");

    /* no focused flag anywhere -> first monitor (HDMI) is the fallback */
    const char *nofocus =
        "[{ \"name\":\"HDMI-A-1\", \"width\":1920, \"height\":1080, "
        "\"refreshRate\":60.00, \"availableModes\":[\"1920x1080@60.00Hz\"] }, "
        "{ \"name\":\"eDP-1\", \"width\":1920, \"height\":1080, "
        "\"refreshRate\":144.00, \"availableModes\":[\"1920x1080@144.00Hz\"] }]";
    rc = disp_hypr_test_parse(nofocus, hz, 16);
    CHECK(rc == 1 && hz[0] == 60, "hypr no-focus: first monitor fallback");

    rc = disp_hypr_test_parse("[]", hz, 16);
    CHECK(rc == -1, "hypr empty array honest failure");
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

    /* the sudo fallback is a pure argv exec: quotes are no longer
     * rejected pre-exec; unwritable paths must fail cleanly through
     * the real child (no sudo rule for the test runner) */
    snprintf(path, sizeof(path), "%s/nodir/w.txt", tmp);
    CHECK(ut_priv_write(path, "4'2") != 0, "unwritable path fails cleanly");
    CHECK(ut_priv_write("/no'such/dir/w", "42") != 0,
          "quoted path survives the exec path");

    /* argv-fed child plumbing without sudo: a real tee writes verbatim */
    snprintf(path, sizeof(path), "%s/tee.txt", tmp);
    const char *av[] = { "tee", path, NULL };
    CHECK(ut_write_via_argv(av, "V'Q\"") == 0, "argv tee writes");
    CHECK(ut_read_file(path, buf, sizeof(buf)) == 0 &&
              !strcmp(buf, "V'Q\""), "tee roundtrip quotes verbatim");
    unlink(path);

    /* a child that exits before reading must not SIGPIPE the runner */
    const char *av_early[] = { "true", NULL };
    ut_write_via_argv(av_early, "x");
    CHECK(1, "early-exit child does not kill the writer");

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

static void check_argv_exec(void)
{
    char buf[64];
    const char *av1[] = { "echo", "hello", "world", NULL };
    CHECK(ut_exec_argv(av1, buf, sizeof(buf)) == 0, "argv echo rc");
    CHECK(!strcmp(buf, "hello world"), "argv echo first line");

    const char *av2[] = { "false", NULL };
    CHECK(ut_exec_argv(av2, buf, sizeof(buf)) != 0, "argv false rc");

    const char *av3[] = { "/no/such/binary", NULL };
    CHECK(ut_exec_argv(av3, buf, sizeof(buf)) == 127, "argv missing 127");

    /* raw mode keeps everything but strips ANSI escapes */
    const char *av4[] = { "printf", "\033[31mhi\033[0m\n", NULL };
    CHECK(ut_exec_argv_raw(av4, buf, sizeof(buf)) == 0, "argv raw rc");
    CHECK(!strcmp(buf, "hi\n"), "argv raw strips ansi");

    /* both mode merges stderr into the capture */
    const char *av5[] = { "sh", "-c", "echo out; echo err >&2", NULL };
    CHECK(ut_exec_argv_both(av5, buf, sizeof(buf)) == 0, "argv both rc");
    CHECK(strstr(buf, "out") && strstr(buf, "err"), "argv both merges stderr");

    /* a child writing far more than the buffer must not die on SIGPIPE
     * (a truncated read would fake rc=141 = failed write) */
    const char *av6[] = { "seq", "1", "100000", NULL };
    char small[32];
    CHECK(ut_exec_argv_raw(av6, small, sizeof(small)) == 0, "argv drain rc");
    CHECK(strlen(small) == sizeof(small) - 1, "argv drain keeps first bytes");
}

static void check_telem(void)
{
    telem_reset();
    CHECK(telem_tick(0) == 1, "telem first tick");
    CHECK(telem_tick(100) == 0, "telem gate holds");
    CHECK(telem_tick(250) == 1, "telem gate opens at 250ms");

    telem_reset();
    CHECK(telem_last(TS_CPU_TEMP) < 0, "telem empty last");
    char sp[64];
    telem_spark(TS_CPU_TEMP, sp, sizeof sp, 8);
    CHECK(!strcmp(sp, "--"), "telem empty spark");

    /* fixed range 0..100: 0 -> lowest block, 50 -> mid, 100 -> full */
    telem_push(TS_CPU_TEMP, 0);
    telem_push(TS_CPU_TEMP, 50);
    telem_push(TS_CPU_TEMP, 100);
    CHECK(telem_last(TS_CPU_TEMP) == 100, "telem last");
    CHECK(telem_count(TS_CPU_TEMP) == 3, "telem count");
    telem_spark(TS_CPU_TEMP, sp, sizeof sp, 3);
    CHECK(!strcmp(sp, "\xE2\x96\x81\xE2\x96\x84\xE2\x96\x88"),
          "telem spark 0/50/100 fixed scale");

    telem_push(TS_CPU_TEMP, 150); /* above the fixed range: clipped */
    telem_spark(TS_CPU_TEMP, sp, sizeof sp, 4);
    CHECK(!strncmp(sp + strlen(sp) - 3, "\xE2\x96\x88", 3),
          "telem spark clips above range");

    telem_push(TS_CPU_TEMP, -1); /* unknown: skipped, ring unchanged */
    CHECK(telem_count(TS_CPU_TEMP) == 4, "telem -1 skipped");

    /* flat AUTO window draws the mid block everywhere */
    telem_reset();
    for (int i = 0; i < 5; i++)
        telem_push(TS_CPU_MHZ, 4200);
    telem_spark(TS_CPU_MHZ, sp, sizeof sp, 5);
    CHECK(!strcmp(sp,
                  "\xE2\x96\x84\xE2\x96\x84\xE2\x96\x84\xE2\x96\x84\xE2\x96\x84"),
          "telem flat auto mid");

    /* wrap: oldest samples drop, window min/max follow */
    telem_reset();
    for (int i = 0; i < TELEM_N + 10; i++)
        telem_push(TS_FAN_CPU, i);
    CHECK(telem_count(TS_FAN_CPU) == TELEM_N, "telem ring caps");
    CHECK(telem_last(TS_FAN_CPU) == TELEM_N + 9, "telem newest after wrap");
    double mn, mx;
    telem_minmax(TS_FAN_CPU, &mn, &mx);
    CHECK(mn == 10 && mx == TELEM_N + 9, "telem window minmax");
    char big[TELEM_N * 3 + 2];
    telem_spark(TS_FAN_CPU, big, sizeof big, TELEM_N);
    CHECK(!strncmp(big, "\xE2\x96\x81", 3) &&
              !strncmp(big + strlen(big) - 3, "\xE2\x96\x88", 3),
          "telem auto endpoints oldest-low newest-high");

    /* telem_at: back-indexed access used by the detail charts */
    telem_reset();
    double av;
    CHECK(!telem_at(TS_CPU_TEMP, 0, &av) && av < 0, "telem_at empty false");
    telem_push(TS_CPU_TEMP, 10);
    telem_push(TS_CPU_TEMP, 20);
    telem_push(TS_CPU_TEMP, -1); /* unknown tick: never stored */
    telem_push(TS_CPU_TEMP, 30);
    CHECK(telem_at(TS_CPU_TEMP, 0, &av) && av == 30, "telem_at newest");
    CHECK(telem_at(TS_CPU_TEMP, 1, &av) && av == 20, "telem_at back 1");
    CHECK(telem_at(TS_CPU_TEMP, 2, &av) && av == 10, "telem_at back 2");
    CHECK(!telem_at(TS_CPU_TEMP, 3, &av), "telem_at beyond window false");
    CHECK(!telem_at(-1, 0, &av) && !telem_at(TS_COUNT, 0, &av),
          "telem_at bad index false");
    telem_reset();
}

static void check_telem_hw(void)
{
    const char *mi =
        "MemTotal:       32043180 kB\n"
        "MemFree:        17549508 kB\n"
        "MemAvailable:   24906408 kB\n"
        "Buffers:            2992 kB\n"
        "SwapTotal:       8388604 kB\n"
        "SwapFree:        8388600 kB\n";
    long kb;
    CHECK(hw_parse_meminfo_kb(mi, "MemTotal", &kb) == 0 && kb == 32043180,
          "meminfo total");
    CHECK(hw_parse_meminfo_kb(mi, "MemAvailable", &kb) == 0 && kb == 24906408,
          "meminfo available");
    CHECK(hw_parse_meminfo_kb(mi, "SwapFree", &kb) == 0 && kb == 8388600,
          "meminfo swapfree");
    CHECK(hw_parse_meminfo_kb(mi, "SwapCached", &kb) == -1,
          "meminfo missing key honest");
    CHECK(hw_parse_meminfo_kb(mi, "Mem", &kb) == -1,
          "meminfo needs full key+colon");

    int t, mw, mhz, util, vu, vt;
    char ps[8];
    unsigned rs;
    hw_parse_gpu_csv("44, 12.55, 210, 0, 1228, 8192", &t, &mw, &mhz, &util,
                     &vu, &vt, NULL, 0, NULL);
    CHECK(t == 44 && mw == 12550 && mhz == 210 && util == 0 && vu == 1228 &&
              vt == 8192,
          "gpu csv full row");
    hw_parse_gpu_csv("44, [N/A], 210, [N/A], 1228, 8192", &t, &mw, &mhz,
                     &util, &vu, &vt, NULL, 0, NULL);
    CHECK(t == 44 && mw == -1 && util == -1 && vu == 1228 && vt == 8192,
          "gpu csv N/A fields stay -1");
    hw_parse_gpu_csv("Failed to initialize NVML", &t, &mw, &mhz, &util, &vu,
                     &vt, NULL, 0, NULL);
    CHECK(t == -1 && mw == -1, "gpu csv garbage honest");
    /* extended form: 6 numerics + "P2, Not Active, ..." reason flags —
     * fields 8.. in query order: idle, app clocks, sw power cap, ... */
    hw_parse_gpu_csv("45, 6.08, 210, 0, 8192, 16384, P2, Not Active, "
                     "Not Active, Active, Not Active, Not Active, "
                     "Not Active, Not Active, Not Active, Not Active",
                     &t, &mw, &mhz, &util, &vu, &vt, ps, sizeof ps, &rs);
    CHECK(t == 45 && mw == 6080 && util == 0 && vt == 16384,
          "gpu csv extended numeric row");
    CHECK(strcmp(ps, "P2") == 0, "gpu csv pstate token");
    CHECK(rs == (1u << 2), "gpu csv active reason bit");
    hw_parse_gpu_csv("44, 12.55, 210, 0, 1228, 8192", &t, &mw, &mhz, &util,
                     &vu, &vt, ps, sizeof ps, &rs);
    CHECK(ps[0] == '\0' && rs == 0,
          "gpu csv plain query leaves ext unknown");
    hw_parse_gpu_csv("46, 10.0, 2400, 99, 16000, 16384, P0, Active, "
                     "Not Active, Not Active, Active, Not Active, "
                     "Not Active, Active, Not Active, Not Active",
                     &t, &mw, &mhz, &util, &vu, &vt, ps, sizeof ps, &rs);
    CHECK(strcmp(ps, "P0") == 0 && rs == 0x49u,
          "gpu csv multiple reason bits");

    char th[128];
    hw_gpu_throttle_str(0, th, sizeof th);
    CHECK(strcmp(th, "none") == 0, "gpu throttle none wording");
    hw_gpu_throttle_str((1u << 3) | (1u << 6), th, sizeof th);
    CHECK(strcmp(th, "hw slowdown, hw thermal") == 0,
          "gpu throttle label join");

    CHECK(!hw_net_if_whole("lo") && !hw_net_if_whole("veth0") &&
              !hw_net_if_whole("docker0") && !hw_net_if_whole("br-a1b2c3") &&
              !hw_net_if_whole("virbr0") && !hw_net_if_whole("tap0") &&
              !hw_net_if_whole("tun0") && !hw_net_if_whole("wg0"),
          "net classifier skips plumbing");
    CHECK(hw_net_if_whole("enp3s0") && hw_net_if_whole("wlan0") &&
              hw_net_if_whole("eth0"),
          "net classifier keeps physical");

    /* pp_dpm_sclk: real driver output shape, '*' marks the active level */
    int dn, da, dm;
    hw_parse_pp_dpm("0: 400Mhz \n1: 2000Mhz *\n2: 2200Mhz \n",
                    &dn, &da, &dm);
    CHECK(dn == 3 && da == 1 && dm == 2000, "pp_dpm three levels, active 1");
    hw_parse_pp_dpm("0: 400Mhz \n1: 2000Mhz \n", &dn, &da, &dm);
    CHECK(dn == 2 && da == -1, "pp_dpm no active marker honest");
    hw_parse_pp_dpm("", &dn, &da, &dm);
    CHECK(dn == 0 && da == -1 && dm == -1, "pp_dpm empty");
    hw_parse_pp_dpm("garbage line\nstill not a level\n", &dn, &da, &dm);
    CHECK(dn == 0, "pp_dpm garbage ignored");

    /* gpu_metrics: signature-gated gfx-power read */
    unsigned char gm[64];
    memset(gm, 0xFF, sizeof gm);
    CHECK(hw_metrics_gfx_mw(gm, sizeof gm) == -1,
          "metrics wrong signature rejected");
    gm[0] = 120; gm[1] = 0; gm[2] = 2; gm[3] = 1;
    gm[28] = 0x58; gm[29] = 0x26; gm[30] = 0; gm[31] = 0; /* 0x2658 = 9816 mW */
    CHECK(hw_metrics_gfx_mw(gm, sizeof gm) == 9816, "metrics gfx mW read");
    gm[28] = 0xFF; gm[29] = 0xFF; gm[30] = 0xFF; gm[31] = 0x7F;
    CHECK(hw_metrics_gfx_mw(gm, sizeof gm) == -1,
          "metrics out-of-range value rejected");
    CHECK(hw_metrics_gfx_mw(gm, 16) == -1, "metrics short buffer rejected");

    char fb[24];
    hw_fmt_bytes(fb, sizeof fb, -1);
    CHECK(strcmp(fb, "--") == 0, "fmt bytes unknown");
    hw_fmt_bytes(fb, sizeof fb, 512 * 1024 * 1024);
    CHECK(strcmp(fb, "512 MB") == 0, "fmt bytes MB");
    hw_fmt_bytes(fb, sizeof fb, 32LL * 1024 * 1024 * 1024 + 300 * 1000 * 1000);
    CHECK(strcmp(fb, "32.3 GB") == 0, "fmt bytes GB");

    const char *ds =
        "   1       0 loop0 0 0 0 0 0 0 0 0 0 0\n"
        " 259       0 nvme0c0n1 12345 5 100000 500 678 7 90000 400 0 0 0\n"
        " 259       1 nvme0c0n1p1 12340 5 99500 498 670 7 89800 398 0 0 0\n"
        "   8       0 sda 10 1 2000 30 5 2 3000 20 0 0 0\n"
        "   8       1 sda1 10 1 2000 30 5 2 3000 20 0 0 0\n";
    long long rd, wr;
    CHECK(hw_parse_diskstats(ds, &rd, &wr) == 0 && rd == 102000 && wr == 93000,
          "diskstats whole disks only (no parts/loops)");
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

static void check_fan_write_verdict(void)
{
    /* hwmon present, everything verified (asusctl attempted or not) */
    CHECK(fan_write_verdict(0, 8, 8, -1) == FAN_W_VERIFIED, "verdict verified");
    CHECK(fan_write_verdict(0, 8, 8, 1) == FAN_W_VERIFIED, "verdict verified+asusctl");
    /* read-back mismatch keeps staging */
    CHECK(fan_write_verdict(0, 6, 8, 1) == FAN_W_VERIFY_FAIL, "verdict mismatch");
    CHECK(fan_write_verdict(0, 6, 8, -1) == FAN_W_VERIFY_FAIL, "verdict mismatch no-asusctl");
    /* no fan hwmon: the asusctl outcome is everything (the old code
     * claimed plain success here for all three) */
    CHECK(fan_write_verdict(0, -1, -1, 1) == FAN_W_ASUSCTL, "verdict no-hwmon asusctl ok");
    CHECK(fan_write_verdict(0, -1, -1, 0) == FAN_W_FAILED, "verdict no-hwmon asusctl errored");
    CHECK(fan_write_verdict(0, -1, -1, -1) == FAN_W_FAILED, "verdict no-hwmon no-asusctl");
    /* sysfs failed: asusctl rescues or not */
    CHECK(fan_write_verdict(-1, -1, -1, 1) == FAN_W_ASUSCTL, "verdict sysfs-dead asusctl ok");
    CHECK(fan_write_verdict(-1, 6, 8, 0) == FAN_W_FAILED, "verdict sysfs-dead asusctl errored");
    CHECK(fan_write_verdict(-1, 6, 8, -1) == FAN_W_FAILED, "verdict sysfs-dead no-asusctl");
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

static void check_singleton(void)
{
    char tmp[] = "/tmp/ctron2-test-XXXXXX";
    CHECK(mkdtemp(tmp) != NULL, "single mkdtemp");
    const char *old = getenv("XDG_RUNTIME_DIR");
    setenv("XDG_RUNTIME_DIR", tmp, 1);
    /* earlier tests leak CTRON_CONFIG into the process env (they set
     * it and never restore); the guard's bypass must not fire here */
    const char *old_cfg = getenv("CTRON_CONFIG");
    const char *old_test = getenv("CTRON_TEST");
    unsetenv("CTRON_CONFIG");
    unsetenv("CTRON_TEST");

    /* decision helper: every unreadable shape is stale */
    CHECK(ut_singleton_stale("", getpid(), "test_core"), "stale empty");
    CHECK(ut_singleton_stale("garbage", getpid(), "test_core"), "stale garbage");
    CHECK(ut_singleton_stale("12x4", getpid(), "test_core"), "stale partial");
    char self[24];
    snprintf(self, sizeof self, "%ld", (long)getpid());
    CHECK(ut_singleton_stale(self, getpid(), "test_core"), "stale self");
    CHECK(ut_singleton_stale("999999999", getpid(), "test_core"), "stale dead");
    CHECK(ut_singleton_stale("1\n", getpid(), "test_core"), "stale init");

    /* live other: a forked child shares our comm */
    pid_t ch = fork();
    CHECK(ch >= 0, "single fork");
    if (ch == 0) {
        pause();
        _exit(0);
    }
    char chp[24];
    snprintf(chp, sizeof chp, "%ld", (long)ch);
    CHECK(!ut_singleton_stale(chp, getpid(), "test_core"), "live child");
    char path[300];
    snprintf(path, sizeof(path), "%s/ctron-tui.pid", tmp);
    CHECK(ut_write_file(path, chp) == 0, "single write child pid");
    CHECK(ut_singleton_enter("test_core") == (int)ch, "enter defers to child");
    kill(ch, SIGTERM);
    waitpid(ch, NULL, 0);

    /* stale after the child dies: claim, verify the file, leave */
    CHECK(ut_singleton_enter("test_core") == 0, "enter claims after death");
    char buf[64];
    CHECK(ut_read_file(path, buf, sizeof(buf)) == 0 &&
              atol(buf) == (long)getpid(), "pidfile holds us");
    ut_singleton_leave();
    CHECK(!ut_path_exists(path), "leave removes");

    /* hardened claim: a planted symlink is never followed — the victim
     * file survives and the slot is retaken by a regular 0600 file */
    char victim[320];
    snprintf(victim, sizeof(victim), "%s/victim", tmp);
    CHECK(ut_write_file(victim, "VICTIM") == 0, "victim written");
    CHECK(symlink(victim, path) == 0, "pidfile symlink planted");
    CHECK(ut_singleton_enter("test_core") == 0, "enter replaces symlink");
    struct stat st;
    CHECK(lstat(path, &st) == 0 && S_ISREG(st.st_mode), "pidfile regular");
    CHECK((st.st_mode & 0777) == 0600, "pidfile mode 0600");
    char vbuf[32];
    CHECK(ut_read_file(victim, vbuf, sizeof(vbuf)) == 0 &&
              strcmp(vbuf, "VICTIM") == 0, "victim untouched");
    ut_singleton_leave();
    CHECK(!ut_path_exists(path), "leave removes (post-symlink)");

    if (old)
        setenv("XDG_RUNTIME_DIR", old, 1);
    else
        unsetenv("XDG_RUNTIME_DIR");
    if (old_cfg)
        setenv("CTRON_CONFIG", old_cfg, 1);
    else
        unsetenv("CTRON_CONFIG");
    if (old_test)
        setenv("CTRON_TEST", old_test, 1);
    else
        unsetenv("CTRON_TEST");
    rmdir(tmp);
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

static void check_ppt_ryzen_argv(void)
{
    const char *av[10];
    const char *wv[7];
    char bufs[3][16];

    /* name-based mapping: SPL->STAPM(-a), SPPT->SLOW(-c), FPPT->FAST(-b),
     * W in -> mW out, fixed flag order — argv words, no shell string */
    ctrl_ppt_ryzen_argv("/usr/bin/ryzenadj", 30, 50, 40, av, bufs);
    CHECK(!strcmp(av[0], "sudo") && !strcmp(av[1], "-n") &&
          !strcmp(av[2], "/usr/bin/ryzenadj") && !strcmp(av[3], "-a") &&
          !strcmp(av[4], "30000") && !strcmp(av[5], "-c") &&
          !strcmp(av[6], "50000") && !strcmp(av[7], "-b") &&
          !strcmp(av[8], "40000") && av[9] == NULL,
          "ryzen argv mapping + mW");

    ctrl_ppt_ryzen_argv(NULL, 45, 55, 55, av, bufs);
    CHECK(!strcmp(av[2], "ryzenadj") && !strcmp(av[4], "45000") &&
          !strcmp(av[6], "55000") && !strcmp(av[8], "55000") && !av[9],
          "ryzen argv bare fallback");

    /* only integers ever enter the words — no injection surface */
    ctrl_ppt_ryzen_argv("/x/r", 0, 0, 0, av, bufs);
    CHECK(!strcmp(av[2], "/x/r") && !strcmp(av[4], "0") &&
          !strcmp(av[6], "0") && !strcmp(av[8], "0"), "ryzen argv zero");

    /* wrapper variant (SECURITY.md §4.3): bare mW integers, no flags */
    ctrl_ppt_wrapper_argv(CTRON_PPT_WRAPPER, 30, 50, 40, wv, bufs);
    CHECK(!strcmp(wv[0], "sudo") && !strcmp(wv[1], "-n") &&
          !strcmp(wv[2], CTRON_PPT_WRAPPER) && !strcmp(wv[3], "30000") &&
          !strcmp(wv[4], "50000") && !strcmp(wv[5], "40000") &&
          wv[6] == NULL,
          "wrapper argv bare ints");
    ctrl_ppt_wrapper_argv(NULL, 1, 2, 3, wv, bufs);
    CHECK(!strcmp(wv[2], CTRON_PPT_WRAPPER) && !strcmp(wv[3], "1000") &&
          !strcmp(wv[4], "2000") && !strcmp(wv[5], "3000"),
          "wrapper argv default path");
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

static void *log_hammer(void *arg)
{
    (void)arg;
    for (int i = 0; i < 200; i++)
        ut_log("hammer %d", i);
    return NULL;
}

static void check_progress_and_log(void)
{
    char t[96];
    int pct;

    ut_progress_clear();
    ut_progress_get(t, sizeof(t), &pct);
    CHECK(t[0] == '\0' && pct == -1, "progress cleared");

    ut_progress("step %d of %d", 3, 8);
    ut_progress_pct(42);
    ut_progress_get(t, sizeof(t), &pct);
    CHECK(!strcmp(t, "step 3 of 8") && pct == 42, "progress roundtrip");

    /* concurrent ring writes must not corrupt or exceed the ring */
    pthread_t th;
    CHECK(pthread_create(&th, NULL, log_hammer, NULL) == 0, "hammer spawn");
    for (int i = 0; i < 200; i++)
        ut_log("main %d", i);
    pthread_join(th, NULL);
    int n = ut_log_count();
    CHECK(n >= 1 && n <= 24, "log count within the ring");
    for (int i = 0; i < 3; i++) {
        const char *l = ut_log_get(i);
        CHECK(l[0] >= '0' && l[0] <= '9', "log entry has a timestamp");
    }
}

static void check_fan_presets(void)
{
    CHECK(!strcmp(fan_preset_name(FAN_PRESET_QUIET), "Quiet"), "preset quiet name");
    CHECK(!strcmp(fan_preset_name(FAN_PRESET_BALANCED), "Balanced"), "preset bal name");
    CHECK(!strcmp(fan_preset_name(FAN_PRESET_PERF), "Performance"), "preset perf name");
    CHECK(!strcmp(fan_preset_name(FAN_PRESET_ACTIVE), "Active (Live)"), "preset active name");

    /* RON parse test with sample RON text */
    static const char *SAMPLE_RON =
        "(\n"
        "    profiles: (\n"
        "        balanced: [\n"
        "            (\n"
        "                fan: CPU,\n"
        "                pwm: (20, 40, 60, 80, 100, 120, 140, 160),\n"
        "                temp: (30, 40, 50, 60, 70, 80, 90, 100),\n"
        "                enabled: true,\n"
        "            ),\n"
        "            (\n"
        "                fan: GPU,\n"
        "                pwm: (15, 30, 45, 60, 75, 90, 105, 120),\n"
        "                temp: (35, 45, 55, 65, 75, 85, 95, 105),\n"
        "                enabled: true,\n"
        "            ),\n"
        "        ],\n"
        "        quiet: [\n"
        "            (\n"
        "                fan: CPU,\n"
        "                pwm: (10, 20, 30, 40, 50, 60, 70, 80),\n"
        "                temp: (35, 45, 55, 65, 75, 85, 95, 105),\n"
        "            ),\n"
        "            (\n"
        "                fan: GPU,\n"
        "                pwm: (12, 22, 32, 42, 52, 62, 72, 82),\n"
        "                temp: (36, 46, 56, 66, 76, 86, 96, 105),\n"
        "            ),\n"
        "        ],\n"
        "    ),\n"
        ")\n";

    fan_curve_t cpu = {0}, gpu = {0};
    bool c_en = true, g_en = true;
    CHECK(fan_ron_parse(SAMPLE_RON, FAN_PRESET_BALANCED, &cpu, &gpu, &c_en, &g_en) == 0,
          "ron parse balanced");
    CHECK(cpu.n == 8 && cpu.temp_c[0] == 30 && cpu.pwm[0] == 20, "ron bal cpu pt 0");
    CHECK(cpu.temp_c[7] == 100 && cpu.pwm[7] == 160, "ron bal cpu pt 7");
    CHECK(gpu.n == 8 && gpu.temp_c[0] == 35 && gpu.pwm[0] == 15, "ron bal gpu pt 0");
    CHECK(gpu.temp_c[7] == 105 && gpu.pwm[7] == 120, "ron bal gpu pt 7");
    CHECK(c_en && g_en, "ron bal both enabled");

    memset(&cpu, 0, sizeof(cpu));
    memset(&gpu, 0, sizeof(gpu));
    CHECK(fan_ron_parse(SAMPLE_RON, FAN_PRESET_QUIET, &cpu, &gpu, &c_en, &g_en) == 0,
          "ron parse quiet");
    CHECK(cpu.n == 8 && cpu.pwm[0] == 10, "ron quiet cpu");
    CHECK(gpu.n == 8 && gpu.pwm[0] == 12, "ron quiet gpu");
    CHECK(c_en && g_en, "ron quiet enabled defaults true (field absent)");

    /* perf not in sample text -> returns -1 */
    CHECK(fan_ron_parse(SAMPLE_RON, FAN_PRESET_PERF, &cpu, &gpu, NULL, NULL) == -1,
          "ron parse missing perf");

    /* enabled:false parsing + per-fan attribution (GPU off, CPU on) */
    static const char *DISABLED_RON =
        "(\n"
        "    profiles: (\n"
        "        performance: [\n"
        "            (\n"
        "                fan: CPU,\n"
        "                pwm: (10, 20, 30, 40, 50, 60, 70, 80),\n"
        "                temp: (40, 50, 60, 70, 80, 90, 100, 105),\n"
        "                enabled: true,\n"
        "            ),\n"
        "            (\n"
        "                fan: GPU,\n"
        "                pwm: (12, 22, 32, 42, 52, 62, 72, 82),\n"
        "                temp: (45, 55, 65, 75, 85, 95, 100, 105),\n"
        "                enabled: false,\n"
        "            ),\n"
        "        ],\n"
        "    ),\n"
        ")\n";
    c_en = g_en = true;
    CHECK(fan_ron_parse(DISABLED_RON, FAN_PRESET_PERF, &cpu, &gpu, &c_en, &g_en) == 0,
          "ron parse disabled perf");
    CHECK(c_en && !g_en, "ron disabled gpu only");

    /* built-in snapshot: Balanced was synced to the live ron on
     * 2026-10-17 (the old GPU table was a different machine's step
     * curve) — pin every point so drift is caught */
    fan_curve_t b_cpu = {0}, b_gpu = {0};
    CHECK(fan_preset_builtin(FAN_PRESET_BALANCED, &b_cpu, &b_gpu) == 0, "builtin balanced");
    static const int bal_ct[8] = { 40, 54, 61, 67, 71, 75, 80, 90 };
    static const int bal_cp[8] = { 22, 75, 80, 98, 125, 160, 190, 210 };
    static const int bal_gt[8] = { 51, 53, 59, 64, 66, 70, 74, 83 };
    static const int bal_gp[8] = { 33, 53, 73, 93, 140, 180, 215, 245 };
    bool bal_ok = b_cpu.n == 8 && b_gpu.n == 8;
    for (int i = 0; i < 8 && bal_ok; i++)
        bal_ok = b_cpu.temp_c[i] == bal_ct[i] && b_cpu.pwm[i] == bal_cp[i] &&
                 b_gpu.temp_c[i] == bal_gt[i] && b_gpu.pwm[i] == bal_gp[i];
    CHECK(bal_ok, "builtin balanced pinned (synced 2026-10-17)");
    fan_curve_t q_cpu = {0}, pfc = {0};
    CHECK(fan_preset_builtin(FAN_PRESET_QUIET, &q_cpu, NULL) == 0 &&
              q_cpu.n == 8 && q_cpu.temp_c[0] == 38 && q_cpu.pwm[7] == 100,
          "builtin quiet spot pins");
    CHECK(fan_preset_builtin(FAN_PRESET_PERF, NULL, &pfc) == 0 &&
              pfc.n == 8 && pfc.temp_c[7] == 105 && pfc.pwm[0] == 78,
          "builtin perf spot pins");
    CHECK(fan_preset_builtin(FAN_PRESET_ACTIVE, &q_cpu, &pfc) == -1,
          "builtin rejects active");

    /* fan_preset_get for Quiet, Balanced, Performance (source is the
     * real /etc/asusd file when present, else the pinned snapshot) */
    fan_curve_t p_cpu = {0}, p_gpu = {0};
    fan_preset_info_t info = {0};
    CHECK(fan_preset_get(NULL, FAN_PRESET_QUIET, &p_cpu, &p_gpu, &info) == 0, "preset get quiet");
    CHECK(p_cpu.n == 8 && p_gpu.n == 8, "quiet 8 points");
    CHECK(p_cpu.temp_c[0] < p_cpu.temp_c[7], "quiet cpu sorted");
    CHECK(p_gpu.temp_c[0] < p_gpu.temp_c[7], "quiet gpu sorted");
    CHECK(info.src == FAN_PSRC_RON || info.src == FAN_PSRC_BUILTIN, "quiet src ron|builtin");

    CHECK(fan_preset_get(NULL, FAN_PRESET_BALANCED, &p_cpu, &p_gpu, &info) == 0, "preset get balanced");
    CHECK(p_cpu.n == 8 && p_gpu.n == 8, "balanced 8 points");
    CHECK(info.src == FAN_PSRC_RON || info.src == FAN_PSRC_BUILTIN, "balanced src ron|builtin");

    CHECK(fan_preset_get(NULL, FAN_PRESET_PERF, &p_cpu, &p_gpu, NULL) == 0, "preset get perf");
    CHECK(p_cpu.n == 8 && p_gpu.n == 8, "perf 8 points");

    /* Active preset with hw: live hwmon when the machine has it, else
     * the seeded init snapshot (machine-dependent, both valid) */
    hw_state_t hw;
    memset(&hw, 0, sizeof(hw));
    fan_from_csv(&hw.fan_cpu_stock, "30,50,70", "40,80,120");
    fan_from_csv(&hw.fan_gpu_stock, "35,55,75", "50,90,130");
    CHECK(fan_preset_get(&hw, FAN_PRESET_ACTIVE, &p_cpu, &p_gpu, &info) == 0, "preset get active");
    CHECK(p_cpu.n >= 3 && p_cpu.temp_c[0] >= FAN_TMIN && p_cpu.temp_c[p_cpu.n - 1] <= FAN_TMAX,
          "active cpu returns valid curve points");
    CHECK(p_gpu.n >= 3 && p_gpu.temp_c[0] >= FAN_TMIN && p_gpu.temp_c[p_gpu.n - 1] <= FAN_TMAX,
          "active gpu returns valid curve points");
    CHECK(info.src == FAN_PSRC_HWMON || info.src == FAN_PSRC_STOCK, "active src hwmon|stock");

    /* Active preset with hw == NULL falls back to default curve */
    fan_curve_t d_cpu = {0}, d_gpu = {0};
    CHECK(fan_preset_get(NULL, FAN_PRESET_ACTIVE, &d_cpu, &d_gpu, &info) == 0, "active NULL hw");
    CHECK(d_cpu.n == 8 && d_cpu.temp_c[0] == 30, "active NULL falls back to default");
    CHECK(info.src == FAN_PSRC_DEFAULT, "active NULL src default");


    /* Verify startup honesty: settings_load does not overwrite live hwmon curves or set fan_staged */
    char tmp[] = "/tmp/ctron-test-staged-XXXXXX";
    CHECK(mkdtemp(tmp) != NULL, "mkdtemp staged");
    setenv("CTRON_CONFIG", tmp, 1);

    memset(&hw, 0, sizeof(hw));
    fan_from_csv(&hw.fan_cpu, "40,60,80", "80,140,220");
    fan_from_csv(&hw.fan_gpu, "45,75", "60,200");
    CHECK(settings_save(&hw) == 0, "settings save staged test");

    /* Simulate hw_init with live hwmon curves present */
    hw_state_t live_hw;
    memset(&live_hw, 0, sizeof(live_hw));
    live_hw.has_fan_curve = true;
    fan_from_csv(&live_hw.fan_cpu, "35,65", "50,150");
    fan_from_csv(&live_hw.fan_gpu, "40,70", "60,160");
    live_hw.fan_staged = false;

    CHECK(settings_load(&live_hw) == 0, "settings load on live hw");
    CHECK(!live_hw.fan_staged, "live hw startup does not set fan_staged");
    CHECK(live_hw.fan_cpu.n == 2 && live_hw.fan_cpu.temp_c[0] == 35, "live cpu curve preserved");
    CHECK(live_hw.fan_gpu.n == 2 && live_hw.fan_gpu.temp_c[0] == 40, "live gpu curve preserved");
}

int main(void)
{
    check_fan_csv();
    check_fan_edit();
    check_fan_presets();
    check_settings_roundtrip();
    check_modes();

    check_profiles();
    check_kde_parser();
    check_hypr_parser();
    check_telem();
    check_telem_hw();
    check_power_fmt();
    check_cpu_list_parse();
    check_topology_group();
    check_asusd_enforced();
    check_asusd_ron_parse();
    check_read_file_all();
    check_util_write();
    check_argv_exec();
    check_mode_split();
    check_fan_enable();
    check_fan_write_verdict();
    check_singleton();
    check_gpu_temp_gate();
    check_cell_join();
    check_mode_touch_mask();
    check_mode_drift();
    check_ppt_order();
    check_ppt_ryzen_argv();
    check_gpu_clock();
    check_snapshots();
    check_progress_and_log();

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("core tests ok\n");
    return 0;
}
