/* pymergetic.metal.console — one ring, N viewports. Seat fill is the sink. */
#include "pymergetic/metal/console/__exports__.h"
#include "pymergetic/metal/console/__types__.h"

#include "pymergetic/util/mem.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(PM_METAL_FIRMWARE)
void uart_write(const char *s, size_t n);
#else
#include <stdio.h>
#if !defined(__EMSCRIPTEN__)
#include <unistd.h>
#endif
#endif

#ifndef PM_METAL_CONSOLE_LINES
#define PM_METAL_CONSOLE_LINES 64u
#endif
#ifndef PM_METAL_CONSOLE_COLS
#define PM_METAL_CONSOLE_COLS 160u
#endif
#ifndef PM_METAL_CONSOLE_VP_MAX
#define PM_METAL_CONSOLE_VP_MAX 4u
#endif

typedef struct {
    const char *kind;
    pm_metal_console_sink_fn sink;
} pm_metal_console_vp_t;

static int s_ready;
static uint32_t s_head;
static uint32_t s_count;
static uint32_t s_col;
static char s_ring[PM_METAL_CONSOLE_LINES][PM_METAL_CONSOLE_COLS];
static char s_line[PM_METAL_CONSOLE_COLS];
static pm_metal_console_vp_t s_vp[PM_METAL_CONSOLE_VP_MAX];
static uint32_t s_nvp;

static void fill_write(const char *s, uint32_t n) {
    if (s == NULL || n == 0) {
        return;
    }
#if defined(PM_METAL_FIRMWARE)
    uart_write(s, (size_t)n);
#elif defined(__EMSCRIPTEN__)
    {
        size_t nw = fwrite(s, 1, (size_t)n, stdout);
        (void)nw;
        (void)fflush(stdout);
    }
#else
    {
        ssize_t nw = write(1, s, (size_t)n);
        (void)nw;
    }
#endif
}

static const char *fill_kind(void) {
#if defined(__EMSCRIPTEN__)
    return "panel";
#elif defined(PM_METAL_FIRMWARE)
    return "serial uart";
#else
    return "posix stdout";
#endif
}

static void fanout(const char *s, uint32_t n) {
    uint32_t i;
    if (s == NULL || n == 0) {
        return;
    }
    if (s_nvp == 0) {
        fill_write(s, n);
        return;
    }
    for (i = 0; i < s_nvp; i++) {
        if (s_vp[i].sink != NULL) {
            s_vp[i].sink(s, n);
        }
    }
}

static void commit_line(void) {
    uint32_t i;
    s_line[s_col < PM_METAL_CONSOLE_COLS ? s_col : (PM_METAL_CONSOLE_COLS - 1u)] = 0;
    i = s_head;
    memcpy(s_ring[i], s_line, PM_METAL_CONSOLE_COLS);
    s_head = (s_head + 1u) % PM_METAL_CONSOLE_LINES;
    if (s_count < PM_METAL_CONSOLE_LINES) {
        s_count++;
    }
    s_col = 0;
    s_line[0] = 0;
}

static void replay_one(pm_metal_console_sink_fn sink) {
    uint32_t i;
    uint32_t start;
    if (sink == NULL) {
        return;
    }
    start = (s_count == PM_METAL_CONSOLE_LINES) ? s_head : 0u;
    for (i = 0; i < s_count; i++) {
        const char *line = s_ring[(start + i) % PM_METAL_CONSOLE_LINES];
        uint32_t n = 0;
        while (line[n] != 0) {
            n++;
        }
        sink(line, n);
        sink("\n", 1);
    }
    if (s_col > 0u) {
        sink(s_line, s_col);
    }
}

static int32_t pm_metal_console_init(pm_util_mem_arena_t *arena) {
    (void)arena;
    memset(s_ring, 0, sizeof(s_ring));
    memset(s_line, 0, sizeof(s_line));
    memset(s_vp, 0, sizeof(s_vp));
    s_head = 0;
    s_count = 0;
    s_col = 0;
    s_nvp = 0;
    s_ready = 0;
    if (pm_metal_console_viewport_attach(fill_kind(), fill_write) != 0) {
        return -1;
    }
    s_ready = 1;
    return 0;
}

static void pm_metal_console_deinit(void) {
    s_ready = 0;
    s_nvp = 0;
    s_count = 0;
    s_col = 0;
}

int32_t pm_metal_console_ready(void) {
    return s_ready;
}

uint32_t pm_metal_console_id(void) {
    return 0;
}

uint32_t pm_metal_console_viewport_count(void) {
    return s_nvp;
}

const char *pm_metal_console_viewport_kind(uint32_t i) {
    if (i >= s_nvp) {
        return NULL;
    }
    return s_vp[i].kind;
}

int32_t pm_metal_console_viewport_attach(const char *kind, pm_metal_console_sink_fn sink) {
    if (kind == NULL || kind[0] == 0 || sink == NULL) {
        return -1;
    }
    if (s_nvp >= PM_METAL_CONSOLE_VP_MAX) {
        return -1;
    }
    s_vp[s_nvp].kind = kind;
    s_vp[s_nvp].sink = sink;
    s_nvp++;
    replay_one(sink);
    return 0;
}

int32_t pm_metal_console_write(const char *s, uint32_t n) {
    uint32_t i;
    if (s == NULL || n == 0) {
        return 0;
    }
    fanout(s, n);
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            commit_line();
            continue;
        }
        if (s_col + 1u < PM_METAL_CONSOLE_COLS) {
            s_line[s_col++] = c;
        }
    }
    return 0;
}

uint32_t pm_metal_console_line_count(void) {
    return s_count;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_ready, pm_metal_console_ready, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_id, pm_metal_console_id, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_viewport_count, pm_metal_console_viewport_count, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_viewport_kind, pm_metal_console_viewport_kind, const char *(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_write, pm_metal_console_write, int32_t(const char *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_line_count, pm_metal_console_line_count, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_viewport_attach, pm_metal_console_viewport_attach, int32_t(const char *, pm_metal_console_sink_fn));

PM_MOD_BOOT_C(pymergetic.metal.console, pm_metal_console_init, pm_metal_console_deinit);
