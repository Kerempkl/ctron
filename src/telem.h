#ifndef CTRON_TELEM_H
#define CTRON_TELEM_H

#include <stdbool.h>
#include <stddef.h>

/* Pure telemetry sample rings — no notcurses, no hardware. The UI
 * samples into these after each fast poll; the fullscreen TELEMETRY
 * view renders them. Adding a metric = one row in TELEM_SERIES (the
 * enum and the definition table both come from that single list)
 * plus one extractor line in the sampler. */

#define TELEM_N 240 /* samples per ring; 250 ms gate ≈ 60 s window */
#define TELEM_AUTO 0.0 /* range sentinel: scale to the window min/max */

#define TELEM_SERIES(X)                                                    \
    X(TS_CPU_TEMP, "cpu_temp", 0, 100)                                     \
    X(TS_CPU_MHZ, "cpu_mhz", TELEM_AUTO, TELEM_AUTO)                       \
    X(TS_CPU_LOAD, "cpu_load", TELEM_AUTO, TELEM_AUTO)                     \
    X(TS_GPU_TEMP, "gpu_temp", 0, 100)                                     \
    X(TS_GPU_MHZ, "gpu_mhz", TELEM_AUTO, TELEM_AUTO)                       \
    X(TS_GPU_MW, "gpu_mw", TELEM_AUTO, TELEM_AUTO)                         \
    X(TS_GPU_UTIL, "gpu_util", 0, 100)                                     \
    X(TS_IGPU_TEMP, "igpu_temp", 0, 100)                                   \
    X(TS_IGPU_MHZ, "igpu_mhz", TELEM_AUTO, TELEM_AUTO)                     \
    X(TS_IGPU_MW, "igpu_mw", TELEM_AUTO, TELEM_AUTO)                       \
    X(TS_FAN_CPU, "fan_cpu_rpm", TELEM_AUTO, TELEM_AUTO)                   \
    X(TS_FAN_GPU, "fan_gpu_rpm", TELEM_AUTO, TELEM_AUTO)                   \
    X(TS_MEM_USED, "mem_used_mb", TELEM_AUTO, TELEM_AUTO)                  \
    X(TS_DISK_RD, "disk_rd_bs", TELEM_AUTO, TELEM_AUTO)                    \
    X(TS_DISK_WR, "disk_wr_bs", TELEM_AUTO, TELEM_AUTO)                    \
    X(TS_BAT_MW, "bat_mw", TELEM_AUTO, TELEM_AUTO)                         \
    X(TS_BAT_PCT, "bat_pct", 0, 100)                                       \
    X(TS_PD_MW, "pd_in_mw", TELEM_AUTO, TELEM_AUTO)

typedef enum {
#define TELEM_ENUM(id, key, mn, mx) id,
    TELEM_SERIES(TELEM_ENUM)
    TS_COUNT
#undef TELEM_ENUM
} telem_series_t;

const char *telem_key(int idx);
/* Definition range — AUTO sentinels passed through as-is. */
void telem_range(int idx, double *mn, double *mx);

void telem_reset(void);
/* Gate: true when a new sample slot opens (>= 250 ms since the last
 * accepted tick). The caller then telem_push()es every series once. */
int telem_tick(long mono_ms);
/* Append one sample. v < 0 means "unknown" and is skipped — a series
 * without a value this tick simply does not advance. */
void telem_push(int idx, double v);

double telem_last(int idx); /* last accepted value, -1 when empty */
int telem_count(int idx);   /* samples stored */
/* Sample `back` accepted ticks ago (0 = newest). False when the index
 * or offset is outside the window; *v then stays -1. The detail charts
 * walk the ring through this — the rings keep no second copy of data. */
bool telem_at(int idx, int back, double *v);
void telem_minmax(int idx, double *mn, double *mx); /* stored window */

/* UTF-8 block sparkline (▁▂▃▄▅▆▇█), oldest → newest, last `cells`
 * samples, scaled to the def range (clipped at the edges) or to the
 * window min/max when AUTO; a flat AUTO window draws the mid block.
 * An empty series draws "--". */
void telem_spark(int idx, char *out, size_t n, int cells);

#endif
