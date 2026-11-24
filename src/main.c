#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#include "cmds.h"
#include "control.h"
#include "daeboard.h"
#include "hw.h"
#include "modes.h"
#include "profile.h"
#include "settings.h"
#include "ui/ui.h"
#include "util.h"
#include "display/display.h"

#define PROG "ctron"
#define VERSION "0.12.0"

/* ---- output helpers ----------------------------------------------------- */

static void print_version(void)
{
    printf("%s %s — ASUS laptop control center\n", PROG, VERSION);
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [flags] | %s mode <sub> | %s snapshot <sub>\n\n", prog, prog, prog);
    printf("No arguments opens the fullscreen TUI (needs a terminal).\n\n");
    printf("Information:\n");
    printf("  --status, -s [--json]   live hardware snapshot\n");
    printf("  --watch, -w             rolling one-line telemetry (Ctrl-C stops)\n");
    printf("  --csv <file>            append telemetry rows to a CSV (Ctrl-C stops)\n");
    printf("  --doctor [--json]       capability report\n");
    printf("  --help, -h              this help\n");
    printf("  --version, -V           version\n\n");
    printf("Modes & snapshots:\n");
    printf("  --mode <name> [--dry-run]   apply (or preview) a shortcut bundle\n");
    printf("  mode list|show <n>|add <name> <steps>|delete <n>\n");
    printf("  snapshot list   (alias: profile)\n");
    printf("  snapshot apply [--dry-run]|diff|export|delete <name>\n\n");
    printf("Hardware (key = value pairs, same as mode steps):\n");
    printf("  --profile quiet|balanced|performance\n");
    printf("  --ac-profile off|quiet|balanced|performance\n");
    printf("  --battery-profile off|quiet|balanced|performance\n");
    printf("  --epp power|balance_power|balance_performance|performance\n");
    printf("  --freq <mhz>                 every thread\n");
    printf("  --freq core <cpu-id> <mhz>   one thread (kernel id)\n");
    printf("  --freq ccd <n> <mhz>         every thread in that CCD\n");
    printf("  --hz <rate|max>         display refresh (compositor backend)\n");
    printf("  --battery <20..100>     charge limit\n");
    printf("  --battery-oneshot       charge to full once\n");
    printf("  --ppt Q45|B60|P80|<spl>,<sppt>,<fppt>|off|on\n");
    printf("  --nv-boost <5..25 W>    NVIDIA dynamic boost\n");
    printf("  --nv-temp <75..87 C>    NVIDIA temp target\n");
    printf("  --gpu-clock <mhz>|reset NVIDIA core-clock lock\n");
    printf("  --panel-od on|off       panel overdrive\n");
    printf("  --cpu-boost on|off      cpufreq boost\n");
    printf("  --kbd off|low|med|high  keyboard backlight\n");
    printf("  --follow            run ctron actions from daeboard binds\n");
    printf("  --daeboard-start    start the daemon (sudo -n systemd-run)\n");
    printf("  --daeboard-stop     ask the daemon to quit\n");
    printf("  --daeboard-reload   recompile ~/.config/ctron/daeboard.binds\n");
    printf("  --aura <effect> [color|hex]\n");
    printf("  --fan stock|silent|cool|full|on|off|cpu on|cpu off|gpu on|gpu off\n");
    printf("  --fan-curve cpu|gpu <temps> <pwms>\n");
    printf("  --fan-write             write in-memory curve to the EC\n");
    printf("  --dry-run               validate flags/mode/snapshot without writing\n\n");
    printf("Config: %s [--config-dir DIR]\n", prog);
    printf("Exit codes: 0 ok · 1 runtime/verify error · 2 usage error · 3 permission/sudo denied\n");
}

/* ---- minimal zero-dependency JSON helpers -------------------------------- */

static void json_str(const char *s)
{
    if (!s) {
        fputs("null", stdout);
        return;
    }
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"':  fputs("\\\"", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '\b': fputs("\\b", stdout);  break;
        case '\f': fputs("\\f", stdout);  break;
        case '\n': fputs("\\n", stdout);  break;
        case '\r': fputs("\\r", stdout);  break;
        case '\t': fputs("\\t", stdout);  break;
        default:
            if (*p < 0x20)
                printf("\\u%04x", *p);
            else
                putchar(*p);
        }
    }
    putchar('"');
}

static void json_int_min(int v, int min_valid)
{
    if (v < min_valid)
        fputs("null", stdout);
    else
        printf("%d", v);
}

static void json_ll(long long v)
{
    if (v < 0)
        fputs("null", stdout);
    else
        printf("%lld", v);
}

static void json_dbl(double v, const char *fmt)
{
    if (v < 0)
        fputs("null", stdout);
    else
        printf(fmt, v);
}

static void json_asusd_auto(int v)
{
    if (v == -1)
        json_str("off");
    else if (v >= 0 && v < HW_PROF_COUNT)
        json_str(hw_profile_name((hw_profile_t)v));
    else
        fputs("null", stdout);
}

/* ---- status / doctor / watch --------------------------------------------- */

/* "--" for unknown values, else the number — into the caller's buffer
 * (no static ring: a printf with several of these cannot collide). */
static const char *dash_if(char *buf, size_t n, int v)
{
    if (v < 0)
        return "--";
    snprintf(buf, n, "%d", v);
    return buf;
}

