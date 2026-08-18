/* pymergetic.metal.console — N text rings (F1–F6). UART stays on #0. */
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

#ifndef PM_METAL_CONSOLE_N
#define PM_METAL_CONSOLE_N 6u
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

typedef struct {
    uint32_t head;
    uint32_t count;
    uint32_t col;
    char ring[PM_METAL_CONSOLE_LINES][PM_METAL_CONSOLE_COLS];
    char line[PM_METAL_CONSOLE_COLS];
    pm_metal_console_vp_t vp[PM_METAL_CONSOLE_VP_MAX];
    uint32_t nvp;
} pm_metal_console_t;

static int s_ready;
static uint32_t s_focus;
static pm_metal_console_t s_c[PM_METAL_CONSOLE_N];

static int kind_eq(const char *a, const char *b) {
    uint32_t i;
    if (a == NULL || b == NULL) {
        return 0;
    }
    for (i = 0; a[i] != 0 && b[i] != 0; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return a[i] == 0 && b[i] == 0;
}

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

static void fanout(pm_metal_console_t *c, const char *s, uint32_t n) {
    uint32_t i;
    if (c == NULL || s == NULL || n == 0) {
        return;
    }
    if (c->nvp == 0) {
        return;
    }
    for (i = 0; i < c->nvp; i++) {
        if (c->vp[i].sink != NULL) {
            c->vp[i].sink(s, n);
        }
    }
}

static void commit_line(pm_metal_console_t *c) {
    uint32_t i;
    c->line[c->col < PM_METAL_CONSOLE_COLS ? c->col : (PM_METAL_CONSOLE_COLS - 1u)] = 0;
    i = c->head;
    memcpy(c->ring[i], c->line, PM_METAL_CONSOLE_COLS);
    c->head = (c->head + 1u) % PM_METAL_CONSOLE_LINES;
    if (c->count < PM_METAL_CONSOLE_LINES) {
        c->count++;
    }
    c->col = 0;
    c->line[0] = 0;
}

static void replay_one(const pm_metal_console_t *c, pm_metal_console_sink_fn sink) {
    uint32_t i;
    uint32_t start;
    if (c == NULL || sink == NULL) {
        return;
    }
    start = (c->count == PM_METAL_CONSOLE_LINES) ? c->head : 0u;
    for (i = 0; i < c->count; i++) {
        const char *line = c->ring[(start + i) % PM_METAL_CONSOLE_LINES];
        uint32_t n = 0;
        while (line[n] != 0) {
            n++;
        }
        sink(line, n);
        sink("\n", 1);
    }
    if (c->col > 0u) {
        sink(c->line, c->col);
    }
}

static int32_t attach_id(uint32_t id, const char *kind, pm_metal_console_sink_fn sink) {
    pm_metal_console_t *c;
    if (id >= PM_METAL_CONSOLE_N || kind == NULL || kind[0] == 0 || sink == NULL) {
        return -1;
    }
    c = &s_c[id];
    if (c->nvp >= PM_METAL_CONSOLE_VP_MAX) {
        return -1;
    }
    c->vp[c->nvp].kind = kind;
    c->vp[c->nvp].sink = sink;
    c->nvp++;
    replay_one(c, sink);
    return 0;
}

static void vp_remove_kind(pm_metal_console_t *c, const char *kind) {
    uint32_t i;
    uint32_t j = 0;
    if (c == NULL) {
        return;
    }
    for (i = 0; i < c->nvp; i++) {
        if (kind_eq(c->vp[i].kind, kind)) {
            continue;
        }
        c->vp[j++] = c->vp[i];
    }
    c->nvp = j;
}

static int32_t rebind_kind(const char *kind, uint32_t to_id) {
    uint32_t i;
    pm_metal_console_sink_fn sink = NULL;
    if (kind == NULL || to_id >= PM_METAL_CONSOLE_N) {
        return -1;
    }
    for (i = 0; i < PM_METAL_CONSOLE_N; i++) {
        uint32_t v;
        for (v = 0; v < s_c[i].nvp; v++) {
            if (kind_eq(s_c[i].vp[v].kind, kind)) {
                sink = s_c[i].vp[v].sink;
                vp_remove_kind(&s_c[i], kind);
                return attach_id(to_id, kind, sink);
            }
        }
    }
    return -1;
}

static int32_t write_id(uint32_t id, const char *s, uint32_t n) {
    pm_metal_console_t *c;
    uint32_t i;
    if (id >= PM_METAL_CONSOLE_N) {
        return -1;
    }
    if (s == NULL || n == 0) {
        return 0;
    }
    c = &s_c[id];
    if (!s_ready) {
        fill_write(s, n);
        return 0;
    }
    if (c->nvp != 0) {
        fanout(c, s, n);
    }
    for (i = 0; i < n; i++) {
        char ch = s[i];
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            commit_line(c);
            continue;
        }
        if (c->col + 1u < PM_METAL_CONSOLE_COLS) {
            c->line[c->col++] = ch;
        }
    }
    return 0;
}

