/* pymergetic.metal.console — N text rings (F1–F6). UART stays on #0. */
#include "pymergetic/metal/console/__exports__.h"
#include "pymergetic/metal/console/__types__.h"

#include "pymergetic/util/limits.h"
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

/* Screens the seat offers (F1-F6), the default of console.screen. A screen
 * costs nothing until something is printed on it: the knob says how many
 * there are to select, not how much memory is standing by. */
#ifndef PM_METAL_CONSOLE_SCREEN_DEFAULT
#define PM_METAL_CONSOLE_SCREEN_DEFAULT 6u
#endif
/* LINES/COLS live in __types__.h: a reader outside this card sizes its line
 * buffer from them. */
#ifndef PM_METAL_CONSOLE_VP_DEFAULT
#define PM_METAL_CONSOLE_VP_DEFAULT 4u
#endif

typedef struct {
    const char *kind;
    pm_metal_console_sink_fn sink;
} pm_metal_console_vp_t;

/* A reader that pulls. `who` is the reader's own name for itself (a browser
 * tab picks one per panel) so two tabs are two views and one tab polling
 * forever is still one; a reader that gives no name shares slot `who == 0`,
 * which counts as one anonymous view however many there are — better than
 * counting a poll as a viewer. */
typedef struct {
    const char *kind;
    uint32_t who;
    uint32_t at_ms;
    int used;
} pm_metal_console_tap_t;

typedef struct {
    int live;
    uint32_t head;
    uint32_t count;
    uint32_t seq;
    uint32_t col;
    /* ring_lines * PM_METAL_CONSOLE_COLS, laid out oldest-first from `head`
     * as usual; NULL until this console has a line to keep. */
    char *ring;
    uint32_t ring_lines;
    char *line;
    pm_metal_console_vp_t *vp;
    uint32_t nvp;
    uint32_t vp_cap;
    pm_metal_console_tap_t *tap;
    uint32_t tap_cap;
} pm_metal_console_t;

static int s_ready;
static uint32_t s_focus;
static pm_util_mem_arena_t *s_arena;
/* The screens that have something on them. The table is as wide as the
 * highest id in use, and a console's own memory arrives with its first line. */
static pm_metal_console_t *s_c;
static uint32_t s_c_cap;
static uint32_t s_c_used;

static int32_t scrollback_apply(pm_util_limit_t *knob);

PM_UTIL_LIMIT_C(pm_console_limit_screen, "console.screen",
    PM_METAL_CONSOLE_SCREEN_DEFAULT, 0u, &s_c_used);
/* A line is PM_METAL_CONSOLE_COLS bytes, so the hard ceiling is what keeps
 * "deeper scrollback" from meaning "the whole arena". The ring is reshaped
 * when this moves, by whoever moved it — see ring_fit. */
PM_UTIL_LIMIT_APPLY_C(pm_console_limit_scrollback, "console.scrollback",
    PM_METAL_CONSOLE_LINES_DEFAULT, 65536u, NULL, scrollback_apply);
PM_UTIL_LIMIT_C(pm_console_limit_viewport, "console.viewport",
    PM_METAL_CONSOLE_VP_DEFAULT, 0u, NULL);
PM_UTIL_LIMIT_C(pm_console_limit_tap, "console.tap",
    PM_METAL_CONSOLE_TAP_DEFAULT, 0u, NULL);

/* How many screens this seat offers right now. */
static uint32_t screens(void) {
    uint32_t n = pm_console_limit_screen.soft;
    return n != 0u ? n : pm_console_limit_screen.dflt;
}

/* The line the ring keeps at `slot`. */
static char *ring_line(const pm_metal_console_t *c, uint32_t slot) {
    return c->ring + ((size_t)slot * PM_METAL_CONSOLE_COLS);
}

static int ring_fit(pm_metal_console_t *c);

/* A console that already has memory, or NULL. Readers take this one: asking
 * about a screen nobody has printed on must not conjure a ring for it. */