static int cmd_status(hw_state_t *hw)
{
    char v1[16], v2[16], v3[16], v4[16], v5[16];
    printf("ctron — %s\n", hw->model);
    printf("  CPU            : %s\n", hw->cpu);
    if (hw->cpu_temp_ccd1 > 0 && hw->cpu_temp_ccd2 > 0)
        printf("  CPU temp       : %s °C (k10temp) · Tccd %d/%d °C\n",
               dash_if(v1, sizeof(v1), hw->cpu_temp),
               hw->cpu_temp_ccd1, hw->cpu_temp_ccd2);
    else if (hw->cpu_temp_ccd1 > 0)
        printf("  CPU temp       : %s °C (k10temp) · Tccd %d °C\n",
               dash_if(v1, sizeof(v1), hw->cpu_temp), hw->cpu_temp_ccd1);
    else
        printf("  CPU temp       : %s °C (k10temp)\n",
               dash_if(v1, sizeof(v1), hw->cpu_temp));
    printf("  GPU temp       : %s °C (nvidia-smi)\n", dash_if(v2, sizeof(v2), hw->gpu_temp));
    printf("  CPU clock      : %d MHz (limit %d MHz)\n", hw->cpu_mhz_cur, hw->cpu_mhz_limit);
    if (hw->gpu_mhz > 0 || hw->gpu_mw >= 0 || hw->gpu_util >= 0) {
        if (hw->gpu_mw >= 0)
            snprintf(v3, sizeof(v3), "%d.%03d",
                     hw->gpu_mw / 1000, hw->gpu_mw % 1000);
        else
            snprintf(v3, sizeof(v3), "--");
        printf("  GPU live       : %s MHz · %s W · util %s %% · pstate %s\n",
               dash_if(v1, sizeof(v1), hw->gpu_mhz), v3,
               dash_if(v4, sizeof(v4), hw->gpu_util),
               hw->gpu_pstate[0] ? hw->gpu_pstate : "--");
    }
    if (hw->gpu_pstate[0] && hw->gpu_throttle) {
        char th[128];
        hw_gpu_throttle_str(hw->gpu_throttle, th, sizeof th);
        printf("  GPU throttle   : %s\n", th);
    }
    if (hw->igpu_temp > 0 || hw->igpu_mhz > 0) {
        int len = printf("  iGPU           : %s °C · %s MHz (amdgpu)\n",
                         dash_if(v1, sizeof(v1), hw->igpu_temp),
                         dash_if(v3, sizeof(v3), hw->igpu_mhz));
        (void)len;
        if (hw->igpu_busy >= 0)
            printf("  iGPU busy      : %d %%", hw->igpu_busy);
        if (hw->igpu_busy >= 0 && hw->igpu_dpm >= 0)
            printf(" · sclk DPM %d/%d @ %d MHz", hw->igpu_dpm + 1,
                   hw->igpu_dpm_n, hw->igpu_dpm_mhz);
        if (hw->igpu_busy >= 0)
            printf("\n");
    }
    if (hw->mem_total_mb > 0)
        printf("  RAM            : %s/%d MB used · modules %s%s °C%s\n",
               dash_if(v2, sizeof(v2), hw->mem_used_mb), hw->mem_total_mb,
               hw->ram_temp_n > 0 ? dash_if(v1, sizeof(v1), hw->ram_temp[0]) : "--",
               hw->ram_temp_n > 1 ? "/…" : "",
               hw->ram_alarm ? " !over max" : "");
    if (hw->nvme_temp_n > 0 || hw->fs_total_mb > 0)
        printf("  Storage        : NVMe %s °C%s · / %s/%s MB\n",
               hw->nvme_temp_n > 0 ? dash_if(v2, sizeof(v2), hw->nvme_temp[0]) : "--",
               hw->nvme_temp_n > 1 ? "+1" : "",
               dash_if(v3, sizeof(v3), hw->fs_used_mb),
               hw->fs_total_mb > 0 ? dash_if(v4, sizeof(v4), hw->fs_total_mb) : "--");
    if (hw->net_n > 0) {
        char line[128];
        int len = snprintf(line, sizeof line, " ");
        for (int i = 0; i < hw->net_n; i++) {
            if (len < 0 || (size_t)len >= sizeof line)
                break;
            len += snprintf(line + len, sizeof line - len, "%s%s %s",
                            i ? " · " : "", hw->net_if[i],
                            hw->net_up[i] ? "up" : "down");
        }
        char rb[20], tb[20];
        hw_fmt_bytes(rb, sizeof rb, hw->net_rx_tot);
        hw_fmt_bytes(tb, sizeof tb, hw->net_tx_tot);
        if (len > 0 && (size_t)len < sizeof line)
            len += snprintf(line + len, sizeof line - len,
                            " · rx %s · tx %s total", rb, tb);
        if (hw->wifi_link_pct >= 0 && len > 0 && (size_t)len < sizeof line) {
            len += snprintf(line + len, sizeof line - len,
                            " · wifi %d%%", hw->wifi_link_pct);
            if (hw->wifi_level_dbm < 0 && len > 0 &&
                (size_t)len < sizeof line)
                snprintf(line + len, sizeof line - len,
                         " (%d dBm)", hw->wifi_level_dbm);
        }
        printf("  NET            : %s\n", line);
    }
    if (hw->load1 >= 0 && hw->load5 >= 0 && hw->load15 >= 0)
        printf("  Load           : %.2f / %.2f / %.2f (1/5/15 min)\n",
               hw->load1, hw->load5, hw->load15);
    else if (hw->load1 >= 0)
        printf("  Load           : %.2f\n", hw->load1);
    printf("  Fan RPM        : %s / %s (cpu/gpu)\n",
           dash_if(v3, sizeof(v3), hw->rpm_cpu), dash_if(v4, sizeof(v4), hw->rpm_gpu));
    printf("  Profile        : %s\n", hw_profile_name(hw->profile));
    {
        int enf = hw_asusd_enforced(hw);
        if (enf >= 0 && enf != (int)hw->profile)
            printf("  asusd note     : %s takeover will revert this to %s "
                   "on the next power event\n",
                   hw->ac_online ? "AC" : "battery",
                   hw_profile_name((hw_profile_t)enf));
    }
    if (hw->asusd_ac != -2 || hw->asusd_bat != -2) {
        printf("  asusd auto     : AC %s · battery %s\n",
               hw->asusd_ac == -1 ? "off"
               : (hw->asusd_ac >= 0 ? hw_profile_name((hw_profile_t)hw->asusd_ac) : "--"),
               hw->asusd_bat == -1 ? "off"
               : (hw->asusd_bat >= 0 ? hw_profile_name((hw_profile_t)hw->asusd_bat) : "--"));
    }
    printf("  EPP            : %s\n", hw_epp_name(hw->epp));
    printf("  Display        : %s Hz (%s)\n",
           hw->hz_cur > 0 ? dash_if(v5, sizeof(v5), hw->hz_cur) : "--",
           display_get() ? display_get()->name : "no backend");
    if (hw->hz_count > 0) {
        printf("  Modes          : ");
        for (int i = 0; i < hw->hz_count; i++)
            printf("%s%d", i ? ", " : "", hw->hz_modes[i]);
        printf(" Hz\n");
    }
    char pwr[24];
    hw_fmt_power(pwr, sizeof(pwr), hw->bat_mw_known, hw->bat_mw);
    printf("  Battery        : %s%% %s %s%s limit %s%%\n",
           dash_if(v1, sizeof(v1), hw->bat_pct),
           hw->bat_status[0] ? hw->bat_status : "--",
           pwr, hw->ac_online ? " (AC)" : "",
           hw->bat_limit > 0 ? dash_if(v2, sizeof(v2), hw->bat_limit) : "--");
    printf("  PPT            : %s / %s / %s W (SPL/SPPT/FPPT; -- until written)\n",
           dash_if(v1, sizeof(v1), hw->ppt_spl),
           dash_if(v2, sizeof(v2), hw->ppt_sppt),
           dash_if(v3, sizeof(v3), hw->ppt_fppt));
    printf("  NV boost/temp  : %s W / %s °C\n",
           dash_if(v4, sizeof(v4), hw->nv_boost), dash_if(v5, sizeof(v5), hw->nv_temp));
    printf("  Panel OD       : %s   CPU boost: %s\n",
           hw->panel_od ? "on" : "off", hw->cpu_boost ? "on" : "off");
    printf("  Keyboard       : %s\n", hw_kbd_name(hw->kbd));
    {
        char ct[128], cp[128], gt[128], gp[128];
        fan_to_csv(&hw->fan_cpu, ct, sizeof(ct), cp, sizeof(cp));
        fan_to_csv(&hw->fan_gpu, gt, sizeof(gt), gp, sizeof(gp));
        printf("  Fan curve cpu  : %s %s (%s)\n", ct, cp, hw->fan_cpu_on ? "on" : "off");
        printf("  Fan curve gpu  : %s %s (%s)\n", gt, gp, hw->fan_gpu_on ? "on" : "off");
    }
    if (hw->gpu_clock_lock > 0)
        printf("  GPU clock      : locked %d MHz\n", hw->gpu_clock_lock);
    else if (hw->gpu_mhz_max > 0)
        printf("  GPU clock      : driver default (max %d MHz)\n", hw->gpu_mhz_max);
    if (hw->cpu_n > 0 && hw->cpu_mhz_limit > 0) {
        int below = 0;
        for (int c = 0; c < hw->cpu_n && c < HW_CPU_MAX; c++) {
            int id = hw->cpu_ids[c];
            int lim = hw->cpu_mhz_core[id];
            if (lim > 0 && hw->cpu_mhz_limit - lim > 2)
                below++;
        }
        if (below > 0)
            printf("  CPU cores      : %d of %d threads below %d MHz "
                   "(--freq ccd N M | --freq core ID M)\n",
                   below, hw->cpu_n, hw->cpu_mhz_limit);
    }
    return 0;
}