static int32_t pm_metal_console_init(pm_util_mem_arena_t *arena) {
    (void)arena;
    memset(s_c, 0, sizeof(s_c));
    s_focus = 0;
    s_ready = 0;
    if (attach_id(0, fill_kind(), fill_write) != 0) {
        return -1;
    }
    s_ready = 1;
    return 0;
}

static void pm_metal_console_deinit(void) {
    s_ready = 0;
    s_focus = 0;
    memset(s_c, 0, sizeof(s_c));
}

int32_t pm_metal_console_ready(void) {
    return s_ready;
}

uint32_t pm_metal_console_count(void) {
    return PM_METAL_CONSOLE_N;
}

uint32_t pm_metal_console_id(void) {
    return s_focus;
}

int32_t pm_metal_console_select(int32_t id) {
    if (id < 0 || (uint32_t)id >= PM_METAL_CONSOLE_N) {
        return -1;
    }
    if (s_focus == (uint32_t)id) {
        return 0;
    }
    s_focus = (uint32_t)id;
    (void)rebind_kind("fb", s_focus);
    return 0;
}

uint32_t pm_metal_console_viewport_count_id(int32_t id) {
    if (id < 0 || (uint32_t)id >= PM_METAL_CONSOLE_N) {
        return 0;
    }
    return s_c[id].nvp;
}

uint32_t pm_metal_console_viewport_count(void) {
    return pm_metal_console_viewport_count_id(0);
}

const char *pm_metal_console_viewport_kind_id(int32_t id, uint32_t i) {
    if (id < 0 || (uint32_t)id >= PM_METAL_CONSOLE_N || i >= s_c[id].nvp) {
        return NULL;
    }
    return s_c[id].vp[i].kind;
}

const char *pm_metal_console_viewport_kind(uint32_t i) {
    return pm_metal_console_viewport_kind_id(0, i);
}

int32_t pm_metal_console_viewport_attach_id(int32_t id, const char *kind,
    pm_metal_console_sink_fn sink) {
    if (id < 0) {
        return -1;
    }
    return attach_id((uint32_t)id, kind, sink);
}

int32_t pm_metal_console_viewport_attach(const char *kind, pm_metal_console_sink_fn sink) {
    return attach_id(0, kind, sink);
}

int32_t pm_metal_console_write_id(int32_t id, const char *s, uint32_t n) {
    if (id < 0) {
        return -1;
    }
    return write_id((uint32_t)id, s, n);
}

int32_t pm_metal_console_write(const char *s, uint32_t n) {
    return write_id(0, s, n);
}

uint32_t pm_metal_console_line_count_id(int32_t id) {
    if (id < 0 || (uint32_t)id >= PM_METAL_CONSOLE_N) {
        return 0;
    }
    return s_c[id].count;
}

uint32_t pm_metal_console_line_count(void) {
    return s_c[0].count;
}

int32_t pm_metal_console_up(void) {
    uint32_t n0;
    if (!s_ready) {
        return -1;
    }
    n0 = s_c[0].count;
    if (pm_metal_console_select(1) != 0 || s_focus != 1u) {
        return -1;
    }
    if (write_id(1, "F2\n", 3) != 0 || s_c[1].count < 1u) {
        (void)pm_metal_console_select(0);
        return -1;
    }
    if (s_c[0].count != n0) {
        (void)pm_metal_console_select(0);
        return -1;
    }
    if (pm_metal_console_select(0) != 0 || s_focus != 0u) {
        return -1;
    }
    return 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_ready, pm_metal_console_ready, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_count, pm_metal_console_count, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_id, pm_metal_console_id, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_select, pm_metal_console_select, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_viewport_count, pm_metal_console_viewport_count, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_viewport_count_id, pm_metal_console_viewport_count_id, uint32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_viewport_kind, pm_metal_console_viewport_kind, const char *(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_viewport_kind_id, pm_metal_console_viewport_kind_id, const char *(int32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_write, pm_metal_console_write, int32_t(const char *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_write_id, pm_metal_console_write_id, int32_t(int32_t, const char *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_line_count, pm_metal_console_line_count, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_line_count_id, pm_metal_console_line_count_id, uint32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_viewport_attach, pm_metal_console_viewport_attach, int32_t(const char *, pm_metal_console_sink_fn));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_viewport_attach_id, pm_metal_console_viewport_attach_id, int32_t(int32_t, const char *, pm_metal_console_sink_fn));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_up, pm_metal_console_up, int32_t(void));

int32_t pm_metal_console_fb_attach(void);
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_fb_attach, pm_metal_console_fb_attach, int32_t(void));

PM_MOD_BOOT_C(pymergetic.metal.console, pm_metal_console_init, pm_metal_console_deinit);