static pm_metal_console_t *console_get(int32_t id) {
    if (id < 0 || (uint32_t)id >= screens() || (uint32_t)id >= s_c_cap) {
        return NULL;
    }
    return s_c[id].live ? &s_c[id] : NULL;
}

/* The console at `id`, made if this is its first line. NULL when the seat
 * offers no such screen or the arena has nothing left — callers then fall
 * back to the fill rather than losing the output. */
static pm_metal_console_t *console_make(int32_t id) {
    pm_metal_console_t *c = console_get(id);
    if (c != NULL) {
        return c;
    }
    if (id < 0 || (uint32_t)id >= screens() || s_arena == NULL) {
        return NULL;
    }
    if ((uint32_t)id >= s_c_cap) {
        uint32_t cap = s_c_cap;
        pm_metal_console_t *grown = pm_util_limits_grow(s_arena, s_c, &cap,
            (uint32_t)sizeof(*s_c), &pm_console_limit_screen);
        if (grown == NULL || (uint32_t)id >= cap) {
            return NULL;
        }
        s_c = grown;
        s_c_cap = cap;
    }
    c = &s_c[id];
    c->line = pm_util_mem_alloc(s_arena, PM_METAL_CONSOLE_COLS);
    if (c->line == NULL) {
        return NULL;
    }
    c->line[0] = 0;
    c->live = 1;
    s_c_used++;
    (void)ring_fit(c);
    return c;
}

/* The ring at the depth console.scrollback asks for now.
 *
 * Called when a console is opened and when a seat moves the knob — never
 * from the write path. Printing must not reach the allocator: a line can be
 * printed from anywhere, including a context that already holds the arena's
 * lock, and a console that allocated as it filled would turn that into a
 * seat that stops talking. A console's ring is therefore one allocation,
 * made when the console is opened, and reshaped only when asked.
 *
 * The ring is re-laid out oldest-first on the way, so growing it is a copy
 * in order and coming back down keeps the newest lines. */
static int ring_fit(pm_metal_console_t *c) {
    uint32_t want = pm_console_limit_scrollback.soft;
    uint32_t target;
    char *ring;
    uint32_t keep;
    uint32_t i;
    if (want == 0u) {
        want = pm_console_limit_scrollback.dflt;
    }
    target = want;
    if (target == c->ring_lines) {
        return c->ring != NULL ? 0 : -1;
    }
    ring = pm_util_mem_alloc(s_arena, (size_t)target * PM_METAL_CONSOLE_COLS);
    if (ring == NULL) {
        return c->ring != NULL ? 0 : -1; /* keep what there is */
    }
    keep = c->count < target ? c->count : target;
    for (i = 0; i < keep; i++) {
        /* The newest `keep` lines, oldest of them first. */
        uint32_t from = (c->head + c->ring_lines - (keep - i)) % c->ring_lines;
        memcpy(ring + ((size_t)i * PM_METAL_CONSOLE_COLS), ring_line(c, from),
            PM_METAL_CONSOLE_COLS);
    }
    if (c->ring != NULL) {
        pm_util_mem_free(s_arena, c->ring);
    }
    c->ring = ring;
    c->ring_lines = target;
    c->count = keep;
    c->head = keep % target;
    return 0;
}

/* console.scrollback moved: every open console follows it, here and now, in
 * the context of whoever turned the knob. */
static int32_t scrollback_apply(pm_util_limit_t *knob) {
    uint32_t i;
    (void)knob;
    for (i = 0; i < s_c_cap; i++) {
        if (s_c[i].live && ring_fit(&s_c[i]) != 0) {
            return -1;
        }
    }
    return 0;
}