static int cmd_status_json(const hw_state_t *hw)
{
    const display_ops_t *d = display_get();
    printf("{\n  \"version\": "); json_str(VERSION);
    printf(",\n  \"model\": "); json_str(hw->model);
    printf(",\n  \"cpu\": "); json_str(hw->cpu);

    printf(",\n  \"temps\": {\"cpu_c\": "); json_int_min(hw->cpu_temp, 0);
    printf(", \"ccd1_c\": "); json_int_min(hw->cpu_temp_ccd1, 1);
    printf(", \"ccd2_c\": "); json_int_min(hw->cpu_temp_ccd2, 1);
    printf(", \"gpu_c\": "); json_int_min(hw->gpu_temp, 0);
    printf(", \"igpu_c\": "); json_int_min(hw->igpu_temp, 0);
    printf(", \"ram_c\": [");
    for (int i = 0; i < hw->ram_temp_n; i++)
        printf("%s%d", i ? ", " : "", hw->ram_temp[i]);
    printf("], \"nvme_c\": [");
    for (int i = 0; i < hw->nvme_temp_n; i++)
        printf("%s%d", i ? ", " : "", hw->nvme_temp[i]);
    printf("]}");

    printf(",\n  \"clocks\": {\"cpu_cur_mhz\": "); json_int_min(hw->cpu_mhz_cur, 1);
    printf(", \"cpu_limit_mhz\": "); json_int_min(hw->cpu_mhz_limit, 1);
    printf(", \"cpu_max_mhz\": "); json_int_min(hw->cpu_mhz_max, 1);
    printf(", \"gpu_mhz\": "); json_int_min(hw->gpu_mhz, 0);
    printf(", \"gpu_lock_mhz\": "); json_int_min(hw->gpu_clock_lock, 1);
    printf(", \"gpu_max_mhz\": "); json_int_min(hw->gpu_mhz_max, 1);
    printf(", \"igpu_mhz\": "); json_int_min(hw->igpu_mhz, 0);
    printf(", \"igpu_dpm\": "); json_int_min(hw->igpu_dpm, 0);
    printf(", \"igpu_dpm_n\": "); json_int_min(hw->igpu_dpm_n, 1);
    printf(", \"igpu_dpm_mhz\": "); json_int_min(hw->igpu_dpm_mhz, 1);
    printf("}");

    printf(",\n  \"gpu\": {\"power_w\": ");
    if (hw->gpu_mw >= 0) printf("%.3f", hw->gpu_mw / 1000.0); else fputs("null", stdout);
    printf(", \"util_pct\": "); json_int_min(hw->gpu_util, 0);
    printf(", \"vram_used_mb\": "); json_int_min(hw->gpu_vram_used_mb, 0);
    printf(", \"vram_total_mb\": "); json_int_min(hw->gpu_vram_total_mb, 1);
    printf(", \"pstate\": "); json_str(hw->gpu_pstate[0] ? hw->gpu_pstate : NULL);
    printf(", \"throttle\": ");
    if (hw->gpu_pstate[0]) {
        char th[128];
        hw_gpu_throttle_str(hw->gpu_throttle, th, sizeof th);
        json_str(th);
    } else {
        fputs("null", stdout);
    }
    printf("}");

    printf(",\n  \"igpu\": {\"busy_pct\": "); json_int_min(hw->igpu_busy, 0);
    printf(", \"power_w\": ");
    if (hw->igpu_mw >= 0) printf("%.3f", hw->igpu_mw / 1000.0); else fputs("null", stdout);
    printf(", \"vddgfx_mv\": "); json_int_min(hw->igpu_vddgfx_mv, 1);
    printf(", \"vddnb_mv\": "); json_int_min(hw->igpu_vddnb_mv, 1);
    printf("}");

    printf(",\n  \"fans\": {\"cpu_rpm\": "); json_int_min(hw->rpm_cpu, 0);
    printf(", \"gpu_rpm\": "); json_int_min(hw->rpm_gpu, 0);
    printf(", \"cpu_custom\": %s, \"gpu_custom\": %s}",
           hw->fan_cpu_on ? "true" : "false", hw->fan_gpu_on ? "true" : "false");

    printf(",\n  \"memory\": {\"used_mb\": "); json_int_min(hw->mem_used_mb, 0);
    printf(", \"total_mb\": "); json_int_min(hw->mem_total_mb, 1);
    printf(", \"swap_used_mb\": "); json_int_min(hw->swap_used_mb, 0);
    printf(", \"swap_total_mb\": "); json_int_min(hw->swap_total_mb, 0);
    printf(", \"ram_alarm\": %s}", hw->ram_alarm ? "true" : "false");

    printf(",\n  \"storage\": {\"root_used_mb\": "); json_int_min(hw->fs_used_mb, 0);
    printf(", \"root_total_mb\": "); json_int_min(hw->fs_total_mb, 1);
    printf("}");

    printf(",\n  \"network\": {\"interfaces\": [");
    for (int i = 0; i < hw->net_n; i++) {
        printf("%s{\"name\": ", i ? ", " : "");
        json_str(hw->net_if[i]);
        printf(", \"up\": %s, \"rx_bytes\": %lld, \"tx_bytes\": %lld}",
               hw->net_up[i] ? "true" : "false", hw->net_rx[i], hw->net_tx[i]);
    }
    printf("], \"rx_total_bytes\": "); json_ll(hw->net_rx_tot);
    printf(", \"tx_total_bytes\": "); json_ll(hw->net_tx_tot);
    printf(", \"wifi_if\": "); json_str(hw->wifi_if[0] ? hw->wifi_if : NULL);
    printf(", \"wifi_link_pct\": "); json_int_min(hw->wifi_link_pct, 0);
    printf(", \"wifi_dbm\": ");
    if (hw->wifi_level_dbm < 0) printf("%d", hw->wifi_level_dbm); else fputs("null", stdout);
    printf("}");

    printf(",\n  \"load\": {\"m1\": "); json_dbl(hw->load1, "%.2f");
    printf(", \"m5\": "); json_dbl(hw->load5, "%.2f");
    printf(", \"m15\": "); json_dbl(hw->load15, "%.2f");
    printf("}");

    printf(",\n  \"psi\": {\"cpu\": "); json_dbl(hw->psi_cpu, "%.2f");
    printf(", \"mem\": "); json_dbl(hw->psi_mem, "%.2f");
    printf(", \"io\": "); json_dbl(hw->psi_io, "%.2f");
    printf("}");

    printf(",\n  \"power\": {\"profile\": "); json_str(hw_profile_name(hw->profile));
    printf(", \"asusd_ac\": "); json_asusd_auto(hw->asusd_ac);
    printf(", \"asusd_bat\": "); json_asusd_auto(hw->asusd_bat);
    printf(", \"epp\": "); json_str(hw_epp_name(hw->epp));
    printf(", \"ppt_spl_w\": "); json_int_min(hw->ppt_spl, 1);
    printf(", \"ppt_sppt_w\": "); json_int_min(hw->ppt_sppt, 1);
    printf(", \"ppt_fppt_w\": "); json_int_min(hw->ppt_fppt, 1);
    printf(", \"ppt_off\": %s", hw->ppt_off ? "true" : "false");
    printf(", \"nv_boost_w\": "); json_int_min(hw->nv_boost, 1);
    printf(", \"nv_temp_c\": "); json_int_min(hw->nv_temp, 1);
    printf(", \"cpu_boost\": %s}", hw->cpu_boost ? "true" : "false");

    printf(",\n  \"battery\": {\"pct\": "); json_int_min(hw->bat_pct, 0);
    printf(", \"status\": "); json_str(hw->bat_status[0] ? hw->bat_status : NULL);
    printf(", \"ac_online\": %s", hw->ac_online ? "true" : "false");
    printf(", \"power_mw\": ");
    if (hw->bat_mw_known) printf("%d", hw->bat_mw); else fputs("null", stdout);
    printf(", \"limit_pct\": "); json_int_min(hw->bat_limit, 1);
    printf(", \"health_pct\": "); json_int_min(hw->bat_health, 0);
    printf(", \"pd_in_mw\": ");
    if (hw->pd_n > 0) printf("%d", hw->pd_in_mw); else fputs("null", stdout);
    printf("}");

    printf(",\n  \"display\": {\"hz\": "); json_int_min(hw->hz_cur, 1);
    printf(", \"backend\": "); json_str(d ? d->name : NULL);
    printf(", \"modes\": [");
    for (int i = 0; i < hw->hz_count; i++)
        printf("%s%d", i ? ", " : "", hw->hz_modes[i]);
    printf("], \"panel_od\": %s}", hw->panel_od ? "true" : "false");

    printf(",\n  \"keyboard\": {\"brightness\": "); json_str(hw_kbd_name(hw->kbd));
    printf("}\n}\n");
    return 0;
}

static volatile sig_atomic_t s_watch_run = 1;
static void watch_stop(int sig) { (void)sig; s_watch_run = 0; }

static int cmd_watch(hw_state_t *hw)
{
    struct sigaction sa = {0};
    sa.sa_handler = watch_stop;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("ctron watch — Ctrl-C stops\n");
    while (s_watch_run) {
        char tc[16], tg[16], hz[16], rc[16], rg[16], bp[16], pwr[24];
        hw_refresh_fast(hw);
        hw_fmt_power(pwr, sizeof(pwr), hw->bat_mw_known, hw->bat_mw);
        printf("\r %3s°C  GPU %3s°C  %4s MHz  fan %4s/%4s rpm  BAT %3s%% %-11s %s  %s   ",
               dash_if(tc, sizeof(tc), hw->cpu_temp),
               dash_if(tg, sizeof(tg), hw->gpu_temp),
               hw->cpu_mhz_cur > 0 ? dash_if(hz, sizeof(hz), hw->cpu_mhz_cur) : "--",
               dash_if(rc, sizeof(rc), hw->rpm_cpu),
               dash_if(rg, sizeof(rg), hw->rpm_gpu),
               dash_if(bp, sizeof(bp), hw->bat_pct),
               hw->bat_status[0] ? hw->bat_status : "--",
               pwr, hw_profile_name(hw->profile));
        usleep((useconds_t)g_prefs.poll_ms * 1000);
    }
    printf("\n");
    return 0;
}

/* --csv: append one row per poll tick for external graphing. Unknown
 * sensors are EMPTY fields (--), never fabricated values — the same
 * honest-reads rule as the TUI. Header written once per new file, so
 * consecutive runs append cleanly. */
static void csv_val(char *out, size_t n, int v)
{
    if (v < 0)
        out[0] = '\0';
    else
        snprintf(out, n, "%d", v);
}

static void csv_dval(char *out, size_t n, double v)
{
    if (v < 0)
        out[0] = '\0';
    else
        snprintf(out, n, "%.0f", v);
}