static void console_release(pm_metal_console_t *c) {
    if (c == NULL || !c->live) {
        return;
    }
    if (c->ring != NULL) {
        pm_util_mem_free(s_arena, c->ring);
    }
    if (c->line != NULL) {
        pm_util_mem_free(s_arena, c->line);
    }
    if (c->vp != NULL) {
        pm_util_mem_free(s_arena, c->vp);
    }
    if (c->tap != NULL) {
        pm_util_mem_free(s_arena, c->tap);
    }
    memset(c, 0, sizeof(*c));
    if (s_c_used != 0u) {
        s_c_used--;
    }
}

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
    c->line[c->col < PM_METAL_CONSOLE_COLS ? c->col : (PM_METAL_CONSOLE_COLS - 1u)] = 0;
    if (c->ring == NULL) {
        /* Nowhere to keep it: the line still went out to the viewports, and
         * the sequence still counts it, so a reader is told it scrolled out
         * rather than being handed the wrong line. */
        c->seq++;
        c->col = 0;
        c->line[0] = 0;
        return;
    }
    memcpy(ring_line(c, c->head), c->line, PM_METAL_CONSOLE_COLS);
    c->head = (c->head + 1u) % c->ring_lines;
    c->seq++;
    if (c->count < c->ring_lines) {
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
    start = (c->ring != NULL && c->count == c->ring_lines) ? c->head : 0u;
    for (i = 0; c->ring != NULL && i < c->count; i++) {
        const char *line = ring_line(c, (start + i) % c->ring_lines);
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
    if (id >= screens() || kind == NULL || kind[0] == 0 || sink == NULL) {
        return -1;
    }
    c = console_make((int32_t)id);
    if (c == NULL) {
        return -1;
    }
    if (!PM_UTIL_LIMIT_ROOM(pm_console_limit_viewport, c->nvp)) {
        return -1;
    }
    if (c->nvp >= c->vp_cap) {
        uint32_t cap = c->vp_cap;
        pm_metal_console_vp_t *grown = pm_util_limits_grow(s_arena, c->vp, &cap,
            (uint32_t)sizeof(*c->vp), &pm_console_limit_viewport);
        if (grown == NULL || cap <= c->nvp) {
            return -1;
        }
        c->vp = grown;
        c->vp_cap = cap;
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
    if (kind == NULL || to_id >= screens()) {
        return -1;
    }
    for (i = 0; i < s_c_cap; i++) {
        uint32_t v;
        if (!s_c[i].live) {
            continue;
        }
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
    if (id >= screens()) {
        return -1;
    }
    if (s == NULL || n == 0) {
        return 0;
    }
    c = console_make((int32_t)id);
    if (!s_ready || c == NULL) {
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
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    s_focus = 0;
    s_ready = 0;
    if (attach_id(0, fill_kind(), fill_write) != 0) {
        return -1;
    }
    s_ready = 1;
    return 0;
}

static void pm_metal_console_deinit(void) {
    uint32_t i;
    s_ready = 0;
    s_focus = 0;
    for (i = 0; i < s_c_cap; i++) {
        console_release(&s_c[i]);
    }
    if (s_c != NULL && s_arena != NULL) {
        pm_util_mem_free(s_arena, s_c);
    }
    s_c = NULL;
    s_c_cap = 0;
    s_c_used = 0;
    s_arena = NULL;
}

int32_t pm_metal_console_ready(void) {
    return s_ready;
}

uint32_t pm_metal_console_count(void) {
    return screens();
}

uint32_t pm_metal_console_id(void) {
    return s_focus;
}

int32_t pm_metal_console_select(int32_t id) {
    if (id < 0 || (uint32_t)id >= screens()) {
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
    const pm_metal_console_t *c = console_get(id);
    return c != NULL ? c->nvp : 0u;
}

uint32_t pm_metal_console_viewport_count(void) {
    return pm_metal_console_viewport_count_id(0);
}

const char *pm_metal_console_viewport_kind_id(int32_t id, uint32_t i) {
    const pm_metal_console_t *c = console_get(id);
    if (c == NULL || i >= c->nvp) {
        return NULL;
    }
    return c->vp[i].kind;
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

/* Taps — the pulling half of "who is looking at this console".
 *
 * The card keeps no clock of its own: the caller hands in the milliseconds it
 * already has (the route reads the coop mono clock), which is also what lets a
 * test walk time forward without waiting for it. Unsigned subtraction is the
 * age even across a wrap of the millisecond counter. */
static int tap_live(const pm_metal_console_tap_t *t, uint32_t now_ms) {
    return t->used && (uint32_t)(now_ms - t->at_ms) <= PM_METAL_CONSOLE_TAP_TTL_MS;
}

int32_t pm_metal_console_tap_id(int32_t id, uint32_t who, uint32_t now_ms, const char *kind) {
    pm_metal_console_t *c;
    uint32_t i;
    uint32_t slot;
    uint32_t oldest = 0;
    if (id < 0 || (uint32_t)id >= screens() || kind == NULL || kind[0] == 0) {
        return -1;
    }
    c = console_make(id);
    if (c == NULL) {
        return -1;
    }
    slot = c->tap_cap;
    for (i = 0; i < c->tap_cap; i++) {
        if (c->tap[i].used && c->tap[i].who == who) {
            c->tap[i].at_ms = now_ms;
            c->tap[i].kind = kind;
            return 0;
        }
        /* A free slot, or one whose reader has gone quiet, before a live one. */
        if (slot == c->tap_cap && (!c->tap[i].used || !tap_live(&c->tap[i], now_ms))) {
            slot = i;
        }
        if ((uint32_t)(now_ms - c->tap[i].at_ms)
            > (uint32_t)(now_ms - c->tap[oldest].at_ms)) {
            oldest = i;
        }
    }
    if (slot == c->tap_cap) {
        /* Every slot is a live reader: take one more while the knob allows. */
        uint32_t cap = c->tap_cap;
        pm_metal_console_tap_t *grown = pm_util_limits_grow(s_arena, c->tap, &cap,
            (uint32_t)sizeof(*c->tap), &pm_console_limit_tap);
        if (grown != NULL && cap > c->tap_cap) {
            c->tap = grown;
            c->tap_cap = cap;
        }
    }
    /* Still full: the quietest reader loses its place rather than the
     * newcomer being turned away, so the count follows who is actually
     * watching now. */
    if (slot >= c->tap_cap) {
        slot = oldest;
    }
    if (c->tap == NULL) {
        return -1;
    }
    c->tap[slot].used = 1;
    c->tap[slot].who = who;
    c->tap[slot].at_ms = now_ms;
    c->tap[slot].kind = kind;
    return 0;
}

uint32_t pm_metal_console_tap_count_id(int32_t id, uint32_t now_ms) {
    const pm_metal_console_t *c = console_get(id);
    uint32_t i;
    uint32_t n = 0;
    if (c == NULL || c->tap == NULL) {
        return 0;
    }
    for (i = 0; i < c->tap_cap; i++) {
        if (tap_live(&c->tap[i], now_ms)) {
            n++;
        }
    }
    return n;
}

/* The kind of the i-th tap still counted, so a tree can name the rows it just
 * counted. Indexed over the live ones only: a slot that went quiet is not a
 * gap in the list, it is not in the list. */
const char *pm_metal_console_tap_kind_id(int32_t id, uint32_t i, uint32_t now_ms) {
    const pm_metal_console_t *c = console_get(id);
    uint32_t k;
    uint32_t n = 0;
    if (c == NULL || c->tap == NULL) {
        return NULL;
    }
    for (k = 0; k < c->tap_cap; k++) {
        if (!tap_live(&c->tap[k], now_ms)) {
            continue;
        }
        if (n == i) {
            return c->tap[k].kind;
        }
        n++;
    }
    return NULL;
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
    const pm_metal_console_t *c = console_get(id);
    return c != NULL ? c->count : 0u;
}

uint32_t pm_metal_console_line_count(void) {
    return pm_metal_console_line_count_id(0);
}

/* Reading the ring back.
 *
 * A viewport is a sink: it has to be there when the line is written, and it
 * gets the bytes once. That is the wrong shape for a reader that comes and
 * goes — a browser panel polling the seat, an ssh session reconnecting — so
 * lines also carry a sequence number, counted from the first line the console
 * ever committed, and a reader asks for the ones it has not seen. It is the
 * same cursor the build card's event ring hands out, so both halves of the
 * seat's live output are read the same way.
 *
 * Sequence numbers are the line's identity, not its slot: the ring keeps as
 * many of them as console.scrollback allows, and asking for one that has
 * scrolled out says so rather than handing back whatever now sits there. */
uint32_t pm_metal_console_seq_id(int32_t id) {
    const pm_metal_console_t *c = console_get(id);
    return c != NULL ? c->seq : 0u;
}

uint32_t pm_metal_console_seq(void) {
    return pm_metal_console_seq_id(0);
}

int32_t pm_metal_console_line_at_id(int32_t id, uint32_t seq, char *dst, uint32_t cap) {
    const pm_metal_console_t *c;
    const char *line;
    uint32_t back;
    uint32_t n = 0;
    if (dst == NULL || cap == 0) {
        return -1;
    }
    dst[0] = 0;
    c = console_get(id);
    if (c == NULL || c->ring == NULL) {
        return -1;
    }
    if (seq >= c->seq || c->seq - seq > c->count) {
        return -1; /* not written yet, or scrolled out of the ring */
    }
    back = c->seq - seq;
    line = ring_line(c, (c->head + c->ring_lines - back) % c->ring_lines);
    while (line[n] != 0 && n + 1u < cap) {
        dst[n] = line[n];
        n++;
    }
    dst[n] = 0;
    return (int32_t)n;
}

int32_t pm_metal_console_line_at(uint32_t seq, char *dst, uint32_t cap) {
    return pm_metal_console_line_at_id(0, seq, dst, cap);
}

/* The line being typed or printed right now, the one with no newline yet. A
 * reader that skipped it would sit on a blank panel through a prompt. */
uint32_t pm_metal_console_pending_id(int32_t id, char *dst, uint32_t cap) {
    const pm_metal_console_t *c;
    uint32_t n = 0;
    if (dst == NULL || cap == 0) {
        return 0;
    }
    dst[0] = 0;
    c = console_get(id);
    if (c == NULL) {
        return 0;
    }
    while (n < c->col && n + 1u < cap) {
        dst[n] = c->line[n];
        n++;
    }
    dst[n] = 0;
    return n;
}

uint32_t pm_metal_console_pending(char *dst, uint32_t cap) {
    return pm_metal_console_pending_id(0, dst, cap);
}

int32_t pm_metal_console_up(void) {
    uint32_t n0;
    if (!s_ready) {
        return -1;
    }
    n0 = pm_metal_console_line_count_id(0);
    if (pm_metal_console_select(1) != 0 || s_focus != 1u) {
        return -1;
    }
    if (write_id(1, "F2\n", 3) != 0 || pm_metal_console_line_count_id(1) < 1u) {
        (void)pm_metal_console_select(0);
        return -1;
    }
    if (pm_metal_console_line_count_id(0) != n0) {
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
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_seq, pm_metal_console_seq, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_seq_id, pm_metal_console_seq_id, uint32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_line_at, pm_metal_console_line_at, int32_t(uint32_t, char *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_line_at_id, pm_metal_console_line_at_id, int32_t(int32_t, uint32_t, char *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_pending, pm_metal_console_pending, uint32_t(char *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_pending_id, pm_metal_console_pending_id, uint32_t(int32_t, char *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_viewport_attach, pm_metal_console_viewport_attach, int32_t(const char *, pm_metal_console_sink_fn));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_viewport_attach_id, pm_metal_console_viewport_attach_id, int32_t(int32_t, const char *, pm_metal_console_sink_fn));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_tap_id, pm_metal_console_tap_id, int32_t(int32_t, uint32_t, uint32_t, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_tap_count_id, pm_metal_console_tap_count_id, uint32_t(int32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_tap_kind_id, pm_metal_console_tap_kind_id, const char *(int32_t, uint32_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_up, pm_metal_console_up, int32_t(void));

int32_t pm_metal_console_fb_attach(void);
PM_MOD_EXPORT_C(pymergetic.metal.console, pm_metal_console_fb_attach, pm_metal_console_fb_attach, int32_t(void));

PM_MOD_BOOT_C(pymergetic.metal.console, pm_metal_console_init, pm_metal_console_deinit);