static int cmd_csv(hw_state_t *hw, const char *path)
{
    /* columns this build writes — the header and the row must agree */
    enum { CSV_COLS = 22 };
    bool new = ut_path_exists(path) == false;
    int hdr_cols = 0;
    if (!new) {
        /* appending to an old file: verify its header matches the
         * current column count, or rows would silently misalign */
        FILE *hf = fopen(path, "r");
        char hl[512];
        bool got = hf && fgets(hl, sizeof hl, hf);
        if (hf)
            fclose(hf);
        if (!got) {
            new = true; /* exists but unreadable/empty: write header */
        } else {
            hdr_cols = 1;
            for (const char *p = hl; *p; p++)
                if (*p == ',')
                    hdr_cols++;
            if (hdr_cols != CSV_COLS) {
                fprintf(stderr,
                        "ctron: %s has a %d-column header, this build "
                        "writes %d — start a new file\n",
                        path, hdr_cols, CSV_COLS);
                return 1;
            }
        }
    }
    FILE *f = fopen(path, "a");
    if (!f) {
        fprintf(stderr, "ctron: cannot write %s\n", path);
        return 1;
    }
    setvbuf(f, NULL, _IOLBF, 0);
    if (new)
        fprintf(f, "epoch,cpu_temp_c,igpu_temp_c,gpu_temp_c,cpu_mhz,gpu_mhz,"
                   "gpu_util_pct,gpu_mw,fan_cpu_rpm,fan_gpu_rpm,bat_pct,bat_mw,"
                   "load1,wifi_signal_pct,busiest_util_pct,apu_ppt_mw,"
                   "mem_used_mb,swap_used_mb,disk_rd_bs,disk_wr_bs,"
                   "net_rx_bs,net_tx_bs\n");

    struct sigaction sa = {0};
    sa.sa_handler = watch_stop;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    fprintf(stderr, "ctron csv — %s (%s) — Ctrl-C stops\n", path,
            new ? "new file" : "appending");
    /* rate counters: the same delta rule as the TUI sampler — first
     * tick has no baseline and stays empty, never a spike */
    long long prev_rd = -1, prev_wr = -1, prev_rx = -1, prev_tx = -1;
    long prev_ms = -1;
    while (s_watch_run) {
        char t[16], ti[16], tg[16], mc[16], mg[16], gu[16], gw[16],
             fc[16], fg[16], bp[16], bm[16], l1[16], wl[16], bu[16],
             ap[16], mu[16], su[16], rd[20], wr[20], rx[20], tx[20];
        hw_refresh_fast(hw);
        long now_ms = ut_mono_ms();
        double rd_bs = -1, wr_bs = -1, rx_bs = -1, tx_bs = -1;
        if (prev_ms > 0 && now_ms > prev_ms) {
            double dt = (double)(now_ms - prev_ms) / 1000.0;
            long long d;
            if (hw->disk_rd_sectors >= 0 && prev_rd >= 0) {
                d = hw->disk_rd_sectors - prev_rd;
                if (d >= 0)
                    rd_bs = (double)d * 512.0 / dt;
            }
            if (hw->disk_wr_sectors >= 0 && prev_wr >= 0) {
                d = hw->disk_wr_sectors - prev_wr;
                if (d >= 0)
                    wr_bs = (double)d * 512.0 / dt;
            }
            if (hw->net_rx_tot >= 0 && prev_rx >= 0) {
                d = hw->net_rx_tot - prev_rx;
                if (d >= 0)
                    rx_bs = (double)d / dt;
            }
            if (hw->net_tx_tot >= 0 && prev_tx >= 0) {
                d = hw->net_tx_tot - prev_tx;
                if (d >= 0)
                    tx_bs = (double)d / dt;
            }
        }
        prev_rd = hw->disk_rd_sectors;
        prev_wr = hw->disk_wr_sectors;
        prev_rx = hw->net_rx_tot;
        prev_tx = hw->net_tx_tot;
        prev_ms = now_ms;

        csv_val(t, sizeof t, hw->cpu_temp);
        csv_val(ti, sizeof ti, hw->igpu_temp);
        csv_val(tg, sizeof tg, hw->gpu_temp);
        csv_val(mc, sizeof mc, hw->cpu_mhz_cur);
        csv_val(mg, sizeof mg, hw->gpu_mhz);
        csv_val(gu, sizeof gu, hw->gpu_util);
        csv_val(gw, sizeof gw, hw->gpu_mw);
        csv_val(fc, sizeof fc, hw->rpm_cpu);
        csv_val(fg, sizeof fg, hw->rpm_gpu);
        csv_val(bp, sizeof bp, hw->bat_pct);
        csv_val(bm, sizeof bm, hw->bat_mw_known ? hw->bat_mw : -1);
        if (hw->load1 >= 0)
            snprintf(l1, sizeof l1, "%.2f", hw->load1);
        else
            l1[0] = '\0';
        csv_val(wl, sizeof wl, hw->wifi_link_pct);
        csv_val(bu, sizeof bu, hw->busiest_util);
        csv_val(ap, sizeof ap, hw->igpu_mw);
        csv_val(mu, sizeof mu, hw->mem_used_mb);
        csv_val(su, sizeof su, hw->swap_used_mb);
        csv_dval(rd, sizeof rd, rd_bs);
        csv_dval(wr, sizeof wr, wr_bs);
        csv_dval(rx, sizeof rx, rx_bs);
        csv_dval(tx, sizeof tx, tx_bs);
        fprintf(f, "%ld,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,"
                   "%s,%s,%s,%s,%s,%s,%s\n",
                time(NULL), t, ti, tg, mc, mg, gu, gw, fc, fg, bp, bm, l1, wl,
                bu, ap, mu, su, rd, wr, rx, tx);
        usleep((useconds_t)g_prefs.poll_ms * 1000);
    }
    fclose(f);
    fprintf(stderr, "ctron csv — stopped\n");
    return 0;
}

static int cmd_doctor(hw_state_t *hw, bool as_json)
{
    char os[128] = "?", dir[400];
    ut_read_file("/etc/os-release", os, sizeof(os)); /* first line; fine */
    {
        FILE *f = fopen("/etc/os-release", "r");
        if (f) {
            char line[256];
            os[0] = '\0';
            while (fgets(line, sizeof(line), f)) {
                if (!strncmp(line, "PRETTY_NAME=", 12)) {
                    char *v = line + 12;
                    v += (*v == '"');
                    v[strcspn(v, "\"\n")] = '\0';
                    snprintf(os, sizeof(os), "%s", v);
                    break;
                }
            }
            fclose(f);
        }
    }
    settings_dir(dir, sizeof(dir));
    const display_ops_t *d = display_get();
    bool ppt_sysfs = ut_path_exists("/sys/devices/platform/asus-nb-wmi/ppt_pl1_spl");
    const char *ppt_iface = hw->ppt_mode == HW_PPT_ARMOURY ? "armoury"
                          : hw->ppt_mode == HW_PPT_RYZENADJ ? "ryzenadj"
                          : "legacy";
    bool sudo_ok = ut_exec_argv((const char *[]){ "sudo", "-n", "true", NULL },
                                NULL, 0) == 0;

    if (as_json) {
        printf("{\n  \"version\": "); json_str(VERSION);
        printf(",\n  \"os\": "); json_str(os[0] ? os : NULL);
        printf(",\n  \"model\": "); json_str(hw->model);
        printf(",\n  \"is_asus\": %s", hw->is_asus ? "true" : "false");
        printf(",\n  \"cpu\": "); json_str(hw->cpu);
        printf(",\n  \"capabilities\": {");
        printf("\"asusctl\": %s", hw->has_asusctl ? "true" : "false");
        printf(", \"armoury\": %s", hw->has_armoury ? "true" : "false");
        printf(", \"fan_curve\": %s", hw->has_fan_curve ? "true" : "false");
        printf(", \"fan_rpm\": %s", hw->has_fan_rpm ? "true" : "false");
        printf(", \"k10temp_c\": "); json_int_min(hw->cpu_temp, 0);
        printf(", \"nvidia_smi\": %s", hw->has_nvidia_smi ? "true" : "false");
        printf(", \"gpu_temp_c\": "); json_int_min(hw->gpu_temp, 0);
        printf(", \"gpu_clock_lock\": "); json_int_min(hw->gpu_clock_lock, 1);
        printf(", \"gpu_mhz_max\": "); json_int_min(hw->gpu_mhz_max, 1);
        printf(", \"kbd_led\": %s", hw->has_kbd_led ? "true" : "false");
        printf(", \"ppt_sysfs\": %s", ppt_sysfs ? "true" : "false");
        printf(", \"ppt_iface\": "); json_str(ppt_iface);
        printf(", \"display_backend\": "); json_str(d ? d->name : NULL);
        printf(", \"sudo_nopass\": %s}", sudo_ok ? "true" : "false");
        printf(",\n  \"config_dir\": "); json_str(dir);
        printf("\n}\n");
        return 0;
    }

    printf("ctron doctor\n");
    printf("  os            : %s\n", os[0] ? os : "?");
    printf("  machine       : %s (%s)\n", hw->model, hw->is_asus ? "ASUS" : "not ASUS");
    printf("  cpu           : %s\n", hw->cpu);
    printf("  asusctl       : %s\n", hw->has_asusctl ? "yes" : "no");
    printf("  asus-armoury  : %s\n", hw->has_armoury ? "yes" : "no");
    printf("  fan curve     : %s\n", hw->has_fan_curve ? "hwmon asus_custom_fan_curve" : "missing");
    printf("  fan rpm       : %s\n", hw->has_fan_rpm ? "hwmon asus" : "missing");
    {
        char t1[16], t2[16];
        printf("  k10temp       : %s °C\n", dash_if(t1, sizeof(t1), hw->cpu_temp));
        printf("  nvidia-smi    : %s (%s °C)\n", hw->has_nvidia_smi ? "yes" : "no",
               dash_if(t2, sizeof(t2), hw->gpu_temp));
        printf("  gpu clock lock: %s%s\n",
               hw->gpu_clock_lock > 0 ? "locked (this session)" : "driver default",
               hw->gpu_mhz_max > 0 ? "" : " (max unknown)");
        if (hw->gpu_mhz_max > 0)
            printf("  gpu clock max : %d MHz (lock: --gpu-clock <mhz>|reset)\n",
                   hw->gpu_mhz_max);
    }
    printf("  kbd led       : %s\n", hw->has_kbd_led ? "asus::kbd_backlight" : "missing");
    printf("  ppt sysfs     : %s\n", ppt_sysfs ? "yes" : "no");
    printf("  ppt iface     : %s\n",
           hw->ppt_mode == HW_PPT_ARMOURY ? "armoury firmware-attributes"
           : hw->ppt_mode == HW_PPT_RYZENADJ ? "ryzenadj (sudo -n; no read-back)"
           : "legacy nb-wmi (unverifiable)");
    printf("  display       : %s\n", d ? d->name : "no backend detected");
    printf("  sudo -n       : %s\n", sudo_ok ? "passwordless" : "unavailable");
    printf("  config        : %s\n", dir);
    return 0;
}

/* ---- mode / profile subcommands ------------------------------------------ */

static int cmd_mode(int argc, char **argv, hw_state_t *hw)
{
    (void)hw; /* modes are applied through --mode, not here */
    if (argc < 3) {
        fprintf(stderr, "usage: ctron mode list|show <name>|add <name> <steps>|delete <name>\n");
        return 2;
    }
    const char *sub = argv[2];

    mode_def_t modes[MODES_MAX];
    int n = modes_load(modes, MODES_MAX);

    if (!strcmp(sub, "list")) {
        for (int i = 0; i < n; i++)
            printf("%-12s %s\n", modes[i].name, modes[i].steps);
        return 0;
    }
    if (!strcmp(sub, "show") && argc > 3) {
        int i = mode_find(modes, n, argv[3]);
        if (i < 0) {
            fprintf(stderr, "no such mode: %s\n", argv[3]);
            return 1;
        }
        printf("[%s]\nsteps = %s\n", modes[i].name, modes[i].steps);
        return 0;
    }
    if (!strcmp(sub, "add") && argc > 4) {
        if (!mode_name_ok(argv[3])) {
            fprintf(stderr, "ctron: bad mode name '%s' (empty, [ ] or newline)\n", argv[3]);
            return 1;
        }
        if (mode_find(modes, n, argv[3]) >= 0) {
            fprintf(stderr, "mode exists: %s (delete it first)\n", argv[3]);
            return 1;
        }
        if (n >= MODES_MAX) {
            fprintf(stderr, "ctron: mode table full (max %d)\n", MODES_MAX);
            return 1;
        }
        char steps[MODE_STEPS_MAX] = {0};
        for (int i = 4; i < argc; i++) {
            strncat(steps, argv[i], sizeof(steps) - strlen(steps) - 2);
            if (i + 1 < argc)
                strncat(steps, " ", sizeof(steps) - strlen(steps) - 2);
        }
        /* a newline in steps would inject fresh modes.ini lines (bogus
         * sections / a rewritten steps key) on the next save — same
         * round-trip class mode_name_ok guards the name against */
        if (!mode_steps_ok(steps)) {
            fprintf(stderr, "ctron: bad mode steps (newline not allowed)\n");
            return 1;
        }
        snprintf(modes[n].name, MODE_NAME_MAX, "%s", argv[3]);
        snprintf(modes[n].steps, MODE_STEPS_MAX, "%s", steps);
        n++;
        modes_save(modes, n);
        printf("added mode '%s'\n", argv[3]);
        return 0;
    }
    if (!strcmp(sub, "delete") && argc > 3) {
        int i = mode_find(modes, n, argv[3]);
        if (i < 0) {
            fprintf(stderr, "no such mode: %s\n", argv[3]);
            return 1;
        }
        for (; i + 1 < n; i++)
            modes[i] = modes[i + 1];
        n--;
        modes_save(modes, n);
        printf("deleted mode '%s'\n", argv[3]);
        return 0;
    }
    fprintf(stderr, "unknown mode subcommand\n");
    return 2;
}

static int cmd_profile(int argc, char **argv, hw_state_t *hw, bool dry_run)
{
    if (argc < 3) {
        fprintf(stderr, "usage: ctron snapshot list|apply|diff|export|delete <name> (alias: profile)\n");
        return 2;
    }
    const char *sub = argv[2];

    if (!strcmp(sub, "list")) {
        char list[MAX_PROFILES][PROFILE_NAME_MAX];
        int n = profile_list(list, MAX_PROFILES);
        for (int i = 0; i < n; i++)
            printf("%s\n", list[i]);
        return 0;
    }
    if (argc < 4) {
        fprintf(stderr, "snapshot %s needs a name\n", sub);
        return 2;
    }
    const char *name = argv[3];

    if (!strcmp(sub, "diff")) {
        hw_refresh_live(hw);
        profile_meta_t m;
        if (profile_meta_parse(name, &m) != 0) {
            fprintf(stderr, "ctron: no such snapshot: %s\n", name);
            return 1;
        }
        char diff[192];
        profile_meta_diff(hw, &m, diff, sizeof(diff));
        printf("%s: %s\n", name, diff);
        return 0;
    }
    if (!strcmp(sub, "apply")) {
        char err[128];
        if (dry_run) {
            int rc = profile_dry_run(name, hw, stdout, err, sizeof(err));
            if (rc != 0)
                fprintf(stderr, "ctron: %s\n", err[0] ? err : "dry-run failed");
            return rc == 0 ? 0 : 1;
        }
        ut_priv_denied_clear();
        hw->fan_staged = false;
        int rc = profile_import(name, hw, err, sizeof(err));
        if (rc == 0 && hw->fan_staged) {
            if (ctrl_fan_write(hw) != 0) {
                rc = -1;
                snprintf(err, sizeof(err), "fan curve write failed");
            }
        }
        if (rc != 0)
            fprintf(stderr, "%s\n", err[0] ? err : "apply failed");
        if (rc == 0)
            return 0;
        return ut_priv_denied() ? 3 : 1;
    }
    if (!strcmp(sub, "export")) {
        hw_refresh_live(hw);
        return profile_export(name, hw) == 0 ? 0 : 1;
    }
    if (!strcmp(sub, "delete"))
        return profile_delete(name) == 0 ? 0 : 1;

    fprintf(stderr, "unknown snapshot subcommand: %s\n", sub);
    return 2;
}

/* ---- flag → cmd_run bridge ------------------------------------------------ */

static int run_flag(hw_state_t *hw, const char *key, const char *val, bool dry)
{
    char err[192];
    int rc = dry ? cmd_validate(hw, key, val, err, sizeof(err))
                 : cmd_run(hw, key, val, err, sizeof(err));
    if (rc != 0) {
        fprintf(stderr, "ctron: %s\n", err[0] ? err : "command failed");
    } else if (dry) {
        if (val && val[0])
            printf("dry-run: %s = %s\n", key, val);
        else
            printf("dry-run: %s\n", key);
    }
    return rc;
}

static int follow_fire(const char *key, void *ud)
{
    hw_state_t *hw = ud;
    char path[512];
    char text[8192];
    char cmd[64];
    char val[128];
    FILE *f;
    size_t n;

    db_binds_path(path, (int)sizeof path);
    f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "ctron: no %s\n", path);
        return 0;
    }
    n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    if (db_action_in(text, key, cmd, (int)sizeof cmd, val, (int)sizeof val) != 0)
        return 0;
    fprintf(stderr, "ctron: fire %s -> %s %s\n", key, cmd, val);
    run_flag(hw, cmd, val, false);
    return 0;
}

/* ---- main ------------------------------------------------------------------ */

static int run_tui(void)
{
    /* one fullscreen TUI at a time: a second instance (e.g. a stale key
     * binding that spawns `konsole -e ctron` on every press) bows out
     * instead of stacking windows */
    int other = ut_singleton_enter("ctron");
    if (other > 0) {
        fprintf(stderr,
                "ctron: TUI already running (pid %d) — closing this window\n",
                other);
        sleep(2); /* keep the note readable in the flash window */
        return 0;
    }
    hw_state_t hw;
    hw_init(&hw);
    settings_load(&hw);
    int rc = ui_run(&hw);
    ut_singleton_leave();
    return rc == 0 ? 0 : 1;
}

int main(int argc, char *argv[])
{
    /* --config-dir DIR, --json, --dry-run (remove themselves from argv) */
    bool opt_json = false, opt_dry_run = false;
    int w = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--config-dir") && i + 1 < argc) {
            setenv("CTRON_CONFIG", argv[++i], 1);
            continue;
        }
        if (!strncmp(argv[i], "--config-dir=", 13)) {
            setenv("CTRON_CONFIG", argv[i] + 13, 1);
            continue;
        }
        if (!strcmp(argv[i], "--json")) {
            opt_json = true;
            continue;
        }
        if (!strcmp(argv[i], "--dry-run")) {
            opt_dry_run = true;
            continue;
        }
        argv[w++] = argv[i];
    }
    argv[w] = NULL;
    argc = w;

    if (argc > 1 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        print_usage(argv[0]);
        return 0;
    }
    if (argc > 1 && (!strcmp(argv[1], "--version") || !strcmp(argv[1], "-V"))) {
        print_version();
        return 0;
    }

    if (argc == 1) {
        if (opt_json || opt_dry_run) {
            fprintf(stderr, "ctron: %s requires a command (e.g. --status --json, --mode <name> --dry-run)\n",
                    opt_json ? "--json" : "--dry-run");
            return 2;
        }
        /* no args: fullscreen TUI when interactive, help otherwise */
        if (isatty(0) && isatty(1))
            return run_tui();
        print_version();
        printf("No terminal attached — printing help instead of the TUI.\n\n");
        print_usage(argv[0]);
        return 2;
    }

    if (!strcmp(argv[1], "--tui") || !strcmp(argv[1], "-t")) {
        if (!isatty(0) || !isatty(1)) {
            fprintf(stderr, "ctron: --tui needs a terminal\n");
            return 1;
        }
        return run_tui();
    }

    hw_state_t hw;
    hw_init(&hw);
    settings_load(&hw);

    /* commands that never touch hardware state */
    if (!strcmp(argv[1], "--doctor")) {
        hw_refresh_live(&hw);
        return cmd_doctor(&hw, opt_json);
    }
    if (!strcmp(argv[1], "--mode") && argc > 2) {
        /* documented in the help text but never wired: the generic
         * flag path would call cmd_run("mode") which is not a key */
        mode_def_t modes[MODES_MAX];
        int n = modes_load(modes, MODES_MAX);
        int i = mode_find(modes, n, argv[2]);
        if (i < 0) {
            fprintf(stderr, "ctron: no such mode: %s (ctron mode list)\n", argv[2]);
            return 1;
        }
        char err[128];
        if (opt_dry_run) {
            int rc = mode_dry_run(&hw, modes[i].steps, stdout, err, sizeof(err));
            if (rc != 0)
                fprintf(stderr, "ctron: mode '%s' invalid: %s\n", argv[2],
                        err[0] ? err : "?");
            return rc == 0 ? 0 : 1;
        }
        ut_priv_denied_clear();
        long a = ut_mono_ms();
        int rc = mode_apply(&hw, modes[i].steps, err, sizeof(err));
        double took = (double)(ut_mono_ms() - a) / 1000.0;
        if (rc == 0)
            printf("mode '%s' applied (%.1f s)\n", argv[2], took);
        else
            fprintf(stderr, "ctron: mode '%s' failed: %s\n", argv[2],
                    err[0] ? err : "?");
        if (rc == 0)
            return 0;
        return ut_priv_denied() ? 3 : 1;
    }
    if (!strcmp(argv[1], "--setup")) {
        char dir[400];
        settings_dir(dir, sizeof(dir));
        printf("ctron config lives in %s\n", dir);
        printf("edit %s/modes.ini to manage shortcut bundles;\n", dir);
        printf("snapshots live in %s/profiles/*.ctr (group: name/sub)\n", dir);
        return 0;
    }
    if (!strcmp(argv[1], "mode"))
        return cmd_mode(argc, argv, &hw);
    if (!strcmp(argv[1], "snapshot") || !strcmp(argv[1], "profile"))
        return cmd_profile(argc, argv, &hw, opt_dry_run);

    /* status / watch need a live snapshot */
    if (!strcmp(argv[1], "--follow")) {
        return db_follow(follow_fire, &hw);
    }
    if (!strcmp(argv[1], "--daeboard-start")) {
        ut_priv_denied_clear();
        if (db_start() == 0)
            return 0;
        return ut_priv_denied() ? 3 : 1;
    }
    if (!strcmp(argv[1], "--daeboard-stop")) {
        return db_quit() == 0 ? 0 : 1;
    }
    if (!strcmp(argv[1], "--daeboard-reload")) {
        char err[64];
        if (db_reload(err, (int)sizeof err) == 0)
            return 0;
        fprintf(stderr, "ctron: daeboard %s\n", err[0] ? err : "reload failed");
        return 1;
    }
    if (!strcmp(argv[1], "--status") || !strcmp(argv[1], "-s")) {
        hw_refresh_live(&hw);
        return opt_json ? cmd_status_json(&hw) : cmd_status(&hw);
    }
    if (!strcmp(argv[1], "--watch") || !strcmp(argv[1], "-w")) {
        hw_refresh_fast(&hw);
        return cmd_watch(&hw);
    }
    if (!strcmp(argv[1], "--csv") && argc > 2) {
        hw_refresh_fast(&hw);
        return cmd_csv(&hw, argv[2]);
    }

    /* everything else: hardware flags applied in order */
    ut_priv_denied_clear();
    int rc = 0, ran = 0;
    for (int i = 1; i < argc; i++) {
        const char *flag = argv[i];
        if (flag[0] != '-' || flag[1] != '-') {
            fprintf(stderr, "ctron: unexpected argument '%s'\n", flag);
            return 2;
        }
        /* --flag=value */
        char key[64] = {0};
        const char *val = NULL;
        const char *eq = strchr(flag + 2, '=');
        if (eq) {
            size_t kn = (size_t)(eq - (flag + 2));
            if (kn >= sizeof(key))
                kn = sizeof(key) - 1;
            memcpy(key, flag + 2, kn);
            val = eq + 1;
        } else {
            snprintf(key, sizeof(key), "%s", flag + 2);
        }

        /* valueless flags */
        if (!strcmp(key, "fan-write") || !strcmp(key, "battery-oneshot")) {
            if (run_flag(&hw, key, "", opt_dry_run) != 0)
                rc = 1;
            else
                ran++;
            continue;
        }

        /* --freq core N M: absorb the two extra tokens when the value
         * starts with "core" (--fan-curve pattern) */
        if (!val && !strcmp(key, "freq") && i + 1 < argc &&
            (!strcmp(argv[i + 1], "core") || !strcmp(argv[i + 1], "ccd"))) {
            char joined[64] = {0};
            int got = 0;
            while (got < 3 && i + 1 < argc && argv[i + 1][0] != '-') {
                if (got)
                    strncat(joined, " ", sizeof(joined) - strlen(joined) - 1);
                strncat(joined, argv[++i], sizeof(joined) - strlen(joined) - 1);
                got++;
            }
            if (run_flag(&hw, key, joined, opt_dry_run) != 0)
                rc = 1;
            else
                ran++;
            continue;
        }

        /* multi-token flags: --fan-curve cpu T P (3 tokens after the flag),
         * --aura effect [color|hex] (1-2 tokens) */
        /* --fan cpu on  /  --fan gpu off  (two tokens; presets stay one) */
        if (!val && !strcmp(key, "fan") && i + 2 < argc &&
            (!strcmp(argv[i + 1], "cpu") || !strcmp(argv[i + 1], "gpu")) &&
            argv[i + 2][0] != '-') {
            char joined[32];
            snprintf(joined, sizeof(joined), "%s %s", argv[i + 1], argv[i + 2]);
            i += 2;
            if (run_flag(&hw, key, joined, opt_dry_run) != 0)
                rc = 1;
            else
                ran++;
            continue;
        }

        if (!val && (!strcmp(key, "fan-curve") || !strcmp(key, "aura"))) {
            int want = !strcmp(key, "fan-curve") ? 3 : 1;
            char joined[256] = {0};
            int got = 0;
            while (got < want && i + 1 < argc && argv[i + 1][0] != '-') {
                if (got)
                    strncat(joined, " ", sizeof(joined) - strlen(joined) - 1);
                strncat(joined, argv[++i], sizeof(joined) - strlen(joined) - 1);
                got++;
            }
            /* --aura with a color/hex right after "effect" already grabbed
             * one token; try to also absorb a color-looking second token */
            if (want == 1 && i + 1 < argc && argv[i + 1][0] != '-') {
                strncat(joined, " ", sizeof(joined) - strlen(joined) - 1);
                strncat(joined, argv[++i], sizeof(joined) - strlen(joined) - 1);
            }
            if (got == 0) {
                fprintf(stderr, "ctron: %s needs a value\n", flag);
                return 2;
            }
            if (run_flag(&hw, key, joined, opt_dry_run) != 0)
                rc = 1;
            else
                ran++;
            continue;
        }

        if (!val) {
            if (i + 1 >= argc) {
                fprintf(stderr, "ctron: %s needs a value\n", flag);
                return 2;
            }
            val = argv[++i];
        }
        if (run_flag(&hw, key, val, opt_dry_run) != 0)
            rc = 1;
        else
            ran++;
    }

    if (!opt_dry_run && rc == 0 && ran > 0)
        settings_save(&hw);
    if (rc != 0 && ut_priv_denied())
        return 3;
    return rc;
}
