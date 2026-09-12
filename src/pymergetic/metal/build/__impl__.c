/* pymergetic.metal.build — __pmm__.toml manifests as data.
 *
 * (a) a minimal TOML reader: exactly the Phase-1 schema
 *     (key = "string" | integer | ["a", "b"], # comments, [table] headers
 *     accepted and skipped — the manifests are flat),
 * (b) unit_parse turning manifest bytes into a pm_metal_build_unit_t,
 * (c) graph_resolve: topological order on depends (cycle = error),
 * (d) compile_source / link: Phase 3 fills these (TCC objects into the
 *     in-tree ELF relocator).
 */
#include "pymergetic/metal/build/__exports__.h"

#include "pymergetic/metal/build/__types__.h"
#include "pymergetic/metal/jit/c/__types__.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/util/lock.h"

#include <stdatomic.h>

/* Transpiler cards for the non-C impls: rs -> C (micro-rustc), cpp -> C,
 * py -> mpy bytecode. Plain link-time faces, same posture as jit.c's
 * object_compile_opts. The py face refuses politely on seats without
 * MICROPY_PERSISTENT_CODE_SAVE — the py branch reports that refusal. */
#include "pymergetic/metal/jit/rs/compiler/__types__.h"
#include "pymergetic/metal/jit/cpp/__types__.h"
#include "pymergetic/metal/jit/py/__types__.h"

#include "pymergetic/metal/inspect/src_embed.inc.h"

#include "pymergetic/wasmmod/registry.h"
#include "pymergetic/metal/inspect/__exports__.h"

/* WASM-seat link path: the loader is RS (loader/__impl__.rs) on every seat,
 * but only the wasm-container seats need it for linking — ELF seats link
 * through the in-tree relocator instead. Cross seats (ELF + the second
 * wasm32 TCC instance) need BOTH routes: link dispatches on the object's
 * magic (\0asm -> loader, ELF -> relocator), not the seat's build flags. */
#if PM_HAS_TCC && defined(TCC_TARGET_WASM32) && !defined(PM_METAL_BUILD_HAS_ELF)
#define PM_METAL_BUILD_WASM_LINK 1
#include "pymergetic/wasmmod/loader/__exports__.h"
#endif
#if PM_HAS_TCC && defined(PM_METAL_TCC_CROSS_WASM32) && defined(PM_METAL_BUILD_HAS_ELF)
#define PM_METAL_BUILD_WASM_LINK 1
#define PM_METAL_BUILD_ELF_LINK 1
#include "pymergetic/wasmmod/loader/__exports__.h"
#endif

#include <stdio.h>
#include <string.h>

/* strtoul lives in stdlib.h; the firmware seats get it through their own
 * libc shims. */
#include <stdlib.h>

/*------------------ per-build context (arena-owned) ------------------
 * Every mutable table this card owns lives in ONE arena-owned context:
 * the build records (+epoch), the ledger scratch, the at-slots (+epoch),
 * the deps-parse scratch and the exec-range cache. A single TU-static
 * POINTER is the only module global left — the state itself is arena
 * memory, so a caller's arena dying never strands records the inspector
 * still serves.
 *
 * Lifetime: the ctx allocates lazily from the boot arena
 * (pm_metal_coop_arena — the arena pm_metal_boot created and nobody
 * frees until teardown), because records/at-slots must outlive any single
 * unit_compile's caller arena. Faces that only need scratch within one
 * call keep taking the caller's arena argument; only the retained state
 * rides the ctx. When the boot arena is unavailable (a unit test that
 * never booted async), ctx_acquire refuses — a face that needs retained
 * state reports NOMEM, never writes through a dangling pointer.
 *
 * What the ctx deliberately does NOT hold: TCC's reallocator state. That
 * is one global function pointer inside libtcc (tcc_set_realloc) — moving
 * our arena pointer into a struct does not make the callback reentrant or
 * parallel; jit.c's save/restore window keeps TCC serialized exactly as
 * before (see PM_METAL_TCC note in jit/c/__impl__.c). */

/* hoisted ahead of the ctx: both shapes are pure state carriers the ctx
 * embeds by value (defined at their original use sites below). */
typedef struct pm_build_at_slot {
    pm_metal_build_at_info_t info;
    int32_t valid;
} pm_build_at_slot_t;

typedef struct pm_build_exec_range {
    uintptr_t lo;
    uintptr_t hi;
} pm_build_exec_range_t;

/* accessor-spine slot count (hoisted: the ctx embeds the slot table) */
#define PM_METAL_BUILD_AT_SLOTS 4u

typedef struct pm_metal_build_ctx {
    /* one lock guards every ctx mutation once jobs/pane reads can run on
     * different cores: the event ring's seq+slot, the record table's
     * slot pick + field writes, the accessor slots' epoch. BSS zero is
     * the lock's unlocked state (same contract as the actor's), so the
     * lazy ctx create needs no explicit init. */
    pm_util_lock_t lock;
    /* build records (provenance chain) — retained per unit_compile, as deep
     * as the record knob asks for when the ctx is taken */
    pm_metal_build_record_t *records;
    uint32_t n_records;
    uint32_t record_epoch;
    /* change-ledger scratch: every ledger read path (read-modify-write
     * append + query scan) — never nested, one buffer on the ctx */
    uint8_t ledger_buf[PM_METAL_BUILD_LEDGER_MAX];
    /* factory-floor event ring: seq-numbered, as long as the event knob asks
     * for, wrapping there. seq starts at 1 so 0 = "nothing yet". */
    pm_metal_build_event_t *events;
    uint32_t n_events;
    uint32_t event_seq;
    /* accessor-spine slots: round-robin handles into at_info/at_ast */
    pm_build_at_slot_t at[PM_METAL_BUILD_AT_SLOTS];
    uint32_t at_epoch;
    /* deps-parse scratch (at_fill_deps's TOML arena) */
    uint8_t deps_scratch[8192];
#ifdef PM_METAL_BUILD_HAS_ELF
    /* exec-range cache (host seat: /proc/self/maps truth, lazy-loaded) */
    pm_build_exec_range_t exec_ranges[512];
    uint32_t n_exec_ranges;
    int exec_ranges_ready;
    uintptr_t exe_lo;
    uintptr_t exe_hi;
    int exe_bounds_ready;
#endif
} pm_metal_build_ctx_t;

static pm_metal_build_ctx_t *s_build_ctx;

/* The coop card's faces this driver uses: the boot arena (created by
 * pm_metal_boot, freed only at teardown), the poll pump (drive the ring
 * while another job holds the TCC serial section), the mono clock (the
 * event ring's t_us/dur_us), and the runner's task faces (the walk posts
 * itself as a coop task; the actor parks its job coro on it). The barrel
 * carries the prototypes; the link still goes through the same card
 * embed — no second implementation anywhere. */
#include "pymergetic/metal/coop.h"
#include "pymergetic/util/limits.h"

/* How much history the factory floor keeps. Both were sized by measuring a
 * whole-tree BUILD ALL and rounding up, which is exactly the guess a knob
 * replaces: a seat with a bigger tree raises the record knob, a seat that only
 * wants the last handful of events lowers the event knob, and neither number is
 * a shape the card was compiled around.
 *
 * Moving either one restarts that history. The tables are rings addressed by
 * an epoch modulo their depth, so a table of a different depth holds those
 * rows in different places; carrying them over would report the wrong build
 * against the wrong unit, which is worse than an empty pane. */
static int32_t records_apply(pm_util_limit_t *knob);
static int32_t events_apply(pm_util_limit_t *knob);
static uint32_t s_records_used;
PM_UTIL_LIMIT_APPLY_C(pm_build_limit_record, pymergetic.metal.build, record,
    PM_METAL_BUILD_RECORD_DEFAULT, 0u, &s_records_used, records_apply);
PM_UTIL_LIMIT_APPLY_C(pm_build_limit_event, pymergetic.metal.build, event,
    PM_METAL_BUILD_EVENT_DEFAULT, 0u, NULL, events_apply);

/* A ring is taken whole, so "no ceiling" reads as the build's own number. */
static uint32_t ring_want(const pm_util_limit_t *knob) {
    return knob->soft != 0u ? knob->soft : knob->dflt;
}

static pm_metal_build_ctx_t *build_ctx_acquire(void) {
    pm_util_mem_arena_t *arena;
    uint32_t nrec = ring_want(&pm_build_limit_record);
    uint32_t nev = ring_want(&pm_build_limit_event);
    if (s_build_ctx != NULL) {
        return s_build_ctx;
    }
    arena = pm_metal_coop_arena();
    if (arena == NULL) {
        return NULL;
    }
    s_build_ctx = (pm_metal_build_ctx_t *)pm_util_mem_alloc(
        arena, sizeof(pm_metal_build_ctx_t));
    if (s_build_ctx == NULL) {
        return NULL;
    }
    memset(s_build_ctx, 0, sizeof(*s_build_ctx));
    s_build_ctx->records = pm_util_mem_alloc(arena,
        (size_t)nrec * sizeof(*s_build_ctx->records));
    s_build_ctx->events = pm_util_mem_alloc(arena,
        (size_t)nev * sizeof(*s_build_ctx->events));
    if (s_build_ctx->records == NULL || s_build_ctx->events == NULL) {
        pm_util_mem_free(arena, s_build_ctx->records);
        pm_util_mem_free(arena, s_build_ctx->events);
        pm_util_mem_free(arena, s_build_ctx);
        s_build_ctx = NULL;
        return NULL;
    }
    memset(s_build_ctx->records, 0, (size_t)nrec * sizeof(*s_build_ctx->records));
    memset(s_build_ctx->events, 0, (size_t)nev * sizeof(*s_build_ctx->events));
    s_build_ctx->n_records = nrec;
    s_build_ctx->n_events = nev;
    return s_build_ctx;
}

/* Retake one of the ctx's rings at n entries, empty. The caller holds the ctx
 * lock, so no pane read is walking the table being swapped out. */
static int32_t ring_fit(void **table, uint32_t *have, uint32_t n, size_t item) {
    pm_util_mem_arena_t *arena = pm_metal_coop_arena();
    void *next;
    if (arena == NULL || n == 0u) {
        return -1;
    }
    if (n == *have) {
        return 0;
    }
    next = pm_util_mem_alloc(arena, (size_t)n * item);
    if (next == NULL) {
        return -1;
    }
    memset(next, 0, (size_t)n * item);
    pm_util_mem_free(arena, *table);
    *table = next;
    *have = n;
    return 0;
}

static int32_t records_apply(pm_util_limit_t *knob) {
    int32_t rc;
    if (s_build_ctx == NULL) {
        return 0; /* nothing taken yet; the ctx reads the knob when it is */
    }
    pm_util_lock_acquire(&s_build_ctx->lock);
    rc = ring_fit((void **)&s_build_ctx->records, &s_build_ctx->n_records,
        ring_want(knob), sizeof(*s_build_ctx->records));
    if (rc == 0) {
        s_build_ctx->record_epoch = 0;
        s_records_used = 0;
    }
    pm_util_lock_release(&s_build_ctx->lock);
    return rc;
}

static int32_t events_apply(pm_util_limit_t *knob) {
    int32_t rc;
    if (s_build_ctx == NULL) {
        return 0;
    }
    pm_util_lock_acquire(&s_build_ctx->lock);
    rc = ring_fit((void **)&s_build_ctx->events, &s_build_ctx->n_events,
        ring_want(knob), sizeof(*s_build_ctx->events));
    if (rc == 0) {
        /* The sequence starts over with the ring: a pane polling ?since=N
         * is told the newest is 0 and begins again, rather than being
         * handed rows the new depth places somewhere else. */
        s_build_ctx->event_seq = 0;
    }
    pm_util_lock_release(&s_build_ctx->lock);
    return rc;
}

/*------------------ build event ring (factory floor telemetry) ----------
 * One append face, one read face, zero per-lane state. emit() is a
 * best-effort no-op when the ctx cannot be acquired (a build before the
 * boot graph ran async): the build itself must never fail because the
 * telemetry could not be served. The ring wraps; reads replay the tail. */
static void build_event_emit(uint16_t kind, uint16_t target,
    const char *fqn, const char *src, uint32_t dur_us, uint32_t bytes) {
    pm_metal_build_ctx_t *ctx = build_ctx_acquire();
    pm_metal_build_event_t *e;
    uint32_t seq;
    if (ctx == NULL || fqn == NULL) {
        return;
    }
    /* the ctx lock makes seq+slot pick + the whole record write one
     * atomic append: a pane read on another core either sees the
     * previous tail or this complete event, never a torn one */
    pm_util_lock_acquire(&ctx->lock);
    ctx->event_seq++;
    seq = ctx->event_seq;
    /* wrap: slot = (seq-1) % EVENTS, so seq N and N+EVENTS collide and the
     * older tail is naturally overwritten in arrival order */
    e = &ctx->events[(seq - 1u) % ctx->n_events];
    memset(e, 0, sizeof(*e));
    e->seq = seq;
    e->kind = kind;
    e->target = target;
    e->t_us = (uint32_t)(pm_metal_coop_mono_us() & 0xffffffffu);
    e->dur_us = dur_us;
    e->bytes = bytes;
    {
        size_t i;
        for (i = 0; i + 1 < PM_METAL_BUILD_EVENT_FQN && fqn[i] != '\0'; i++) {
            e->fqn[i] = fqn[i];
        }
    }
    if (src != NULL) {
        size_t i;
        /* the file TAIL carries more than the head (paths are rooted) */
        size_t n = strlen(src);
        if (n >= PM_METAL_BUILD_EVENT_SRC) {
            src += n - (PM_METAL_BUILD_EVENT_SRC - 1u);
        }
        for (i = 0; i + 1 < PM_METAL_BUILD_EVENT_SRC && src[i] != '\0'; i++) {
            e->src[i] = src[i];
        }
    }
    pm_util_lock_release(&ctx->lock);
}

uint32_t pm_metal_build_events_since(uint32_t since,
    pm_metal_build_event_t *out, uint32_t max, uint32_t *latest) {
    pm_metal_build_ctx_t *ctx = build_ctx_acquire();
    uint32_t newest;
    uint32_t first;
    uint32_t n = 0;
    if (ctx == NULL) {
        if (latest != NULL) {
            *latest = 0;
        }
        return 0;
    }
    /* same lock as emit: the tail copy below reads whole events; without
     * it a concurrent append on another core could be half-copied out */
    pm_util_lock_acquire(&ctx->lock);
    newest = ctx->event_seq;
    if (latest != NULL) {
        *latest = newest;
    }
    if (out == NULL || max == 0 || since >= newest) {
        /* empty-tail read: RELEASE before the early return — the ring is
         * otherwise left locked and every later events pane (or emit)
         * spins forever on the acquired lock. */
        pm_util_lock_release(&ctx->lock);
        return 0;
    }
    /* replay window: the tail the ring still holds, clipped to max */
    first = since + 1u;
    if (newest - first >= ctx->n_events) {
        first = newest - ctx->n_events + 1u;
    }
    for (; first <= newest && n < max; first++) {
        out[n++] = ctx->events[(first - 1u) % ctx->n_events];
    }
    pm_util_lock_release(&ctx->lock);
    return n;
}

uint32_t pm_metal_build_events_latest(void) {
    pm_metal_build_ctx_t *ctx = build_ctx_acquire();
    uint32_t latest;
    if (ctx == NULL) {
        return 0;
    }
    pm_util_lock_acquire(&ctx->lock);
    latest = ctx->event_seq;
    pm_util_lock_release(&ctx->lock);
    return latest;
}

/*------------------ build records (provenance chain) ------------------
 * Retained per unit_compile. Arena memory dies with the caller's arena, but
 * the record must outlive it (the inspector serves it later), so names and
 * lengths are copied into fixed ctx storage. Object bytes stay arena-
 * owned and are NOT retained — the record carries lengths + symbols only;
 * the inspector's /build/<fqn>/<file> pane serves authored source with
 * provenance, not a byte dump of the .o. */
typedef struct pm_build_rec_sym_ctx {
    pm_metal_build_record_t *r;
    uint32_t w;
} pm_build_rec_sym_ctx_t;

#ifdef PM_METAL_BUILD_HAS_ELF
static void record_sym_cb(const char *name, void *addr, void *ctx_in) {
    pm_build_rec_sym_ctx_t *ctx = (pm_build_rec_sym_ctx_t *)ctx_in;
    (void)addr;
    if (ctx == NULL || ctx->r == NULL
        || ctx->w >= PM_METAL_BUILD_MAX_SYMS) {
        return;
    }
    snprintf(ctx->r->sym_names_buf[ctx->w], PM_METAL_BUILD_SYM_NAME_MAX, "%s", name);
    ctx->r->sym_names[ctx->w] = ctx->r->sym_names_buf[ctx->w];
    ctx->w++;
}
#endif

static pm_metal_build_record_t *record_slot(const char *fqn) {
    pm_metal_build_ctx_t *ctx = build_ctx_acquire();
    uint32_t i;
    if (ctx == NULL) {
        return NULL;
    }
    for (i = 0; i < ctx->n_records; i++) {
        if (ctx->records[i].valid && strcmp(ctx->records[i].fqn, fqn) == 0) {
            return &ctx->records[i];
        }
    }
    return NULL;
}

static pm_metal_build_record_t *record_slot_acquire_locked(const char *fqn) {
    /* ctx lock held by the caller: pick + init one atomic slot transition.
     * New slots publish valid AFTER the caller's field writes (record
     * publish, the call sites below) — a reader on another core never
     * sees a torn record. The refresh path (existing slot) relies on the
     * caller's surrounding critical section, same as the emit ring. */
    pm_metal_build_ctx_t *ctx = build_ctx_acquire();
    pm_metal_build_record_t *r;
    if (ctx == NULL) {
        return NULL;
    }
    r = record_slot(fqn);
    if (r != NULL) {
        return r;   /* rebuild of an already-recorded unit: refresh in place */
    }
    /* oldest-slot eviction: epoch round-robins through the table */
    r = &ctx->records[ctx->record_epoch % ctx->n_records];
    ctx->record_epoch++;
    memset(r, 0, sizeof(*r));
    snprintf(r->fqn, sizeof(r->fqn), "%s", fqn);
    return r;
}

/* record publish: the writer's field writes end, the slot becomes visible
 * to readers. Valid-before-fields is the torn-read the ctx lock's record
 * section prevents; call exactly once after the last field write. */
static void record_publish_locked(pm_metal_build_record_t *r) {
    if (r != NULL) {
        r->valid = 1;
    }
}

const pm_metal_build_record_t *pm_metal_build_record_find(const char *fqn) {
    pm_metal_build_record_t *r = NULL;
    pm_metal_build_ctx_t *ctx;
    if (fqn == NULL) {
        return NULL;
    }
    ctx = build_ctx_acquire();
    if (ctx == NULL) {
        return NULL;
    }
    /* the scan is lock-held so the epoch/slot state it walks is stable;
     * the returned record's fields are read outside — valid publishes
     * after full writes (record_publish_locked), so a read sees either
     * an old complete record or the new complete one */
    pm_util_lock_acquire(&ctx->lock);
    r = record_slot(fqn);
    pm_util_lock_release(&ctx->lock);
    return r;
}

void pm_metal_build_record_reset(void) {
    if (s_build_ctx != NULL) {
        pm_util_lock_acquire(&s_build_ctx->lock);
        memset(s_build_ctx->records, 0,
            (size_t)s_build_ctx->n_records * sizeof(*s_build_ctx->records));
        s_build_ctx->record_epoch = 0;
        s_records_used = 0;
        pm_util_lock_release(&s_build_ctx->lock);
    }
}

/*------------------ change ledger (fs-backed, JSON-lines) ------------------
 * One file in the fs card: /src/.changes.jsonl. note_add appends a JSON line
 * (read the file, drop it, re-add with the new bytes — fs_add refuses
 * duplicates, so append is a read-modify-write). notes_query scans lines and
 * concatenates matches. The ledger is seeded from the authored
 * changes.jsonl beside the muscle (source-in-its-lang: the seed is a real
 * file, embedded as bytes, never a C string). */

#include "pymergetic/metal/fs/__exports__.h"
#include "pymergetic/metal/build/changes_embed.inc.h"

#define PM_METAL_BUILD_LEDGER_PATH "/src/.changes.jsonl"

/* One shared scratch for every ledger read path (file read + scan). The
 * runtime is single-threaded and note_add / notes_query never nest, so a
 * single ctx buffer keeps the card's retained footprint at one
 * ledger-sized block (firmware seats link this card too). */

const char *pm_metal_build_ledger_path(void) {
    return PM_METAL_BUILD_LEDGER_PATH;
}

/* Materialize the ledger file into fs at first use (idempotent). Returns 0
 * when the file exists (now or before), -1 when it cannot be created. */
static int32_t ledger_ensure(void) {
    uint32_t len = 0;
    int32_t st = pm_metal_fs_stat(PM_METAL_BUILD_LEDGER_PATH, &len);
    if (st == 0) {
        return len > 0 ? 0 : -1;
    }
    return pm_metal_fs_add(PM_METAL_BUILD_LEDGER_PATH,
        (const uint8_t *)pm_metal_build_changes_jsonl(),
        pm_metal_build_changes_jsonl_len()) < 0 ? -1 : 0;
}

/* Portable bounded substring find (memmem is a GNU extension; firmware
 * freestanding has neither). Returns the match or NULL. */
static const char *build_memfind(const char *hay, size_t hay_n,
    const char *needle) {
    size_t nn = strlen(needle);
    size_t i;
    if (nn == 0 || hay_n < nn) {
        return NULL;
    }
    for (i = 0; i + nn <= hay_n; i++) {
        if (hay[i] == needle[0] && memcmp(hay + i, needle, nn) == 0) {
            return hay + i;
        }
    }
    return NULL;
}

/* JSON string escape into out (bounded). Returns bytes written. */
static size_t note_esc(const char *s, char *out, size_t out_max) {
    size_t w = 0;
    for (; *s != 0 && w + 7u < out_max; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            out[w++] = '\\';
            out[w++] = (char)c;
        } else if (c == '\n') {
            out[w++] = '\\';
            out[w++] = 'n';
        } else if (c == '\r') {
            out[w++] = '\\';
            out[w++] = 'r';
        } else if (c == '\t') {
            out[w++] = '\\';
            out[w++] = 't';
        } else if (c < 0x20u) {
            w += (size_t)snprintf(out + w, out_max - w, "\\u%04x", c);
        } else {
            out[w++] = (char)c;
        }
    }
    out[w] = 0;
    return w;
}

static const char *note_kind_name(pm_metal_build_note_kind_t kind) {
    switch (kind) {
        case PM_METAL_BUILD_NOTE_CHANGE:   return "change";
        case PM_METAL_BUILD_NOTE_DECISION: return "decision";
        case PM_METAL_BUILD_NOTE_WARNING:  return "warning";
        case PM_METAL_BUILD_NOTE_TODO:     return "todo";
        default:                           return NULL;
    }
}

int32_t pm_metal_build_note_add(const char *target,
    pm_metal_build_note_kind_t kind, const char *reason,
    const char *const *refs, uint32_t n_refs) {
    static char line[512 + PM_METAL_BUILD_NOTE_REFS_MAX * 80u];
    char esc[2 * PM_METAL_BUILD_NOTE_REASON_MAX];
    const char *kname;
    size_t w = 0;
    uint32_t existing_len = 0;
    uint32_t i;
    if (target == NULL || target[0] == 0 || reason == NULL || reason[0] == 0) {
        return PM_METAL_BUILD_ERR_PARSE;
    }
    kname = note_kind_name(kind);
    if (kname == NULL) {
        return PM_METAL_BUILD_ERR_PARSE;
    }
    if (n_refs > PM_METAL_BUILD_NOTE_REFS_MAX) {
        n_refs = PM_METAL_BUILD_NOTE_REFS_MAX;
    }
    if (ledger_ensure() != 0) {
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    w = (size_t)snprintf(line, sizeof(line), "{\"kind\":\"%s\",\"target\":\"%s\",\"reason\":\"",
        kname, target);
    if (w >= sizeof(line)) {
        return PM_METAL_BUILD_ERR_PARSE;
    }
    note_esc(reason, esc, sizeof(esc));
    w += (size_t)snprintf(line + w, sizeof(line) - w, "%s\"", esc);
    if (w >= sizeof(line)) {
        return PM_METAL_BUILD_ERR_PARSE;
    }
    if (n_refs > 0) {
        int wrote_any = 0;
        w += (size_t)snprintf(line + w, sizeof(line) - w, ",\"refs\":[");
        for (i = 0; i < n_refs && w < sizeof(line); i++) {
            if (refs[i] == NULL) {
                continue;
            }
            if (wrote_any) {
                w += (size_t)snprintf(line + w, sizeof(line) - w, ",");
            }
            note_esc(refs[i], esc, sizeof(esc));
            w += (size_t)snprintf(line + w, sizeof(line) - w, "\"%s\"", esc);
            wrote_any = 1;
        }
        w += (size_t)snprintf(line + w, sizeof(line) - w, "]");
    }
    if (w >= sizeof(line)) {
        return PM_METAL_BUILD_ERR_PARSE;
    }
    w += (size_t)snprintf(line + w, sizeof(line) - w, "}\n");
    if (w >= sizeof(line)) {
        return PM_METAL_BUILD_ERR_PARSE;
    }
    /* read-modify-write append: fs_add refuses an existing path. The ctx
     * lock covers the whole ledger RMW — the buffer is shared ctx scratch
     * and at_fill_notes may be reading it on another core right now. */
    {
        pm_metal_build_ctx_t *ctx = build_ctx_acquire();
        uint32_t got = 0;
        uint8_t *existing;
        int32_t frc = 0;
        if (ctx == NULL) {
            return PM_METAL_BUILD_ERR_NOMEM;
        }
        pm_util_lock_acquire(&ctx->lock);
        existing = ctx->ledger_buf;
        if (pm_metal_fs_stat(PM_METAL_BUILD_LEDGER_PATH, &existing_len) != 0) {
            frc = PM_METAL_BUILD_ERR_NOMEM;
        }
        if (existing_len > 0) {
            got = existing_len;
            if (got > sizeof(ctx->ledger_buf)) {
                got = sizeof(ctx->ledger_buf);
            }
            if (pm_metal_fs_read(PM_METAL_BUILD_LEDGER_PATH, existing, &got) != 0) {
                frc = PM_METAL_BUILD_ERR_NOMEM;
            }
        }
        if (frc == 0 && got + w >= sizeof(ctx->ledger_buf)) {
            frc = PM_METAL_BUILD_ERR_NOMEM;
        }
        if (frc == 0 && pm_metal_fs_drop(PM_METAL_BUILD_LEDGER_PATH) != 0
            && existing_len > 0) {
            frc = PM_METAL_BUILD_ERR_NOMEM;
        }
        if (frc == 0) {
            memcpy(existing + got, line, w);
            if (pm_metal_fs_add(PM_METAL_BUILD_LEDGER_PATH,
                    existing, got + (uint32_t)w) < 0) {
                frc = PM_METAL_BUILD_ERR_NOMEM;
            }
        }
        pm_util_lock_release(&ctx->lock);
        return frc;
    }
    return PM_METAL_BUILD_OK;
}

/* Does ledger line `ln` (length n) match target + kind? target NULL = all. */
static int note_line_match(const char *ln, size_t n, const char *target,
    int32_t kind) {
    char pat[PM_METAL_BUILD_NOTE_TARGET_MAX + 8u];
    if (kind >= 0) {
        const char *kname = note_kind_name((pm_metal_build_note_kind_t)kind);
        if (kname == NULL) {
            return 0;
        }
        snprintf(pat, sizeof(pat), "\"kind\":\"%s\"", kname);
        if (build_memfind(ln, n, pat) == NULL) {
            return 0;
        }
    }
    if (target == NULL) {
        return 1;
    }
    snprintf(pat, sizeof(pat), "\"target\":\"%s\"", target);
    return build_memfind(ln, n, pat) != NULL;
}

/* The lock-free body. The ctx lock is NOT taken here: callers that hold
 * the ctx lock (the at() fill, below) use this entry so the shared
 * ledger scratch is protected by THEIR critical section; the exported
 * face takes the lock and calls in. TU-local: no second card links it. */
static int32_t pm_metal_build_notes_query_locked(const char *target,
    int32_t kind, char *out, size_t out_len, uint32_t *out_n) {
    pm_metal_build_ctx_t *ctx = build_ctx_acquire();
    uint32_t len = 0;
    uint32_t n_match = 0;
    size_t w = 0;
    const char *p;
    const char *end;
    uint8_t *buf;
    if (out == NULL || out_len == 0 || out_n == NULL) {
        return PM_METAL_BUILD_ERR_PARSE;
    }
    if (ctx == NULL) {
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    buf = ctx->ledger_buf;
    out[0] = 0;
    *out_n = 0;
    if (ledger_ensure() != 0) {
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    len = sizeof(ctx->ledger_buf);
    if (pm_metal_fs_read(PM_METAL_BUILD_LEDGER_PATH, buf, &len) != 0) {
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    p = (const char *)buf;
    end = p + len;
    while (p < end) {
        const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
        size_t lnlen = nl != NULL ? (size_t)(nl - p) : (size_t)(end - p);
        if (lnlen > 0 && note_line_match(p, lnlen, target, kind)) {
            if (n_match > 0 && w + 1u < out_len) {
                out[w++] = '\n';
            }
            if (lnlen >= out_len - w) {
                lnlen = out_len - w - 1u;
            }
            memcpy(out + w, p, lnlen);
            w += lnlen;
            out[w] = 0;
            n_match++;
        }
        p = nl != NULL ? nl + 1 : end;
    }
    *out_n = n_match;
    return (int32_t)n_match;
}

int32_t pm_metal_build_notes_query(const char *target,
    int32_t kind, char *out, size_t out_len, uint32_t *out_n) {
    pm_metal_build_ctx_t *ctx = build_ctx_acquire();
    int32_t rc;
    if (ctx == NULL) {
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    /* ledger read into shared ctx scratch: under the same lock as the
     * RMW append and the at-slot fill's notes pass, so the buffer is
     * never mid-swap under a reader */
    pm_util_lock_acquire(&ctx->lock);
    rc = pm_metal_build_notes_query_locked(target, kind, out, out_len, out_n);
    pm_util_lock_release(&ctx->lock);
    return rc;
}

int32_t pm_metal_build_note_has(const char *target,
    pm_metal_build_note_kind_t kind) {
    char scratch[128];
    uint32_t n = 0;
    if (target == NULL || target[0] == 0) {
        return 0;
    }
    if (pm_metal_build_notes_query(target, (int32_t)kind, scratch,
            sizeof(scratch), &n) < 0) {
        return 0;
    }
    return n > 0 ? 1 : 0;
}

/*------------------ error helpers ------------------*/

static void err_set(char *errbuf, size_t errbuf_len, const char *fmt, int line) {
    if (errbuf == NULL || errbuf_len == 0) {
        return;
    }
    snprintf(errbuf, errbuf_len, "line %d: %s", line, fmt);
}

/*------------------ lexer ------------------*/

typedef struct pm_build_lex {
    const char *p;
    const char *end;
    int line;
} pm_build_lex_t;

static char lex_peek(pm_build_lex_t *lx) {
    return lx->p < lx->end ? *lx->p : '\0';
}

static void lex_skip_ws(pm_build_lex_t *lx) {
    for (;;) {
        char c = lex_peek(lx);
        if (c == ' ' || c == '\t' || c == '\r') {
            lx->p++;
        } else if (c == '\n') {
            lx->p++;
            lx->line++;
        } else if (c == '#') {
            while (lx->p < lx->end && *lx->p != '\n') {
                lx->p++;
            }
        } else {
            return;
        }
    }
}

/*------------------ unit assembly ------------------*/

typedef struct pm_build_strings {
    const char **items;
    uint32_t n;
    uint32_t cap;
    int oom;
} pm_build_strings_t;

static void strings_push(pm_util_mem_arena_t *arena, pm_build_strings_t *s, const char *v) {
    if (s->oom) {
        return;
    }
    if (s->n == s->cap) {
        uint32_t ncap = s->cap == 0 ? 8u : s->cap * 2u;
        const char **ng = (const char **)pm_util_mem_alloc(arena, ncap * sizeof(const char *));
        if (ng == NULL) {
            s->oom = 1;
            return;
        }
        if (s->n > 0) {
            memcpy(ng, s->items, s->n * sizeof(const char *));
        }
        s->items = ng;
        s->cap = ncap;
    }
    s->items[s->n++] = v;
}

static const char *dup_str(pm_util_mem_arena_t *arena, const char *src, size_t len) {
    char *d = (char *)pm_util_mem_alloc(arena, len + 1u);
    if (d == NULL) {
        return NULL;
    }
    memcpy(d, src, len);
    d[len] = '\0';
    return d;
}

/* Read a double-quoted string (caller consumed the opening quote). Escapes
 * limited to \" and \\ — the schema never needs more. */
static const char *parse_string(pm_util_mem_arena_t *arena, pm_build_lex_t *lx,
    int *bad_line) {
    const char *start = lx->p;
    size_t len = 0;
    while (lx->p < lx->end && *lx->p != '"') {
        if (*lx->p == '\n') {
            *bad_line = lx->line;
            return NULL;
        }
        if (*lx->p == '\\' && lx->p + 1 < lx->end
            && (lx->p[1] == '"' || lx->p[1] == '\\')) {
            lx->p++;
        }
        lx->p++;
        len++;
    }
    if (lx->p >= lx->end) {
        *bad_line = lx->line;
        return NULL;
    }
    lx->p++;  /* closing quote */
    char *out = (char *)pm_util_mem_alloc(arena, len + 1u);
    if (out == NULL) {
        return NULL;
    }
    const char *r = start;
    size_t w = 0;
    while (r < lx->p - 1) {
        if (*r == '\\' && r + 1 < lx->p - 1 && (r[1] == '"' || r[1] == '\\')) {
            r++;
        }
        out[w++] = *r++;
    }
    out[w] = '\0';
    return out;
}

/* Append a bare key token (up to '=' ). Returns arena-owned copy. */
static const char *parse_key(pm_util_mem_arena_t *arena, pm_build_lex_t *lx, int *bad_line) {
    const char *start = lx->p;
    while (lx->p < lx->end) {
        char c = *lx->p;
        if (c == '=' || c == '\n' || c == '#' || c == ' ' || c == '\t' || c == '\r') {
            break;
        }
        lx->p++;
    }
    size_t len = (size_t)(lx->p - start);
    if (len == 0) {
        *bad_line = lx->line;
        return NULL;
    }
    return dup_str(arena, start, len);
}

static int expect(pm_build_lex_t *lx, char c, int *bad_line) {
    if (lx->p < lx->end && *lx->p == c) {
        lx->p++;
        return 1;
    }
    *bad_line = lx->line;
    return 0;
}

/*------------------ TOML -> unit ------------------*/

static int set_scalar(pm_util_mem_arena_t *arena, pm_metal_build_unit_t *u,
    const char *key, const char *val, int *oom) {
    if (strcmp(key, "fqn") == 0) {
        if (strlen(val) >= sizeof(u->fqn)) return -1;
        snprintf(u->fqn, sizeof(u->fqn), "%s", val);
    } else if (strcmp(key, "impl") == 0) {
        if (strlen(val) >= sizeof(u->impl)) return -1;
        snprintf(u->impl, sizeof(u->impl), "%s", val);
    } else if (strcmp(key, "version") == 0) {
        if (strlen(val) >= sizeof(u->version)) return -1;
        snprintf(u->version, sizeof(u->version), "%s", val);
    } else if (strcmp(key, "upstream") == 0 || strcmp(key, "archive") == 0
        || strcmp(key, "notes") == 0) {
        /* provenance — carried by the manifest file, not by the unit */
    } else {
        return 0;
    }
    (void)arena;
    (void)oom;
    return 1;
}

int32_t pm_metal_build_unit_parse(pm_util_mem_arena_t *arena,
    const uint8_t *bytes, size_t len, pm_metal_build_unit_t *unit,
    char *errbuf, size_t errbuf_len) {
    pm_build_lex_t lx = { (const char *)bytes, (const char *)bytes + len, 1 };
    pm_build_strings_t sources = { 0 }, includes = { 0 }, defines = { 0 }, depends = { 0 };
    pm_build_strings_t *dst = NULL;
    int bad = 0;

    if (arena == NULL || bytes == NULL || unit == NULL) {
        err_set(errbuf, errbuf_len, "null argument", 0);
        return PM_METAL_BUILD_ERR_PARSE;
    }
    memset(unit, 0, sizeof(*unit));

    for (;;) {
        lex_skip_ws(&lx);
        if (lx.p >= lx.end) {
            break;
        }
        if (*lx.p == '[') {
            /* [table] header — the manifests are flat, skip the line */
            while (lx.p < lx.end && *lx.p != '\n') {
                lx.p++;
            }
            continue;
        }
        const char *key = parse_key(arena, &lx, &bad);
        if (key == NULL) {
            err_set(errbuf, errbuf_len, "expected key", bad ? bad : lx.line);
            return PM_METAL_BUILD_ERR_PARSE;
        }
        lex_skip_ws(&lx);
        if (!expect(&lx, '=', &bad)) {
            err_set(errbuf, errbuf_len, "expected '=' after key", lx.line);
            return PM_METAL_BUILD_ERR_PARSE;
        }
        lex_skip_ws(&lx);

        if (lex_peek(&lx) == '"') {
            lx.p++;
            const char *val = parse_string(arena, &lx, &bad);
            if (val == NULL) {
                err_set(errbuf, errbuf_len, "unterminated string", bad ? bad : lx.line);
                return PM_METAL_BUILD_ERR_PARSE;
            }
            int rc = set_scalar(arena, unit, key, val, &bad);
            if (rc < 0) {
                err_set(errbuf, errbuf_len, "value too long", lx.line);
                return PM_METAL_BUILD_ERR_PARSE;
            }
        } else if (lex_peek(&lx) == '[') {
            lx.p++;
            dst = NULL;
            if (strcmp(key, "sources") == 0) {
                dst = &sources;
            } else if (strcmp(key, "include_dirs") == 0) {
                dst = &includes;
            } else if (strcmp(key, "defines") == 0) {
                dst = &defines;
            } else if (strcmp(key, "depends") == 0) {
                dst = &depends;
            } else {
                err_set(errbuf, errbuf_len, "unknown array key", lx.line);
                return PM_METAL_BUILD_ERR_PARSE;
            }
            for (;;) {
                lex_skip_ws(&lx);
                if (lex_peek(&lx) == ']') {
                    lx.p++;
                    break;
                }
                if (lx.p >= lx.end) {
                    err_set(errbuf, errbuf_len, "unterminated array", lx.line);
                    return PM_METAL_BUILD_ERR_PARSE;
                }
                if (!expect(&lx, '"', &bad)) {
                    err_set(errbuf, errbuf_len, "expected string in array", lx.line);
                    return PM_METAL_BUILD_ERR_PARSE;
                }
                const char *val = parse_string(arena, &lx, &bad);
                if (val == NULL) {
                    err_set(errbuf, errbuf_len, "unterminated string in array",
                        bad ? bad : lx.line);
                    return PM_METAL_BUILD_ERR_PARSE;
                }
                strings_push(arena, dst, val);
                lex_skip_ws(&lx);
                if (lex_peek(&lx) == ',') {
                    lx.p++;
                }
            }
        } else {
            /* integer or anything else — the only non-string scalar the
             * schema carries is not one the unit needs, so consume it */
            const char *start = lx.p;
            while (lx.p < lx.end && *lx.p != '\n' && *lx.p != '#') {
                lx.p++;
            }
            while (lx.p > start && (lx.p[-1] == ' ' || lx.p[-1] == '\t' || lx.p[-1] == '\r')) {
                lx.p--;
            }
            if (lx.p == start) {
                err_set(errbuf, errbuf_len, "expected value", lx.line);
                return PM_METAL_BUILD_ERR_PARSE;
            }
        }
    }

    if (sources.oom || includes.oom || defines.oom || depends.oom) {
        err_set(errbuf, errbuf_len, "arena exhausted", lx.line);
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    unit->sources = sources.items;
    unit->n_sources = sources.n;
    unit->include_dirs = includes.items;
    unit->n_include_dirs = includes.n;
    unit->defines = defines.items;
    unit->n_defines = defines.n;
    unit->depends = depends.items;
    unit->n_depends = depends.n;

    if (unit->fqn[0] == '\0' || unit->impl[0] == '\0') {
        err_set(errbuf, errbuf_len, "manifest missing fqn or impl", lx.line);
        return PM_METAL_BUILD_ERR_PARSE;
    }
    return PM_METAL_BUILD_OK;
}

/*------------------ graph ------------------*/

static int unit_has_all_deps(const pm_metal_build_unit_t *u,
    const pm_metal_build_unit_t **done, uint32_t n_done) {
    uint32_t i, j;
    for (i = 0; i < u->n_depends; i++) {
        int found = 0;
        for (j = 0; j < n_done; j++) {
            if (strcmp(done[j]->fqn, u->depends[i]) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            return 0;
        }
    }
    return 1;
}

int32_t pm_metal_build_graph_resolve(pm_util_mem_arena_t *arena,
    pm_metal_build_unit_t *units, uint32_t n_units,
    const pm_metal_build_unit_t ***order, uint32_t *n_order,
    char *errbuf, size_t errbuf_len) {
    const pm_metal_build_unit_t **out = NULL;
    uint32_t n_done = 0;
    uint32_t i, j;

    if (arena == NULL || units == NULL || order == NULL || n_order == NULL) {
        err_set(errbuf, errbuf_len, "null argument", 0);
        return PM_METAL_BUILD_ERR_PARSE;
    }
    if (n_units == 0) {
        *order = NULL;
        *n_order = 0;
        return PM_METAL_BUILD_OK;
    }
    out = (const pm_metal_build_unit_t **)pm_util_mem_alloc(
        arena, n_units * sizeof(const pm_metal_build_unit_t *));
    if (out == NULL) {
        err_set(errbuf, errbuf_len, "arena exhausted", 0);
        return PM_METAL_BUILD_ERR_NOMEM;
    }

    /* duplicate fqn check first — a graph with two of the same node cannot
     * be ordered and every later lookup would be ambiguous */
    for (i = 0; i < n_units; i++) {
        for (j = i + 1u; j < n_units; j++) {
            if (strcmp(units[i].fqn, units[j].fqn) == 0) {
                err_set(errbuf, errbuf_len, units[i].fqn, 0);
                pm_util_mem_free(arena, out);
                return PM_METAL_BUILD_ERR_CYCLE;
            }
        }
    }

    /* Kahn's algorithm by selection: repeatedly emit any unit whose deps are
     * all already emitted. Deterministic (first match wins). */
    while (n_done < n_units) {
        uint32_t progressed = 0;
        for (i = 0; i < n_units; i++) {
            int emitted = 0;
            for (j = 0; j < n_done; j++) {
                if (out[j] == &units[i]) {
                    emitted = 1;
                    break;
                }
            }
            if (emitted) {
                continue;
            }
            if (unit_has_all_deps(&units[i], out, n_done)) {
                /* every named dep must exist somewhere in the set */
                uint32_t d;
                for (d = 0; d < units[i].n_depends; d++) {
                    int exists = 0;
                    for (j = 0; j < n_units; j++) {
                        if (strcmp(units[j].fqn, units[i].depends[d]) == 0) {
                            exists = 1;
                            break;
                        }
                    }
                    if (!exists) {
                        err_set(errbuf, errbuf_len, units[i].depends[d], 0);
                        pm_util_mem_free(arena, out);
                        return PM_METAL_BUILD_ERR_MISSING_DEP;
                    }
                }
                out[n_done++] = &units[i];
                progressed = 1;
            }
        }
        if (!progressed) {
            err_set(errbuf, errbuf_len, "dependency cycle", 0);
            pm_util_mem_free(arena, out);
            return PM_METAL_BUILD_ERR_CYCLE;
        }
    }

    *order = out;
    *n_order = n_done;
    return PM_METAL_BUILD_OK;
}

/*------------------ compile + link (Phase 3/4) ------------------*/

/* join(unit_root, rel) into arena storage; rel may be absolute already. */
static const char *join_path(pm_util_mem_arena_t *arena, const char *root,
    const char *rel) {
    size_t rl, el;
    char *out;
    if (rel == NULL || rel[0] == '\0') {
        return NULL;
    }
    if (rel[0] == '/' || root == NULL || root[0] == '\0') {
        return dup_str(arena, rel, strlen(rel));
    }
    rl = strlen(root);
    el = strlen(rel);
    out = (char *)pm_util_mem_alloc(arena, rl + 1u + el + 1u);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, root, rl);
    out[rl] = '/';
    memcpy(out + rl + 1u, rel, el);
    out[rl + 1u + el] = '\0';
    return out;
}

/* compile_source: drive the jit.c card's TCC object path (TCC_OUTPUT_OBJ →
 * ET_REL .o bytes in the arena), forwarding the unit's include_dirs (joined
 * with unit_root) and defines. Native seats only — the wasm32 browser cell
 * has no ELF object output and jit.c reports that via errbuf. */
int32_t pm_metal_build_compile_source(pm_util_mem_arena_t *arena,
    const pm_metal_build_unit_t *unit, const char *unit_root, const char *source,
    uint8_t **obj_out, size_t *obj_len, char *errbuf, size_t errbuf_len) {
    const char **includes = NULL;
    uint32_t n_includes = 0;
    uint32_t i;

    if (arena == NULL || unit == NULL || source == NULL || source[0] == '\0'
        || obj_out == NULL || obj_len == NULL) {
        err_set(errbuf, errbuf_len, "compile_source: bad args", 0);
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    if (unit->n_include_dirs > 0) {
        includes = (const char **)pm_util_mem_alloc(
            arena, unit->n_include_dirs * sizeof(const char *));
        if (includes == NULL) {
            err_set(errbuf, errbuf_len, "compile_source: arena exhausted", 0);
            return PM_METAL_BUILD_ERR_NOMEM;
        }
        for (i = 0; i < unit->n_include_dirs; i++) {
            includes[i] = join_path(arena, unit_root, unit->include_dirs[i]);
            if (includes[i] == NULL) {
                err_set(errbuf, errbuf_len, "compile_source: arena exhausted", 0);
                return PM_METAL_BUILD_ERR_NOMEM;
            }
            n_includes++;
        }
    }
    return pm_metal_jit_c_object_compile_opts(arena, source, strlen(source),
        includes, n_includes, unit->defines, unit->n_defines,
        obj_out, obj_len, errbuf, errbuf_len) == 0
        ? PM_METAL_BUILD_OK : PM_METAL_BUILD_ERR_COMPILE;
}

int32_t pm_metal_build_compile_source_target(pm_util_mem_arena_t *arena,
    const pm_metal_build_unit_t *unit, const char *unit_root, const char *source,
    int32_t target,
    uint8_t **obj_out, size_t *obj_len, char *errbuf, size_t errbuf_len) {
    const char **includes = NULL;
    uint32_t n_includes = 0;
    uint32_t i;

    if (arena == NULL || unit == NULL || source == NULL || source[0] == '\0'
        || obj_out == NULL || obj_len == NULL) {
        err_set(errbuf, errbuf_len, "compile_source: bad args", 0);
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    if (unit->n_include_dirs > 0) {
        includes = (const char **)pm_util_mem_alloc(
            arena, unit->n_include_dirs * sizeof(const char *));
        if (includes == NULL) {
            err_set(errbuf, errbuf_len, "compile_source: arena exhausted", 0);
            return PM_METAL_BUILD_ERR_NOMEM;
        }
        for (i = 0; i < unit->n_include_dirs; i++) {
            includes[i] = join_path(arena, unit_root, unit->include_dirs[i]);
            if (includes[i] == NULL) {
                err_set(errbuf, errbuf_len, "compile_source: arena exhausted", 0);
                return PM_METAL_BUILD_ERR_NOMEM;
            }
            n_includes++;
        }
    }
    return pm_metal_jit_c_object_compile_target(arena, source, strlen(source),
        includes, n_includes, unit->defines, unit->n_defines,
        target,
        obj_out, obj_len, errbuf, errbuf_len) == 0
        ? PM_METAL_BUILD_OK : PM_METAL_BUILD_ERR_COMPILE;
}

#ifdef PM_METAL_BUILD_HAS_ELF
#include "pymergetic/wasmmod/pack/format/elf/load.h"
#endif

/* Process resolver (Phase 4.3): resolve a rebuilt card's true externals
 * (tcc_new, malloc, registry functions) against the already-linked process
 * copies. dlopen(NULL) — the global symbol table of the running process,
 * RTLD_LAZY so no eager relocation of every loaded object — not RTLD_DEFAULT
 * because the latter is a dlsym-side constant of a different lookup mode
 * that some platforms restrict to global-scope queries only.
 *
 * On x86_64 the linked image is mapped MAP_32BIT (~<4GiB) while process
 * symbols sit at 0x7f...: a direct R_X86_64_PLT32 call would truncate its
 * 32-bit displacement. Every far target is answered with a synthesized
 * movabs+jmp thunk from ONE pre-allocated RWX thunk table — allocated before
 * the loader starts so it never moves (earlier relocs already point in). */
#define PM_BUILD_THUNK_SLOTS 512u
#define PM_BUILD_THUNK_BYTES 16u

typedef struct pm_build_resolve_ctx {
#ifdef PM_METAL_BUILD_HAS_ELF
    uint8_t *thunk_base;
    size_t thunk_used;
    int failed;
#else
    char _pad;
#endif
} pm_build_resolve_ctx_t;

#ifdef PM_METAL_BUILD_HAS_ELF
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/stat.h>

static void thunk_ctx_init(pm_build_resolve_ctx_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    /* MAP_32BIT like the loader's image mapping: the image's PLT32 relocs
     * point here with a signed 32-bit displacement, so the table must sit
     * in the same low address span. No fallback: without it links would
     * silently truncate (better an honest link error). */
    ctx->thunk_base = (uint8_t *)mmap(NULL,
        (size_t)PM_BUILD_THUNK_SLOTS * PM_BUILD_THUNK_BYTES,
        PROT_READ | PROT_WRITE | PROT_EXEC,
#if defined(__x86_64__) && defined(MAP_32BIT)
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT,
#else
        MAP_PRIVATE | MAP_ANONYMOUS,
#endif
        -1, 0);
    ctx->thunk_used = 0;
    ctx->failed = ctx->thunk_base == MAP_FAILED;
    if (ctx->failed) {
        ctx->thunk_base = NULL;
    }
}

static void thunk_ctx_deinit(pm_build_resolve_ctx_t *ctx) {
    if (ctx != NULL && ctx->thunk_base != NULL) {
        munmap(ctx->thunk_base,
            (size_t)PM_BUILD_THUNK_SLOTS * PM_BUILD_THUNK_BYTES);
        ctx->thunk_base = NULL;
    }
}

/* Executable ranges of this process, from /proc/self/maps. A far DATA symbol
 * (stderr, environ) must be returned raw: GOT slots hold full 64-bit
 * addresses, so nothing truncates. Only far CODE needs a thunk, because a
 * direct `call rel32` cannot reach 0x7f... from a MAP_32BIT image.
 * (pm_build_exec_range_t is hoisted above pm_metal_build_ctx_t.) */

/* The exec-range table + the main executable's own mapping bounds live in
 * the per-build ctx (see pm_metal_build_ctx_t) — process-lifetime truth
 * (a /proc/self/maps snapshot), cached lazily on first resolve. */

static void pm_build_exec_ranges_load(void) {
    pm_metal_build_ctx_t *ctx = build_ctx_acquire();
    FILE *f = fopen("/proc/self/maps", "r");
    long exe_ino = -1;
    struct stat st;
    if (ctx == NULL) {
        return;
    }
    if (stat("/proc/self/exe", &st) == 0) {
        exe_ino = (long)st.st_ino;
    }
    if (f == NULL) {
        ctx->exec_ranges_ready = 1;
        ctx->exe_bounds_ready = 1;
        return;
    }
    while (ctx->n_exec_ranges
        < (uint32_t)(sizeof(ctx->exec_ranges) / sizeof(ctx->exec_ranges[0]))) {
        char line[512];
        unsigned long lo, hi;
        unsigned long devmaj, devmin, ino;
        char perms[8];
        if (fgets(line, sizeof(line), f) == NULL) {
            break;
        }
        if (sscanf(line, "%lx-%lx %7s %*x %lx:%lx %lu",
            &lo, &hi, perms, &devmaj, &devmin, &ino) < 3) {
            continue;
        }
        if (perms[2] == 'x'
            && ctx->n_exec_ranges
                < (uint32_t)(sizeof(ctx->exec_ranges)
                    / sizeof(ctx->exec_ranges[0]))) {
            ctx->exec_ranges[ctx->n_exec_ranges].lo = (uintptr_t)lo;
            ctx->exec_ranges[ctx->n_exec_ranges].hi = (uintptr_t)hi;
            ctx->n_exec_ranges++;
        }
        if ((long)ino == exe_ino) {
            if (ctx->exe_lo == 0) {
                ctx->exe_lo = (uintptr_t)lo;
            }
            ctx->exe_hi = (uintptr_t)hi;
        }
    }
    fclose(f);
    ctx->exe_bounds_ready = 1;
    ctx->exec_ranges_ready = 1;
}

static int pm_build_addr_is_code(uintptr_t a) {
    pm_metal_build_ctx_t *ctx = build_ctx_acquire();
    uint32_t i;
    if (ctx == NULL) {
        return 0;
    }
    if (!ctx->exec_ranges_ready) {
        pm_build_exec_ranges_load();
    }
    for (i = 0; i < ctx->n_exec_ranges; i++) {
        if (a >= ctx->exec_ranges[i].lo && a < ctx->exec_ranges[i].hi) {
            return 1;
        }
    }
    return 0;
}

static void *proc_resolve(const char *name, void *ctx_in) {
    pm_build_resolve_ctx_t *ctx = (pm_build_resolve_ctx_t *)ctx_in;
    void *h = dlopen(NULL, RTLD_LAZY);
    void *p = h != NULL ? dlsym(h, name) : NULL;
    if (p == NULL) {
        pm_metal_build_ctx_t *bctx = build_ctx_acquire();
        /* the boot card's weak image symbols: firmware gets them from the
         * linker script; the host seat's kernel image IS this process */
        if (bctx == NULL) {
            return NULL;
        }
        if (strcmp(name, "__pm_metal_image_base") == 0) {
            if (!bctx->exe_bounds_ready) {
                pm_build_exec_ranges_load();
            }
            if (bctx->exe_lo != 0 && bctx->exe_hi > bctx->exe_lo) {
                return (void *)bctx->exe_lo;
            }
        } else if (strcmp(name, "__pm_metal_image_end") == 0) {
            if (!bctx->exe_bounds_ready) {
                pm_build_exec_ranges_load();
            }
            if (bctx->exe_lo != 0 && bctx->exe_hi > bctx->exe_lo) {
                return (void *)bctx->exe_hi;
            }
        }
        return NULL;
    }
    if (ctx == NULL || ctx->thunk_base == NULL) {
        return p;
    }
    if (!pm_build_addr_is_code((uintptr_t)p)) {
        return p;
    }
    {
        ptrdiff_t delta = (uint8_t *)p - (ctx->thunk_base + ctx->thunk_used);
        /* PLT32 carries a signed 32-bit displacement; keep a wide margin. */
        if (delta > (ptrdiff_t)INT32_MIN / 2 && delta < (ptrdiff_t)INT32_MAX / 2) {
            return p;
        }
    }
    if (ctx->thunk_used >= (size_t)PM_BUILD_THUNK_SLOTS * PM_BUILD_THUNK_BYTES) {
        ctx->failed = 1;
        return NULL;
    }
    {
        uint8_t *t = ctx->thunk_base + ctx->thunk_used;
        /* movabs $target, %rax ; jmp *%rax */
        t[0] = 0x48; t[1] = 0xb8;
        memcpy(t + 2, &p, sizeof(p));
        t[10] = 0xff; t[11] = 0xe0;
        ctx->thunk_used += PM_BUILD_THUNK_BYTES;
        return t;
    }
}
#endif /* PM_METAL_BUILD_HAS_ELF */

#if defined(PM_METAL_BUILD_WASM_LINK)
/* wasm route: each object IS a wasm module (the jit.c wasm32 object path —
 * wasm-native or cross-compiled). "Linking" = load every module through
 * the loader, which instantiates it and publishes its named exports into
 * the registry — the software-defined linker. Cross-module calls resolve
 * through the registry (connect_import), not through a merged image. */
static int32_t link_wasm(pm_util_mem_arena_t *arena,
    const pm_metal_build_unit_t *unit, uint8_t **objects, const size_t *lens,
    uint32_t n_objects, pm_metal_build_artifact_t *artifact,
    char *errbuf, size_t errbuf_len) {
    uint32_t i;
    (void)arena;
    if (arena == NULL || unit == NULL || objects == NULL || lens == NULL
        || n_objects == 0 || artifact == NULL) {
        err_set(errbuf, errbuf_len, "link: bad args", 0);
        return PM_METAL_BUILD_ERR_LINK;
    }
    if (n_objects > PM_METAL_BUILD_MAX_OBJS) {
        err_set(errbuf, errbuf_len, "link: too many objects", 0);
        return PM_METAL_BUILD_ERR_LINK;
    }
    memset(artifact, 0, sizeof(*artifact));
    snprintf(artifact->fqn, sizeof(artifact->fqn), "%s", unit->fqn);
    artifact->is_wasm = 1;
    /* rebuild contract (same shape as the ELF seat's munmap): destroy the
     * previous artifact before linking a new one — the loader publishes
     * under the unit fqn and a live previous module would shadow it. */
    for (i = 0; i < n_objects; i++) {
        pm_wasmmod_registry_handle_t h;
        if (objects[i] == NULL || lens[i] == 0) {
            err_set(errbuf, errbuf_len, "link: empty object", 0);
            pm_metal_build_artifact_destroy(artifact);
            return PM_METAL_BUILD_ERR_LINK;
        }
        h = pm_wasmmod_loader_load(
            (const uint8_t *)unit->fqn, (uint32_t)strlen(unit->fqn),
            objects[i], (uint32_t)lens[i]);
        if (h.index == UINT32_MAX) {
            char msg[PM_METAL_BUILD_ERR_MAX];
            snprintf(msg, sizeof(msg), "link: loader refused object %u", (unsigned)i);
            err_set(errbuf, errbuf_len, msg, 0);
            pm_metal_build_artifact_destroy(artifact);
            return PM_METAL_BUILD_ERR_LINK;
        }
        artifact->loader_handles[i] = h;
        artifact->n_loader_handles++;
    }
    return PM_METAL_BUILD_OK;
}
#endif /* PM_METAL_BUILD_WASM_LINK */

#if defined(PM_METAL_BUILD_HAS_ELF) || defined(PM_METAL_BUILD_ELF_LINK)
static int32_t link_elf(pm_util_mem_arena_t *arena,
    const pm_metal_build_unit_t *unit, uint8_t **objects, const size_t *lens,
    uint32_t n_objects, pm_metal_build_artifact_t *artifact,
    char *errbuf, size_t errbuf_len) {
#ifdef PM_METAL_BUILD_HAS_ELF
    mp_wasm_elf_image_t *img = NULL;
    char err[PM_METAL_BUILD_ERR_MAX];
    uint32_t i;
    uint32_t *lens32;
    pm_build_resolve_ctx_t rctx;

    if (arena == NULL || unit == NULL || objects == NULL || lens == NULL
        || n_objects == 0 || artifact == NULL) {
        err_set(errbuf, errbuf_len, "link: bad args", 0);
        return PM_METAL_BUILD_ERR_LINK;
    }
    /* the multi loader takes uint32 lens; our artifacts are small */
    lens32 = (uint32_t *)pm_util_mem_alloc(arena, n_objects * sizeof(uint32_t));
    if (lens32 == NULL) {
        err_set(errbuf, errbuf_len, "link: arena alloc failed", 0);
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    for (i = 0; i < n_objects; i++) {
        lens32[i] = (uint32_t)lens[i];
    }
    memset(&rctx, 0, sizeof(rctx));
    /* The per-build ctx must exist: the resolver's thunk decision and the
     * weak image symbols read exec-range state that lives on it. Without
     * a ctx the resolver would silently skip thunking and the image would
     * jump through truncated addresses — refuse instead. */
    if (build_ctx_acquire() == NULL) {
        err_set(errbuf, errbuf_len,
            "link: no build ctx (seat never booted modules)", 0);
        return PM_METAL_BUILD_ERR_LINK;
    }
    thunk_ctx_init(&rctx);
    if (!mp_wasm_elf_image_load_multi((const uint8_t *const *)objects, lens32,
        n_objects, proc_resolve, &rctx, &img, err, sizeof(err))) {
        thunk_ctx_deinit(&rctx);
        err_set(errbuf, errbuf_len, err, 0);
        return PM_METAL_BUILD_ERR_LINK;
    }
    /* The image is mmap'd (not arena memory): publish it through the
     * artifact so the caller can lookup and free it. The thunk table is
     * leaked deliberately: the image's code points into it, so it must
     * outlive the artifact. */
    memset(artifact, 0, sizeof(*artifact));
    snprintf(artifact->fqn, sizeof(artifact->fqn), "%s", unit->fqn);
    artifact->bytes = (uint8_t *)img;
    artifact->len = img->size;
    artifact->is_wasm = 0;
    return PM_METAL_BUILD_OK;
#else
    (void)arena; (void)unit; (void)objects; (void)lens; (void)n_objects;
    (void)artifact;
    err_set(errbuf, errbuf_len, "link: no ELF loader on this seat", 0);
    return PM_METAL_BUILD_ERR_LINK;
#endif
}
#endif /* PM_METAL_BUILD_HAS_ELF || PM_METAL_BUILD_ELF_LINK */

int32_t pm_metal_build_link(pm_util_mem_arena_t *arena,
    const pm_metal_build_unit_t *unit, uint8_t **objects, const size_t *lens,
    uint32_t n_objects, pm_metal_build_artifact_t *artifact,
    char *errbuf, size_t errbuf_len) {
#if defined(PM_METAL_BUILD_WASM_LINK) && defined(PM_METAL_BUILD_ELF_LINK)
    /* cross seat: the object's own bytes pick the route — a \0asm module
     * goes through the loader (registry publishes its exports), an ELF
     * ET_REL through the relocator. Mixed batches refuse: one artifact,
     * one target. */
    uint32_t i;
    int is_wasm;
    if (objects == NULL || lens == NULL || n_objects == 0) {
        err_set(errbuf, errbuf_len, "link: bad args", 0);
        return PM_METAL_BUILD_ERR_LINK;
    }
    is_wasm = (lens[0] >= 4 && objects[0][0] == 0x00 && objects[0][1] == 0x61
        && objects[0][2] == 0x73 && objects[0][3] == 0x6d);
    for (i = 1; i < n_objects; i++) {
        int w = (lens[i] >= 4 && objects[i][0] == 0x00 && objects[i][1] == 0x61
            && objects[i][2] == 0x73 && objects[i][3] == 0x6d);
        if (w != is_wasm) {
            err_set(errbuf, errbuf_len, "link: mixed wasm/ELF objects", 0);
            return PM_METAL_BUILD_ERR_LINK;
        }
    }
    if (is_wasm) {
        return link_wasm(arena, unit, objects, lens, n_objects, artifact,
            errbuf, errbuf_len);
    }
    return link_elf(arena, unit, objects, lens, n_objects, artifact,
        errbuf, errbuf_len);
#elif defined(PM_METAL_BUILD_WASM_LINK)
    return link_wasm(arena, unit, objects, lens, n_objects, artifact,
        errbuf, errbuf_len);
#elif defined(PM_METAL_BUILD_HAS_ELF)
    return link_elf(arena, unit, objects, lens, n_objects, artifact,
        errbuf, errbuf_len);
#else
    (void)arena; (void)unit; (void)objects; (void)lens; (void)n_objects;
    (void)artifact;
    err_set(errbuf, errbuf_len, "link: no loader on this seat", 0);
    return PM_METAL_BUILD_ERR_LINK;
#endif
}

#if defined(PM_METAL_BUILD_WASM_LINK)
/* wasm route: the module's exports live in the registry (published by the
 * loader at link time). Lookup walks the registry's export table for the
 * unit fqn; the honest handle is the sentinel 1 — calling goes through
 * pm_wasmmod_registry_call, which owns the wasm trampoline. */
static void *artifact_lookup_wasm(const pm_metal_build_artifact_t *artifact,
    const char *name) {
    if (artifact == NULL || artifact->fqn[0] == '\0' || name == NULL) {
        return NULL;
    }
    {
        uint32_t n = pm_wasmmod_registry_export_count(
            (const uint8_t *)artifact->fqn,
            (uint32_t)strlen(artifact->fqn));
        uint32_t i;
        for (i = 0; i < n; i++) {
            char ename[96];
            uint32_t elen = sizeof(ename);
            pm_wasmmod_registry_export_kind_t kind =
                PM_WASMMOD_REGISTRY_EXPORT_FN;
            if (pm_wasmmod_registry_export_at(
                    (const uint8_t *)artifact->fqn,
                    (uint32_t)strlen(artifact->fqn), i,
                    (uint8_t *)ename, &elen, &kind, NULL, 0) == 0) {
                continue;
            }
            /* copy_str_to_buf writes exactly elen bytes, no terminator —
             * a shorter name over a longer leftover would keep its tail
             * and never compare equal. */
            if (elen >= sizeof(ename)) {
                elen = (uint32_t)sizeof(ename) - 1u;
            }
            ename[elen] = '\0';
            if (strcmp(ename, name) == 0) {
                return (void *)(uintptr_t)1;
            }
        }
        return NULL;
    }
}
#endif

#if defined(PM_METAL_BUILD_HAS_ELF)
static void *artifact_lookup_elf(const pm_metal_build_artifact_t *artifact,
    const char *name) {
    if (artifact == NULL || artifact->bytes == NULL || name == NULL) {
        return NULL;
    }
    return mp_wasm_elf_lookup((const mp_wasm_elf_image_t *)artifact->bytes, name);
}
#endif

void pm_metal_build_artifact_destroy(pm_metal_build_artifact_t *artifact) {
    /* impl = py artifacts carry arena-owned mpy bytes, not a linked image —
     * every free path below assumes a native image, so py stops here. */
    if (artifact != NULL && artifact->is_mpy) {
        artifact->bytes = NULL;
        artifact->len = 0;
        return;
    }
#if defined(PM_METAL_BUILD_WASM_LINK) && defined(PM_METAL_BUILD_ELF_LINK)
    /* cross seat: both release shapes can be live across a session —
     * release whichever this artifact carries */
    if (artifact != NULL && artifact->n_loader_handles > 0) {
        uint32_t i;
        for (i = 0; i < artifact->n_loader_handles; i++) {
            (void)pm_wasmmod_loader_unload(artifact->loader_handles[i]);
        }
        artifact->n_loader_handles = 0;
    }
    if (artifact != NULL && artifact->bytes != NULL) {
        mp_wasm_elf_image_free((mp_wasm_elf_image_t *)artifact->bytes);
        artifact->bytes = NULL;
        artifact->len = 0;
    }
#elif defined(PM_METAL_BUILD_WASM_LINK)
    if (artifact != NULL && artifact->n_loader_handles > 0) {
        uint32_t i;
        for (i = 0; i < artifact->n_loader_handles; i++) {
            (void)pm_wasmmod_loader_unload(artifact->loader_handles[i]);
        }
        artifact->n_loader_handles = 0;
    }
#elif defined(PM_METAL_BUILD_HAS_ELF)
    if (artifact != NULL && artifact->bytes != NULL) {
        mp_wasm_elf_image_free((mp_wasm_elf_image_t *)artifact->bytes);
        artifact->bytes = NULL;
        artifact->len = 0;
    }
#else
    (void)artifact;
#endif
}

void *pm_metal_build_artifact_lookup(const pm_metal_build_artifact_t *artifact,
    const char *name) {
#if defined(PM_METAL_BUILD_WASM_LINK) && defined(PM_METAL_BUILD_ELF_LINK)
    if (artifact == NULL || name == NULL) {
        return NULL;
    }
    /* the artifact's own shape picks the route, same as link */
    if (artifact->is_wasm) {
        return artifact_lookup_wasm(artifact, name);
    }
    return artifact_lookup_elf(artifact, name);
#elif defined(PM_METAL_BUILD_WASM_LINK)
    return artifact_lookup_wasm(artifact, name);
#elif defined(PM_METAL_BUILD_HAS_ELF)
    return artifact_lookup_elf(artifact, name);
#else
    (void)artifact; (void)name;
    return NULL;
#endif
}

/* Call a function in a linked artifact with scalar args. args are the
 * caller's values (i64 transport; the wasm seat narrows to i32 — its C
 * ints are wasm i32), n_args their count; the result lands in *res as
 * an i64 (widened from the callee's return width). The callee's
 * arity/type contract is the caller's: this face only transports
 * scalars, it does not typecheck the target — the same posture as the C
 * feeds, which cast lookup's pointer themselves.
 *
 * Returns 0 on a completed call, negative on refusal (bad artifact, name
 * not present, or a wasm trampoline failure).
 *
 * ELF seats: lookup resolves a native code pointer and the call goes
 * through it directly (the image is already relocated and executable).
 * wasm seat: the pointer is the sentinel 1 — the honest call path is the
 * registry trampoline (pm_wasmmod_registry_call), which owns the WAMR
 * exec-env plumbing the adapter fns need. */
#if defined(PM_METAL_BUILD_WASM_LINK)
static int32_t artifact_call_wasm(const pm_metal_build_artifact_t *artifact,
    const char *name, const int64_t *args, uint32_t n_args, int64_t *res) {
    pm_wasmmod_registry_value_t wargs[8];
    pm_wasmmod_registry_value_t wres;
    uint32_t i;
    int32_t st;
    if (artifact->fqn[0] == '\0') {
        return -1;
    }
    if (pm_metal_build_artifact_lookup(artifact, name) == NULL) {
        return -2;
    }
    /* i32 spine: TCC's wasm32 C lowers int params to wasm i32, and
     * WAMR packs each arg by the caller-declared kind — an i64-kind
     * arg would hand an i32 callee two cells and misalign every
     * parameter after it. The transport widens to i64 only on the
     * way back (the result union covers both). */
    for (i = 0; i < n_args; i++) {
        wargs[i].kind = PM_WASMMOD_REGISTRY_VALKIND_I32;
        wargs[i].of.i32 = (int32_t)args[i];
    }
    wres.kind = PM_WASMMOD_REGISTRY_VALKIND_I32;
    wres.of.i32 = 0;
    st = pm_wasmmod_registry_call(
        (const uint8_t *)artifact->fqn, (uint32_t)strlen(artifact->fqn),
        (const uint8_t *)name, (uint32_t)strlen(name),
        &wargs[0], n_args, &wres, 1u);
    if (st < 0) {
        return -3;
    }
    if (res != NULL) {
        /* the trampoline reports the kind WAMR actually returned; reading
         * the i32 arm of every result would cut a long long down to its
         * low half and sign-extend that back */
        switch (wres.kind) {
        case PM_WASMMOD_REGISTRY_VALKIND_I64:
            *res = wres.of.i64;
            break;
        case PM_WASMMOD_REGISTRY_VALKIND_F32:
            *res = (int64_t)wres.of.f32;
            break;
        case PM_WASMMOD_REGISTRY_VALKIND_F64:
            *res = (int64_t)wres.of.f64;
            break;
        default:
            *res = (int64_t)wres.of.i32;
            break;
        }
    }
    return 0;
}
#endif

#if defined(PM_METAL_BUILD_HAS_ELF)
static int32_t artifact_call_elf(const pm_metal_build_artifact_t *artifact,
    const char *name, const int64_t *args, uint32_t n_args, int64_t *res) {
    void *p = pm_metal_build_artifact_lookup(artifact, name);
    if (p == NULL) {
        return -2;
    }
    /* one call shape per arity: int64_t(int64_t...) — the honest scalar
     * spine. Struct returns, variadics and pointers stay C-feed-only
     * until the bridge grows a type spine of its own. */
    switch (n_args) {
    case 0:
        if (res != NULL) {
            *res = ((int64_t (*)(void))p)();
        } else {
            ((void (*)(void))p)();
        }
        return 0;
    case 1:
        if (res != NULL) {
            *res = ((int64_t (*)(int64_t))p)(args[0]);
        } else {
            ((void (*)(int64_t))p)(args[0]);
        }
        return 0;
    case 2:
        if (res != NULL) {
            *res = ((int64_t (*)(int64_t, int64_t))p)(args[0], args[1]);
        } else {
            ((void (*)(int64_t, int64_t))p)(args[0], args[1]);
        }
        return 0;
    default:
        return -4;
    }
}
#endif

int32_t pm_metal_build_artifact_call(const pm_metal_build_artifact_t *artifact,
    const char *name, const int64_t *args, uint32_t n_args, int64_t *res) {
    if (res != NULL) {
        *res = 0;
    }
    if (artifact == NULL || name == NULL || n_args > 8u) {
        return -1;
    }
#if defined(PM_METAL_BUILD_WASM_LINK) && defined(PM_METAL_BUILD_ELF_LINK)
    /* cross seat: the artifact's own shape picks the route, same as link */
    if (artifact->is_wasm) {
        return artifact_call_wasm(artifact, name, args, n_args, res);
    }
    return artifact_call_elf(artifact, name, args, n_args, res);
#elif defined(PM_METAL_BUILD_WASM_LINK)
    return artifact_call_wasm(artifact, name, args, n_args, res);
#elif defined(PM_METAL_BUILD_HAS_ELF)
    return artifact_call_elf(artifact, name, args, n_args, res);
#else
    (void)args;
    return -5;
#endif
}

#include "pymergetic/wasmmod/guest.h"

/*------------------ retained objects (the downloadable product) ----------
 * The cache the header describes: one arena over a malloc'd span, a flat
 * slot table, and a reset when the next keep does not fit. Nothing here
 * can fail a build — a seat whose malloc refuses simply retains nothing,
 * and pm_metal_build_object_count answers 0 there like anywhere else. */

typedef struct build_keep_obj {
    char fqn[PM_METAL_BUILD_STR_MAX];
    char src[PM_METAL_BUILD_MAX_SRC_PATH];
    const uint8_t *bytes;
    uint32_t len;
} build_keep_obj_t;

static build_keep_obj_t *s_keep;
static uint32_t s_keep_cap;
static uint32_t s_n_keep;
static void *s_keep_backing;
static pm_util_mem_arena_t *s_keep_arena;
/* One lock over every field above. A walk runs PM_METAL_BUILD_WALK_FANOUT
 * jobs at once on coop runners, so several units finish on different cores
 * and all of them land here; the pane's download reads land here too. The
 * first version of this cache was unguarded and re-created its arena when
 * the span filled, which let two finishing jobs tlsf_destroy the same arena
 * and abort the seat mid-build ("block already marked as free"). BSS zero is
 * the unlocked state, same contract as the ctx lock. */
static pm_util_lock_t s_keep_lock;

/* Does this lane emit for an arch this seat cannot load?
 *
 * Not "target != 0": on the wasm32 seat the wasm32 lane IS the seat's
 * backend and its module loads here, and on an x86-64 seat the x86-64 lane
 * names the seat's own arch. The question is only whether the emitted
 * object's arch matches the one this binary's loader relocates. */
static int build_target_is_cross(int32_t target) {
    const char *seat = pm_metal_jit_c_target_arch(
        (int32_t)PM_METAL_JIT_C_TARGET_SEAT);
    const char *want = pm_metal_jit_c_target_arch(target);
    if (target == (int32_t)PM_METAL_JIT_C_TARGET_SEAT) {
        return 0;
    }
    return strcmp(seat, want) != 0;
}

/* The keep and cache knobs: how many retained objects the seat serves and how
 * much memory it may hold them in. Both are defaults a seat moves:
 * a build host with a bigger tree wants more of both, a small seat would
 * rather have the 64 MiB back and serve only the last few downloads.
 *
 * The slot table lives in the cache's own span, so the whole cache is one
 * block the seat asked for and nothing here is reserved until a build
 * actually retains something. */
static int32_t keep_slots_apply(pm_util_limit_t *knob);
static int32_t cache_apply(pm_util_limit_t *knob);
PM_UTIL_LIMIT_APPLY_C(pm_build_limit_keep, pymergetic.metal.build, keep,
    PM_METAL_BUILD_KEEP_DEFAULT, 0u, &s_n_keep, keep_slots_apply);
PM_UTIL_LIMIT_APPLY_C(pm_build_limit_cache, pymergetic.metal.build, cache,
    PM_METAL_BUILD_KEEP_SPAN_DEFAULT, 0u, NULL, cache_apply);

static uint32_t keep_want(const pm_util_limit_t *knob) {
    return knob->soft != 0u ? knob->soft : knob->dflt;
}

/* Free one slot's bytes and blank it. Caller holds s_keep_lock. */
static void build_keep_drop(uint32_t i) {
    if (s_keep[i].bytes != NULL && s_keep_arena != NULL) {
        pm_util_mem_free(s_keep_arena, (void *)s_keep[i].bytes);
    }
    memset(&s_keep[i], 0, sizeof(s_keep[i]));
}

/* Remove slot i, keeping the rest in age order (oldest first). */
static void build_keep_evict(uint32_t i) {
    build_keep_drop(i);
    if (i + 1u < s_n_keep) {
        memmove(&s_keep[i], &s_keep[i + 1u],
            (size_t)(s_n_keep - i - 1u) * sizeof(s_keep[0]));
    }
    s_n_keep--;
    memset(&s_keep[s_n_keep], 0, sizeof(s_keep[0]));
}

/* The cache, at the span and slot count the knobs ask for right now. Taken on
 * the first retained object and not before: a seat that never builds pays
 * nothing. Caller holds s_keep_lock. Answers 0 when the seat will not lend the
 * span — then nothing is downloadable and builds carry on regardless. */
static int keep_ready(void) {
    size_t span;
    if (s_keep_arena != NULL && s_keep != NULL) {
        return 1;
    }
    if (s_keep_backing == NULL) {
        span = (size_t)keep_want(&pm_build_limit_cache);
        s_keep_backing = malloc(span);
        if (s_keep_backing == NULL) {
            return 0;
        }
        s_keep_arena = pm_util_mem_arena_create(s_keep_backing, span);
        if (s_keep_arena == NULL) {
            free(s_keep_backing);
            s_keep_backing = NULL;
            return 0;
        }
    }
    if (s_keep == NULL) {
        uint32_t cap = keep_want(&pm_build_limit_keep);
        s_keep = pm_util_mem_alloc(s_keep_arena, (size_t)cap * sizeof(*s_keep));
        if (s_keep == NULL) {
            return 0;
        }
        memset(s_keep, 0, (size_t)cap * sizeof(*s_keep));
        s_keep_cap = cap;
        s_n_keep = 0;
    }
    return 1;
}

/* The keep knob moved: the same retained objects in a table of the new depth.
 * A table that got smaller lets the oldest downloads go, which is what the
 * cache does when it runs out of room anyway. */
static int32_t keep_slots_apply(pm_util_limit_t *knob) {
    uint32_t cap = keep_want(knob);
    build_keep_obj_t *next;
    int32_t rc = 0;
    pm_util_lock_acquire(&s_keep_lock);
    if (s_keep == NULL) {
        pm_util_lock_release(&s_keep_lock);
        return 0; /* nothing taken yet; the first keep reads the knob */
    }
    if (cap != s_keep_cap) {
        next = pm_util_mem_alloc(s_keep_arena, (size_t)cap * sizeof(*next));
        if (next == NULL) {
            rc = -1;
        } else {
            while (s_n_keep > cap) {
                build_keep_evict(0);
            }
            memset(next, 0, (size_t)cap * sizeof(*next));
            memcpy(next, s_keep, (size_t)s_n_keep * sizeof(*next));
            pm_util_mem_free(s_keep_arena, s_keep);
            s_keep = next;
            s_keep_cap = cap;
        }
    }
    pm_util_lock_release(&s_keep_lock);
    return rc;
}

/* The cache knob moved: the cache is the span, so it is let go and taken
 * again at the new size on the next retained object. Everything downloadable
 * right now is dropped — the seat asked for a different cache. */
static int32_t cache_apply(pm_util_limit_t *knob) {
    (void)knob;
    pm_util_lock_acquire(&s_keep_lock);
    if (s_keep_backing != NULL) {
        while (s_n_keep > 0) {
            build_keep_evict(0);
        }
        if (s_keep_arena != NULL) {
            pm_util_mem_arena_destroy(s_keep_arena);
            s_keep_arena = NULL;
        }
        free(s_keep_backing);
        s_keep_backing = NULL;
        s_keep = NULL;
        s_keep_cap = 0;
    }
    pm_util_lock_release(&s_keep_lock);
    return 0;
}

/* Copy one unit's objects into the cache. Best effort: called after the
 * compile succeeded, so a refusal here costs a download, never a build.
 *
 * The arena is created once and never destroyed. Entries are freed one at a
 * time instead, because the span used to be reset wholesale: a whole BUILD
 * ALL is 18 MiB over 103 objects (measured), so every extra round re-added
 * that much until the span wrapped and all but the last few units lost their
 * download. Freeing per entry keeps the steady state at one round's worth. */
static void build_keep_objects(const char *fqn, const char **srcs,
    uint8_t *const *objs, const size_t *lens, uint32_t n) {
    uint32_t i;

    if (fqn == NULL || n == 0) {
        return;
    }
    pm_util_lock_acquire(&s_keep_lock);
    if (!keep_ready()) {
        /* no span on this seat: nothing is downloadable, builds are fine */
        pm_util_lock_release(&s_keep_lock);
        return;
    }
    /* A rebuild of the same unit replaces its slots rather than stacking a
     * second copy: the newest bytes are the ones a download should serve. */
    for (i = 0; i < s_n_keep;) {
        if (strcmp(s_keep[i].fqn, fqn) == 0) {
            build_keep_evict(i);
        } else {
            i++;
        }
    }
    for (i = 0; i < n; i++) {
        uint8_t *copy;
        if (objs[i] == NULL || lens[i] == 0) {
            continue;
        }
        /* Make room by age: the oldest unit's objects go first, so a seat
         * that cannot hold the whole tree still keeps a contiguous recent
         * window rather than losing everything at once. */
        while (s_n_keep >= s_keep_cap) {
            build_keep_evict(0);
        }
        copy = (uint8_t *)pm_util_mem_alloc(s_keep_arena, lens[i]);
        while (copy == NULL && s_n_keep > 0) {
            build_keep_evict(0);
            copy = (uint8_t *)pm_util_mem_alloc(s_keep_arena, lens[i]);
        }
        if (copy == NULL) {
            break;    /* one object larger than the whole span */
        }
        memcpy(copy, objs[i], lens[i]);
        snprintf(s_keep[s_n_keep].fqn, sizeof(s_keep[s_n_keep].fqn), "%s", fqn);
        snprintf(s_keep[s_n_keep].src, sizeof(s_keep[s_n_keep].src), "%s",
            (srcs != NULL && srcs[i] != NULL) ? srcs[i] : "object");
        s_keep[s_n_keep].bytes = copy;
        s_keep[s_n_keep].len = (uint32_t)lens[i];
        s_n_keep++;
    }
    pm_util_lock_release(&s_keep_lock);
}

uint32_t pm_metal_build_object_count(const char *fqn) {
    uint32_t i;
    uint32_t n = 0;
    if (fqn == NULL) {
        return 0;
    }
    pm_util_lock_acquire(&s_keep_lock);
    for (i = 0; i < s_n_keep; i++) {
        if (strcmp(s_keep[i].fqn, fqn) == 0) {
            n++;
        }
    }
    pm_util_lock_release(&s_keep_lock);
    return n;
}
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_object_count,
    pm_metal_build_object_count, uint32_t(const char *));

/* Name and size of one retained object. Copies the name out: the slot it
 * lives in can be evicted by the next build. */
int32_t pm_metal_build_object_info(const char *fqn, uint32_t idx,
    char *src_out, uint32_t src_max, uint32_t *len_out) {
    uint32_t i;
    uint32_t seen = 0;
    int32_t rc = PM_METAL_BUILD_ERR_PARSE;
    if (fqn == NULL) {
        return PM_METAL_BUILD_ERR_PARSE;
    }
    pm_util_lock_acquire(&s_keep_lock);
    for (i = 0; i < s_n_keep; i++) {
        if (strcmp(s_keep[i].fqn, fqn) != 0 || seen++ != idx) {
            continue;
        }
        if (src_out != NULL && src_max > 0) {
            snprintf(src_out, src_max, "%s", s_keep[i].src);
        }
        if (len_out != NULL) {
            *len_out = s_keep[i].len;
        }
        rc = PM_METAL_BUILD_OK;
        break;
    }
    pm_util_lock_release(&s_keep_lock);
    return rc;
}
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_object_info,
    pm_metal_build_object_info,
    int32_t(const char *, uint32_t, char *, uint32_t, uint32_t *));

/* Copy one window of a retained object into the caller's buffer.
 *
 * A window rather than a pointer on purpose: a download is many requests and
 * a concurrent build can evict the slot between any two of them, so handing
 * out the arena pointer would let a route read freed bytes. Copying under
 * the lock makes the worst case an honest short read. */
int32_t pm_metal_build_object_read(const char *fqn, uint32_t idx,
    uint32_t off, uint8_t *out, uint32_t out_max, uint32_t *n_out) {
    uint32_t i;
    uint32_t seen = 0;
    int32_t rc = PM_METAL_BUILD_ERR_PARSE;
    if (fqn == NULL || out == NULL) {
        return PM_METAL_BUILD_ERR_PARSE;
    }
    pm_util_lock_acquire(&s_keep_lock);
    for (i = 0; i < s_n_keep; i++) {
        uint32_t n = 0;
        if (strcmp(s_keep[i].fqn, fqn) != 0 || seen++ != idx) {
            continue;
        }
        if (off < s_keep[i].len) {
            n = s_keep[i].len - off;
            if (n > out_max) {
                n = out_max;
            }
            memcpy(out, s_keep[i].bytes + off, n);
        }
        if (n_out != NULL) {
            *n_out = n;
        }
        rc = PM_METAL_BUILD_OK;
        break;
    }
    pm_util_lock_release(&s_keep_lock);
    return rc;
}
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_object_read,
    pm_metal_build_object_read,
    int32_t(const char *, uint32_t, uint32_t, uint8_t *, uint32_t, uint32_t *));

/*------------------ runtime card discovery (Phase 4.4) ------------------
 * The embedded card table (tools/embed_src.py -> src_embed.inc.h, included
 * by the inspect card on every seat) is the source of truth: each entry
 * carries the card's impl and raw __pmm__.toml bytes, so discovery is pure
 * data — no filesystem walk, identical on every seat. */

/* Faces a root TU inlines through `#[path]` (declared here so discover,
 * which lives above the splice engine, can list only what a unit really
 * compiles; the full struct docs ride the definition below). */
typedef struct rs_splice_inc {
    const char *rel[32];
    uint32_t n;
} rs_splice_inc_t;

static char *rs_splice(pm_util_mem_arena_t *arena, const char *fqn,
    const char *src, char *errbuf, size_t errbuf_len, rs_splice_inc_t *inc);

int32_t pm_metal_build_discover(pm_util_mem_arena_t *arena,
    pm_metal_build_unit_t **units, uint32_t *n_units,
    char *errbuf, size_t errbuf_len) {
    uint32_t n = pm_metal_src_card_count();
    pm_metal_build_unit_t *out;
    uint32_t i;
    uint32_t w = 0;

    if (arena == NULL || units == NULL || n_units == NULL) {
        err_set(errbuf, errbuf_len, "discover: bad args", 0);
        return PM_METAL_BUILD_ERR_PARSE;
    }
    *units = NULL;
    *n_units = 0;
    if (n == 0) {
        return PM_METAL_BUILD_OK;
    }
    out = (pm_metal_build_unit_t *)pm_util_mem_alloc(
        arena, n * sizeof(pm_metal_build_unit_t));
    if (out == NULL) {
        err_set(errbuf, errbuf_len, "discover: arena exhausted", 0);
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    for (i = 0; i < n; i++) {
        const pm_metal_src_card_t *c = &PM_METAL_SRC_CARDS[i];
        char err[PM_METAL_BUILD_ERR_MAX];
        if (c->toml == NULL) {
            continue;
        }
        if (pm_metal_build_unit_parse(arena, (const uint8_t *)c->toml,
            strlen(c->toml), &out[w], err, sizeof(err)) != PM_METAL_BUILD_OK) {
            continue;  /* a broken manifest is skipped, not fatal */
        }
        /* sources: the card's embedded muscle file names (the parse filled
         * everything else; a card unit has no extra relative paths).
         * `__flat__.rs` is an embed-only synthetic — the parts' assembled
         * TU the inspect tests read; it is not a compilable unit (the
         * parts ride the root shim's #[path] splice, the flat would
         * duplicate the same symbols). An rs root inlines its #[path]
         * faces at compile (rs_splice); those faces are not standalone
         * TUs either, so discover lists only what a unit compiles. */
        {
            const char **srcs = (const char **)pm_util_mem_alloc(
                arena, c->nfiles * sizeof(const char *));
            uint32_t f;
            uint32_t n_srcs = 0;
            rs_splice_inc_t inc;
            if (srcs == NULL) {
                err_set(errbuf, errbuf_len, "discover: arena exhausted", 0);
                return PM_METAL_BUILD_ERR_NOMEM;
            }
            memset(&inc, 0, sizeof(inc));
            if (strcmp(out[w].impl, "rs") == 0) {
                const char *root = NULL;
                for (f = 0; f < c->nfiles; f++) {
                    if (strcmp(c->files[f].rel, "__impl__.rs") == 0) {
                        root = (const char *)c->files[f].data;
                        break;
                    }
                }
                if (root != NULL) {
                    char serr[PM_METAL_BUILD_ERR_MAX];
                    char *sp = rs_splice(arena, out[w].fqn, root, serr, sizeof(serr),
                        &inc);
                    if (sp == NULL) {
                        /* the unit compile reports splice errors honestly;
                         * discover lists the raw files and lets it. */
                        memset(&inc, 0, sizeof(inc));
                    } else {
                        /* the splice only computed the inline set (inc);
                         * the buffer itself is scratch this pass drops. */
                        pm_util_mem_free(arena, sp);
                    }
                }
            }
            for (f = 0; f < c->nfiles; f++) {
                uint32_t k;
                int inlined = 0;
                if (strcmp(c->files[f].rel, "__flat__.rs") == 0) {
                    continue;
                }
                for (k = 0; k < inc.n; k++) {
                    if (strcmp(inc.rel[k], c->files[f].rel) == 0) {
                        inlined = 1;
                        break;
                    }
                }
                if (inlined) {
                    continue;
                }
                srcs[n_srcs] = c->files[f].rel;
                n_srcs++;
            }
            out[w].sources = srcs;
            out[w].n_sources = n_srcs;
        }
        w++;
    }
    *units = out;
    *n_units = w;
    return PM_METAL_BUILD_OK;
}

/* ---- rsx `#[path]` splice ----
 *
 * rsx compiles a card standalone: one .rs in, one C unit out. A card that
 * needs another card's ABI shapes (wasmmod.api using the registry's Value
 * convention) reaches them through `#[path = ".."] mod NAME;` — cargo's
 * own module include. The kernel's rsx has no filesystem, so the build
 * face resolves those includes against the embedded card tree and
 * splices the file bytes into the unit before compiling: the rsx
 * equivalent of a C `#include`, resolved the same way (bytes in the
 * image, no second copy anywhere). One level of nesting: an included
 * face may itself path-include (depth-capped, refuse deeper honestly).
 */

#define PM_BUILD_SPLICE_MAX_DEPTH 8u

static char *fqn_to_dir(pm_util_mem_arena_t *arena, const char *fqn) {
    size_t n = fqn != NULL ? strlen(fqn) : 0u;
    char *out = (char *)pm_util_mem_alloc(arena, n + 1u);
    size_t i;
    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        out[i] = fqn[i] == '.' ? '/' : fqn[i];
    }
    out[n] = '\0';
    return out;
}

/* dir/file with `..` and `.` folded; returns the normalized path (arena).
 * Kept segments are [start,end) spans in the joined buffer; `..` pops the
 * last kept span, an empty stack refuses (no path escapes the tree root). */
static char *norm_join(pm_util_mem_arena_t *arena, const char *dir,
    const char *rel) {
    size_t dl = dir != NULL ? strlen(dir) : 0u;
    size_t rl = rel != NULL ? strlen(rel) : 0u;
    char *buf = (char *)pm_util_mem_alloc(arena, dl + rl + 4u);
    size_t starts[32];
    size_t ends[32];
    size_t n = 0u;
    size_t i = 0u;
    if (buf == NULL) {
        return NULL;
    }
    if (dl > 0u) {
        memcpy(buf, dir, dl);
        buf[dl] = '/';
        memcpy(buf + dl + 1u, rel, rl);
        buf[dl + 1u + rl] = '\0';
    } else {
        memcpy(buf, rel, rl);
        buf[rl] = '\0';
    }
    while (buf[i] != '\0') {
        size_t start = i;
        while (buf[i] != '\0' && buf[i] != '/') {
            i++;
        }
        {
            size_t len = i - start;
            if (len == 1u && buf[start] == '.') {
                /* skip */
            } else if (len == 2u && buf[start] == '.' && buf[start + 1u] == '.') {
                if (n == 0u) {
                    return NULL;  /* above the tree root — refuse */
                }
                n--;
            } else if (len > 0u) {
                if (n < 32u) {
                    starts[n] = start;
                    ends[n] = i;
                    n++;
                }
            }
        }
        if (buf[i] == '/') {
            i++;
        }
    }
    {
        char *out = (char *)pm_util_mem_alloc(arena, dl + rl + 4u);
        size_t o = 0u;
        size_t s;
        if (out == NULL) {
            return NULL;
        }
        for (s = 0u; s < n; s++) {
            if (o > 0u) {
                out[o++] = '/';
            }
            memcpy(out + o, buf + starts[s], ends[s] - starts[s]);
            o += ends[s] - starts[s];
        }
        out[o] = '\0';
        return out;
    }
}

/* Append every `#[path = ".."] mod NAME;`-included face to *out (grown in
 * the arena). Depth-capped recursion. Returns 0 ok, nonzero refuse. */
/* An attribute line guards a test-only item when its cfg mentions test
 * (`#[cfg(test)]`, `#[cfg(all(test, ...))]`, ...). Loose on purpose: the
 * authored tree never writes a non-test cfg on a `#[path]` mod. */
static int rs_attr_is_test(const char *ls, size_t len) {
    size_t i;
    if (build_memfind(ls, len, "cfg(") == NULL) {
        return 0;
    }
    for (i = 0u; i + 4u <= len; i++) {
        if (memcmp(ls + i, "test", 4u) == 0
            && (i == 0u
                || !(ls[i - 1u] == '_' || ls[i - 1u] == '"'
                    || (ls[i - 1u] >= 'a' && ls[i - 1u] <= 'z')
                    || (ls[i - 1u] >= 'A' && ls[i - 1u] <= 'Z')
                    || (ls[i - 1u] >= '0' && ls[i - 1u] <= '9')))) {
            return 1;
        }
    }
    return 0;
}

/* An attribute line is ANY cfg guard (`#[cfg(..)]`). A cfg-guarded mod
 * decl must NOT be spliced: the attr rides through ahead of the decl and
 * rsx evaluates it against the kernel's feature set (empty — every
 * feature cfg is false there), dropping the decl whole. Splicing would
 * inline the file bytes after the attr, which then guards only the
 * file's FIRST item — the rest of a `#[cfg(feature="gen")] mod host;`
 * face (std::fs, toml, Path) would compile unguarded into a unit that
 * must build freestanding. `#[cfg(test)]` is the same rule, already
 * covered by test_guard. */
static int rs_attr_is_cfg(const char *ls, size_t len) {
    if (ls[0] != '#' || ls[1] != '[') {
        return 0;
    }
    return build_memfind(ls, len, "cfg(") != NULL;
}

/* ---- `use crate::...` chase ----
 *
 * A `use crate::<path>::{...}` import names another card's exported ABI
 * (functions the muscle calls, types the signatures ride on). The rsx
 * compile is standalone, so the callee's declarations arrive the same
 * way `#[path]` includes do: by splice. The referenced card's
 * `__types__.rs` (real ABI shapes) then `__exports__.rs` (the generated
 * consumer bindgen face — `unsafe extern "C" { pub fn ..; }` decls) are
 * appended once per referenced card, depth-capped. Byte faces from the
 * embed, one definition everywhere, no second copy of any declaration. */
#define PM_BUILD_SPLICE_MAX_CARDS 24u

typedef struct rs_splice_cards {
    char names[PM_BUILD_SPLICE_MAX_CARDS][48u];
    uint32_t n;
} rs_splice_cards_t;

static int rs_splice_card_seen(const rs_splice_cards_t *set, const char *name) {
    uint32_t i;
    for (i = 0u; i < set->n; i++) {
        if (strcmp(set->names[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int rs_splice_card_note(rs_splice_cards_t *set, const char *name) {
    size_t n;
    if (set->n >= PM_BUILD_SPLICE_MAX_CARDS) {
        return 0;
    }
    n = strlen(name);
    if (n >= sizeof(set->names[0])) {
        return 0;
    }
    memcpy(set->names[set->n], name, n + 1u);
    set->n++;
    return 1;
}

/* Does `p` (line start) open a `use crate::...;` import? Returns 1 and
 * advances `*at_io` past the terminating ';' (the full raw span, which
 * may span lines when the import list is brace-formatted), 0 otherwise.
 * `use` must be a word on its own — `reuse`, `fused`, attribute tails
 * and string contents never match. */
/* Line-start `pub mod NAME;` / `mod NAME;` — cargo resolves the module to
 * a sibling `NAME.rs` (same dir as this file); the in-kernel splice does
 * the same against the embedded card tree, so a multi-module card (util.gen's
 * impl + sink.rs/cli.rs/discover.rs/host.rs) compiles as ONE rsx unit the
 * way cargo compiles one crate. Guards: cfg(test)-guarded mod decls are
 * skipped by the caller's test_guard pass; `mod NAME { .. }` (inline body)
 * does not end in `;` and is left alone. Returns 1 with [name_io, name_len_io)
 * the module ident and at_io just past the `;`. */
static int rs_mod_decl_starts(const char *p, const char **name_io,
    size_t *name_len_io, const char **at_io) {
    const char *q = p;
    const char *name;
    size_t nlen;
    /* optional `pub ` (plain or pub(crate)/pub(super)/pub(in ..) form —
     * anything starting with `pub` followed by ws or `(`) */
    if (q[0] == 'p' && q[1] == 'u' && q[2] == 'b'
        && (q[3] == ' ' || q[3] == '\t' || q[3] == '(')) {
        if (q[3] == '(') {
            /* visibility scope: skip to the matching ')' */
            int depth = 0;
            q += 3;
            while (*q != '\0') {
                if (*q == '(') depth++;
                else if (*q == ')') { depth--; if (depth == 0) { q++; break; } }
                q++;
            }
        } else {
            q += 3;
        }
        while (*q == ' ' || *q == '\t') {
            q++;
        }
    }
    if (!(q[0] == 'm' && q[1] == 'o' && q[2] == 'd')) {
        return 0;
    }
    q += 3;
    if (*q != ' ' && *q != '\t') {
        return 0;
    }
    while (*q == ' ' || *q == '\t') {
        q++;
    }
    /* ident: [A-Za-z_][A-Za-z0-9_]* (raw idents r#NAME strip the prefix) */
    if (q[0] == 'r' && q[1] == '#') {
        q += 2;
    }
    name = q;
    if (!( (*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || *q == '_' )) {
        return 0;
    }
    while ( (*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z')
        || (*q >= '0' && *q <= '9') || *q == '_') {
        q++;
    }
    nlen = (size_t)(q - name);
    while (*q == ' ' || *q == '\t') {
        q++;
    }
    if (*q != ';') {
        return 0;  /* `mod NAME { .. }` inline body — not a file module */
    }
    *name_io = name;
    *name_len_io = nlen;
    *at_io = q + 1;
    return 1;
}

static int rs_use_crate_starts(const char *p, const char **at_io) {
    const char *q = p;
    if (!(q[0] == 'u' && q[1] == 's' && q[2] == 'e')) {
        return 0;
    }
    q += 3;
    if (*q != ' ' && *q != '\t') {
        return 0;
    }
    while (*q == ' ' || *q == '\t') {
        q++;
    }
    if (!(q[0] == 'c' && q[1] == 'r' && q[2] == 'a' && q[3] == 't'
            && q[4] == 'e')) {
        return 0;
    }
    q += 5;
    if (!(*q == ':' && q[1] == ':' && q[2] != ':')) {
        return 0;
    }
    q += 2;
    /* span to the terminating ';' (multi-line brace lists included) */
    {
        const char *e = q;
        while (*e != '\0' && *e != ';') {
            e++;
        }
        if (*e == '\0') {
            return 0;
        }
        *at_io = e + 1;
    }
    return 1;
}

/* Faces the splice inlined into a unit's root TU (mod-decl chases and
 * #[path] includes of the SAME card). The unit compile skips exactly
 * these as standalone TUs: they are already in the root's crate TU, and
 * a second object would collide on every symbol at link. A companion
 * the root does NOT reference (a cfg(test) bench) stays its own TU.
 * (typedef declared above discover; this is its doc home.) */
static int32_t rs_splice_inc_add(pm_util_mem_arena_t *arena,
    rs_splice_inc_t *inc, const char *rel) {
    uint32_t i;
    if (inc == NULL || rel == NULL) {
        return PM_METAL_BUILD_OK;
    }
    for (i = 0; i < inc->n; i++) {
        if (strcmp(inc->rel[i], rel) == 0) {
            return PM_METAL_BUILD_OK;  /* already recorded */
        }
    }
    if (inc->n >= 32u) {
        return PM_METAL_BUILD_OK;  /* cap: deeper trees keep compiling solo */
    }
    inc->rel[inc->n] = dup_str(arena, rel, strlen(rel));
    if (inc->rel[inc->n] == NULL) {
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    inc->n++;
    return PM_METAL_BUILD_OK;
}

/* Append one embedded face's bytes to the splice buffer. */
static int32_t rs_splice_face(pm_util_mem_arena_t *arena,
    const pm_metal_src_file_t *face, char **buf_io, size_t *len_io,
    char *errbuf, size_t errbuf_len) {
    char *nb;
    if (face == NULL) {
        err_set(errbuf, errbuf_len, "splice: face not in embed", 0);
        return PM_METAL_BUILD_ERR_PARSE;
    }
    nb = (char *)pm_util_mem_alloc(arena, *len_io + (size_t)face->len + 4u);
    if (nb == NULL) {
        err_set(errbuf, errbuf_len, "splice: arena exhausted", 0);
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    memcpy(nb, *buf_io, *len_io);
    memcpy(nb + *len_io, face->data, (size_t)face->len);
    *len_io += (size_t)face->len;
    nb[*len_io] = '\n';
    (*len_io)++;
    nb[*len_io] = '\0';
    *buf_io = nb;
    return PM_METAL_BUILD_OK;
}

/* Find a card file by rel in EITHER table: `mod NAME;` names a muscle
 * companion (sink.rs — FILES, the authored sources), while #[path] and
 * use-crate faces are the generated ABI faces (FACES). The mod chase
 * must see both; face_find alone misses every authored companion. */
static const pm_metal_src_file_t *rs_card_file_find(
    const char *fqn, const char *rel) {
    const pm_metal_src_card_t *c = pm_metal_src_find(fqn);
    uint32_t i;
    if (c == NULL || rel == NULL) {
        return NULL;
    }
    for (i = 0u; i < c->nfaces; i++) {
        if (strcmp(c->faces[i].rel, rel) == 0) {
            return &c->faces[i];
        }
    }
    for (i = 0u; i < c->nfiles; i++) {
        if (strcmp(c->files[i].rel, rel) == 0) {
            return &c->files[i];
        }
    }
    return NULL;
}

static int32_t rs_splice_into(pm_util_mem_arena_t *arena, const char *fqn,
    const char *src, uint32_t depth, rs_splice_cards_t *seen,
    char **buf_io, size_t *len_io, char *errbuf, size_t errbuf_len,
    rs_splice_inc_t *inc);

/* Chase one `use crate::<path>...` span: find the longest `<path>`
 * prefix that names an embedded card, then splice that card's
 * `__types__.rs` + `__exports__.rs` once. `crate::x::y` maps to the
 * card fqn `pymergetic.x.y` (raw `r#` identifiers stripped). */
static int32_t rs_splice_use_crate(pm_util_mem_arena_t *arena,
    const char *span, rs_splice_cards_t *seen, uint32_t depth,
    char **buf_io, size_t *len_io, char *errbuf, size_t errbuf_len,
    rs_splice_inc_t *inc) {
    char fqn[96];
    size_t nsegs;
    const char *seg[16];
    size_t segl[16];
    const char *p = span;

    /* the span begins past `crate::` (the caller strips the `use crate::`
     * head), so segments collected here are the real card path */
    if (!(p[0] == 'c' && p[1] == 'r' && p[2] == 'a' && p[3] == 't'
            && p[4] == 'e' && p[5] == ':' && p[6] == ':')) {
        return PM_METAL_BUILD_OK;
    }
    p += 7;

    /* collect `::`-separated path segments until `{` or end */
    nsegs = 0u;
    while (*p != '\0') {
        const char *e;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            p++;
        }
        if (*p == '{' || *p == '\0') {
            break;
        }
        e = p;
        while (*e != '\0' && *e != ':' && *e != '{'
            && *e != ' ' && *e != '\t' && *e != '\n' && *e != '\r') {
            e++;
        }
        if (e == p) {
            p++;
            continue;
        }
        if (nsegs < 16u) {
            /* strip a raw-identifier `r#` prefix */
            if (e - p >= 2 && p[0] == 'r' && p[1] == '#') {
                p += 2;
            }
            seg[nsegs] = p;
            segl[nsegs] = (size_t)(e - p);
            nsegs++;
        }
        if (*e == ':') {
            if (e[1] != ':') {
                /* single ':' inside a use path — not a segment separator
                 * we understand; stop collecting (rest is ignored). */
                break;
            }
            p = e + 2;
        } else {
            p = e;
        }
    }
    if (nsegs == 0u) {
        return PM_METAL_BUILD_OK;
    }
    /* longest embedded-card prefix */
    while (nsegs > 0u) {
        size_t at = 0u, k;
        int truncated = 0;
        memcpy(fqn, "pymergetic.", 11u);
        at = 11u;
        for (k = 0u; k < nsegs; k++) {
            if (at + segl[k] + 1u >= sizeof(fqn)) {
                truncated = 1;
                break;
            }
            memcpy(fqn + at, seg[k], segl[k]);
            at += segl[k];
            if (k + 1u < nsegs) {
                fqn[at] = '.';
                at++;
            }
        }
        /* terminate before the find: pm_metal_src_find strcmps this as a
         * C string, and an unterminated buffer reads stack residue past
         * the copied bytes — the lookup then depends on what earlier
         * compiles left on the stack (nondeterministic across sweeps). */
        fqn[at] = '\0';
        if (!truncated && pm_metal_src_find(fqn) != NULL) {
            break;
        }
        nsegs--;
    }
    if (nsegs == 0u) {
        /* not a card path — nothing to chase (a std/core path or a name
         * the unit already declares) */
        return PM_METAL_BUILD_OK;
    }
    if (rs_splice_card_seen(seen, fqn)) {
        return PM_METAL_BUILD_OK;
    }
    if (depth + 1u > PM_BUILD_SPLICE_MAX_DEPTH) {
        err_set(errbuf, errbuf_len, "splice: use-chase nesting too deep", 0);
        return PM_METAL_BUILD_ERR_PARSE;
    }
    if (!rs_splice_card_note(seen, fqn)) {
        err_set(errbuf, errbuf_len, "splice: too many referenced cards", 0);
        return PM_METAL_BUILD_ERR_PARSE;
    }
    /* __types__.rs first (real ABI shapes), then __exports__.rs (the
     * extern fn declarations consumers type against). Both are optional
     * faces — a card without one simply contributes the other. */
    {
        const pm_metal_src_file_t *tf =
            pm_metal_src_face_find(fqn, "__types__.rs");
        int32_t rc;
        if (tf != NULL) {
            rc = rs_splice_face(arena, tf, buf_io, len_io, errbuf, errbuf_len);
            if (rc != PM_METAL_BUILD_OK) {
                return rc;
            }
            rc = rs_splice_into(arena, fqn, (const char *)tf->data, depth + 1u,
                seen, buf_io, len_io, errbuf, errbuf_len, inc);
            if (rc != PM_METAL_BUILD_OK) {
                return rc;
            }
        }
    }
    {
        const pm_metal_src_file_t *ef =
            pm_metal_src_face_find(fqn, "__exports__.rs");
        int32_t rc;
        if (ef != NULL) {
            rc = rs_splice_face(arena, ef, buf_io, len_io, errbuf, errbuf_len);
            if (rc != PM_METAL_BUILD_OK) {
                return rc;
            }
            rc = rs_splice_into(arena, fqn, (const char *)ef->data, depth + 1u,
                seen, buf_io, len_io, errbuf, errbuf_len, inc);
            if (rc != PM_METAL_BUILD_OK) {
                return rc;
            }
        }
    }
    return PM_METAL_BUILD_OK;
}

static int32_t rs_splice_into(pm_util_mem_arena_t *arena, const char *fqn,
    const char *src, uint32_t depth, rs_splice_cards_t *seen,
    char **buf_io, size_t *len_io, char *errbuf, size_t errbuf_len,
    rs_splice_inc_t *inc) {
    const char *p = src;
    char *dir = fqn_to_dir(arena, fqn);
    /* Sticky while consecutive attribute lines stack above one item; a
     * non-attribute line ends the run. */
    uint32_t test_guard = 0u;
    /* A #[path] attr consumed on its own line: the immediately following
     * line is the mod decl that names the spliced file — the decl itself
     * must NOT be chased again (that would splice the file twice). */
    uint32_t path_pending = 0u;
    /* A cfg guard (any `#[cfg(..)]`) parks the NEXT mod decl for rsx's
     * own feature eval — a cfg-guarded mod is not chased. */
    uint32_t cfg_guard = 0u;
    if (dir == NULL) {
        err_set(errbuf, errbuf_len, "splice: arena exhausted", 0);
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    while (*p != '\0') {
        /* line-start `use crate::...;` — chase the referenced card's
         * ABI faces. Runs before the attribute scan: a use line is
         * never inside an attribute. */
        {
            const char *ws = p;
            while (*ws == ' ' || *ws == '\t') {
                ws++;
            }
            if (ws == p || p == src || p[-1] == '\n') {
                const char *after = NULL;
                if (rs_use_crate_starts(ws, &after)) {
                    const char *span_end = after - 1;  /* at the ';' */
                    const char *span = ws + 4;  /* past `use ` */
                    size_t span_len = (size_t)(span_end - span);
                    char *span_dup = (char *)dup_str(arena, span, span_len);
                    int32_t rc;
                    if (span_dup == NULL) {
                        err_set(errbuf, errbuf_len, "splice: arena exhausted", 0);
                        return PM_METAL_BUILD_ERR_NOMEM;
                    }
                    rc = rs_splice_use_crate(arena, span_dup, seen, depth,
                        buf_io, len_io, errbuf, errbuf_len, inc);
                    if (rc != PM_METAL_BUILD_OK) {
                        return rc;
                    }
                    p = after;
                    continue;
                }
            }
        }
        /* line-start `pub mod NAME;` / `mod NAME;` — the sibling-file
         * module include (cargo's own resolution). The referenced file
         * rides in from the same card's embed; a name with no embedded
         * file is left to rsx (its own module-refusal names the line).
         * Depth-capped like every other chase. */
        {
            const char *ws2 = p;
            while (*ws2 == ' ' || *ws2 == '\t') {
                ws2++;
            }
            if (ws2 == p || p == src || p[-1] == '\n') {
                const char *mname = NULL;
                size_t mname_len = 0u;
                const char *after2 = NULL;
                if (path_pending == 0u && test_guard == 0u && cfg_guard == 0u
                    && rs_mod_decl_starts(ws2, &mname, &mname_len, &after2)) {
                    char *mfile = (char *)pm_util_mem_alloc(
                        arena, mname_len + 4u);
                    const pm_metal_src_file_t *mface;
                    if (mfile == NULL) {
                        err_set(errbuf, errbuf_len, "splice: arena exhausted", 0);
                        return PM_METAL_BUILD_ERR_NOMEM;
                    }
                    memcpy(mfile, mname, mname_len);
                    memcpy(mfile + mname_len, ".rs", 3u);
                    mfile[mname_len + 3u] = '\0';
                    mface = rs_card_file_find(fqn, mfile);
                    if (mface != NULL) {
                        int32_t rc;
                        if (depth + 1u > PM_BUILD_SPLICE_MAX_DEPTH) {
                            err_set(errbuf, errbuf_len,
                                "splice: mod include nesting too deep", 0);
                            return PM_METAL_BUILD_ERR_PARSE;
                        }
                        rc = rs_splice_face(arena, mface, buf_io, len_io,
                            errbuf, errbuf_len);
                        if (rc != PM_METAL_BUILD_OK) {
                            return rc;
                        }
                        rc = rs_splice_inc_add(arena, inc, mfile);
                        if (rc != PM_METAL_BUILD_OK) {
                            return rc;
                        }
                        rc = rs_splice_into(arena, fqn, (const char *)mface->data,
                            depth + 1u, seen, buf_io, len_io, errbuf, errbuf_len,
                            inc);
                        if (rc != PM_METAL_BUILD_OK) {
                            return rc;
                        }
                    }
                    test_guard = 0u;
                    cfg_guard = 0u;
                    p = after2;
                    continue;
                }
            }
        }
        /* line-start `#[path ...` (after optional whitespace) */
        const char *ls = p;
        const char *le = p;
        int is_attr;
        while (*ls == ' ' || *ls == '\t') {
            ls++;
        }
        while (*le != '\0' && *le != '\n') {
            le++;
        }
        is_attr = ls[0] == '#' && ls[1] == '[';
        if (!is_attr) {
            test_guard = 0u;
            path_pending = 0u;
            cfg_guard = 0u;
        } else if (rs_attr_is_test(ls, (size_t)(le - ls))) {
            test_guard = 1u;
        } else if (rs_attr_is_cfg(ls, (size_t)(le - ls))) {
            /* any cfg guard ahead of a mod decl parks the decl for rsx's
             * own cfg eval — see rs_attr_is_cfg */
            cfg_guard = 1u;
        }
        if (is_attr && test_guard) {
            /* cfg(test)-guarded `#[path] mod __tests__` — the test face is
             * not muscle and is never embedded; rsx skips the guarded item
             * itself, so the splice must skip it too (a chase would refuse
             * on a file that is out of the embed by design). */
            p = le;
            if (*p == '\n') {
                p++;
            }
            continue;
        }
        if (ls == p || p == src || p[-1] == '\n') {
            if (ls[0] == '#' && ls[1] == '['
                && (ls[2] == 'p' || ls[2] == 'P')
                && strncmp(ls + 2, "path", 4) == 0) {
                const char *q = ls + 6;
                while (*q == ' ' || *q == '\t' || *q == '=') {
                    q++;
                }
                if (*q == '"') {
                    const char *e = q + 1;
                    const char *rel;
                    const char *full;
                    char *tcard;
                    const char *tfile;
                    const char *slash;
                    const pm_metal_src_file_t *face;
                    while (*e != '\0' && *e != '"') {
                        e++;
                    }
                    if (*e == '"') {
                        rel = dup_str(arena, q + 1, (size_t)(e - q - 1u));
                        full = norm_join(arena, dir, rel);
                        if (full == NULL) {
                            err_set(errbuf, errbuf_len,
                                "splice: path escapes the card tree", 0);
                            return PM_METAL_BUILD_ERR_PARSE;
                        }
                        slash = strrchr(full, '/');
                        tfile = dup_str(arena,
                            slash != NULL ? slash + 1 : full,
                            strlen(slash != NULL ? slash + 1 : full));
                        {
                            const char *tdir = (slash != NULL)
                                ? full
                                : (dir != NULL && dir[0] != '\0') ? dir : ".";
                            size_t tn = (slash != NULL)
                                ? (size_t)(slash - full) : strlen(tdir);
                            char *tm = (char *)pm_util_mem_alloc(arena, tn + 1u);
                            size_t k;
                            if (tm == NULL) {
                                err_set(errbuf, errbuf_len,
                                    "splice: arena exhausted", 0);
                                return PM_METAL_BUILD_ERR_NOMEM;
                            }
                            memcpy(tm, tdir, tn);
                            tm[tn] = '\0';
                            for (k = 0; k < tn; k++) {
                                if (tm[k] == '/') {
                                    tm[k] = '.';
                                }
                            }
                            tcard = tm;
                        }
                        if (tcard == NULL || tfile == NULL) {
                            err_set(errbuf, errbuf_len,
                                "splice: arena exhausted", 0);
                            return PM_METAL_BUILD_ERR_NOMEM;
                        }
                        /* card dir -> fqn: dots for slashes */
                        {
                            size_t cn = strlen(tcard);
                            size_t k;
                            for (k = 0; k < cn; k++) {
                                if (tcard[k] == '/') {
                                    tcard[k] = '.';
                                }
                            }
                        }
                        face = pm_metal_src_face_find(tcard, tfile);
                        if (face == NULL) {
                            err_set(errbuf, errbuf_len,
                                "splice: included face not in embed", 0);
                            return PM_METAL_BUILD_ERR_PARSE;
                        }
                        if (depth + 1u > PM_BUILD_SPLICE_MAX_DEPTH) {
                            err_set(errbuf, errbuf_len,
                                "splice: include nesting too deep", 0);
                            return PM_METAL_BUILD_ERR_PARSE;
                        }
                        {
                            char *nb = (char *)pm_util_mem_alloc(
                                arena, *len_io + (size_t)face->len + 4u);
                            if (nb == NULL) {
                                err_set(errbuf, errbuf_len,
                                    "splice: arena exhausted", 0);
                                return PM_METAL_BUILD_ERR_NOMEM;
                            }
                            /* copy the source without its NUL, then the
                             * face, then the newline + NUL terminator */
                            memcpy(nb, *buf_io, *len_io);
                            memcpy(nb + *len_io, face->data, (size_t)face->len);
                            *len_io += (size_t)face->len;
                            nb[*len_io] = '\n';
                            (*len_io)++;
                            nb[*len_io] = '\0';
                            *buf_io = nb;
                        }
                        {
                            int32_t rc = rs_splice_into(arena, tcard,
                                (const char *)face->data, depth + 1u, seen,
                                buf_io, len_io, errbuf, errbuf_len, inc);
                            if (rc != PM_METAL_BUILD_OK) {
                                return rc;
                            }
                        }
                        path_pending = 1u;
                        /* consume the rest of the attr line (p = e + 1
                         * would leave the newline as its own iteration,
                         * whose non-attr scan resets path_pending before
                         * the mod decl line is ever seen). */
                        p = le;
                        if (*p == '\n') {
                            p++;
                        }
                        continue;
                    }
                }
            }
        }
        while (*p != '\0' && *p != '\n') {
            p++;
        }
        if (*p == '\n') {
            p++;
        }
    }
    return PM_METAL_BUILD_OK;
}

/* Splice every `#[path]`-included face after the unit source. The card's
 * own text keeps its true line numbers (rsx #line diagnostics stay
 * honest); the appended faces shift beyond the file end, which only
 * affects diagnostics inside the face itself. */
static char *rs_splice(pm_util_mem_arena_t *arena, const char *fqn,
    const char *src, char *errbuf, size_t errbuf_len,
    rs_splice_inc_t *inc) {
    size_t len = strlen(src);
    char *buf = (char *)dup_str(arena, src, len);
    rs_splice_cards_t seen;
    if (buf == NULL) {
        err_set(errbuf, errbuf_len, "splice: arena exhausted", 0);
        return NULL;
    }
    memset(&seen, 0, sizeof(seen));
    if (rs_splice_into(arena, fqn, buf, 0u, &seen, &buf, &len,
            errbuf, errbuf_len, inc) != PM_METAL_BUILD_OK) {
        return NULL;
    }
#if !defined(PM_METAL_FIRMWARE)
    {
        /* debug tap (never on by default): RSX_DUMP_SPLICE=<path> writes
         * the spliced source — splice-divergence hunts. Host/unix only:
         * the firmware seats have no getenv/fopen in their libc shims.
         * RSX_DUMP_SPLICE_FQN=<fqn> filters to one card (the sweep
         * overwrites the file per unit otherwise). */
        {
            const char *tap = getenv("RSX_DUMP_SPLICE");
            const char *tap_fqn = getenv("RSX_DUMP_SPLICE_FQN");
            if (tap != NULL
                && (tap_fqn == NULL || strcmp(tap_fqn, fqn) == 0)) {
                FILE *o = fopen(tap, "wb");
                if (o != NULL) {
                    fwrite(buf, 1u, len, o);
                    fclose(o);
                }
            }
        }
    }
#endif
    return buf;
}

/* One unit source to a TCC object, dispatching on the file extension:
 * .rs -> micro-rustc -> C -> TCC; .cpp/.cxx/.cc -> cpp lower -> C -> TCC;
 * .c direct; .h is not a TU (skipped, not an object). Refusals carry the
 * transpiler's own diagnostics (rsx names the construct + line). */
static int32_t unit_source_compile(pm_util_mem_arena_t *arena,
    const char *fqn, const char *rel, const char *src,
    const char **includes, uint32_t n_includes,
    const char **defines, uint32_t n_defines,
    int32_t target,
    uint8_t **obj_out, size_t *obj_len,
    char *errbuf, size_t errbuf_len) {
    const char *dot = rel != NULL ? strrchr(rel, '.') : NULL;
    const char *csrc = src;
    char *transpiled = NULL;
    size_t transpiled_len = 0;
    int32_t rc;

    if (dot != NULL) {
        if (strcmp(dot, ".rs") == 0) {
            /* `#[path]`-included faces ride in: the rsx compile is
             * standalone, cross-card ABI shapes arrive by splice. */
            char *spliced = rs_splice(arena, fqn, src, errbuf, errbuf_len,
                NULL);
            if (spliced == NULL) {
                return PM_METAL_BUILD_ERR_COMPILE;
            }
#if !defined(PM_METAL_FIRMWARE)
            {
                /* debug tap (never on by default): RSX_SPLICE_OUT=<path>
                 * writes this unit's spliced Rust — line-number mapping
                 * between a refusal's unit line and the authored faces.
                 * Host/unix only: the firmware libc shims have no
                 * getenv/fopen. */
                const char *tap = getenv("RSX_SPLICE_OUT");
                const char *tap_fqn = getenv("RSX_SPLICE_FQN");
                if (tap != NULL && rel != NULL
                    && (tap_fqn == NULL
                        || strcmp(tap_fqn, fqn) == 0)
                    && (strstr(rel, ".rs") != NULL)) {
                    FILE *o = fopen(tap, "wb");
                    if (o != NULL) {
                        fwrite(spliced, 1u, strlen(spliced), o);
                        fclose(o);
                    }
                }
            }
#endif
            if (pm_metal_jit_rsx_compile(arena, spliced, strlen(spliced),
                    &transpiled, &transpiled_len, errbuf, errbuf_len) != 0) {
                pm_util_mem_free(arena, spliced);
                return PM_METAL_BUILD_ERR_COMPILE;
            }
            /* the spliced source fed the transpiler; its bytes live on in
             * the tokens/AST spans rsx copied out. Free the ~1.5 MB splice
             * buffer before the C phase — the TCC compile below shares
             * this arena and needs the headroom. */
            pm_util_mem_free(arena, spliced);
            csrc = transpiled;
#if !defined(PM_METAL_FIRMWARE)
            {
                /* debug tap (never on by default): RSX_DUMP_UNIT=<path>
                 * writes this unit's generated C — garbage-byte hunts and
                 * ksweep-vs-dump divergences, same posture as rsx_span's
                 * SPAN_OUT. Host/unix only: the firmware libc shims have
                 * no getenv/fopen. */
                const char *tap = getenv("RSX_DUMP_UNIT");
                const char *tap_fqn = getenv("RSX_DUMP_UNIT_FQN");
                if (tap != NULL && rel != NULL
                    && (tap_fqn == NULL
                        || strcmp(tap_fqn, fqn) == 0)
                    && (strstr(rel, ".rs") != NULL)) {
                    FILE *o = fopen(tap, "wb");
                    if (o != NULL) {
                        fwrite(transpiled, 1u, transpiled_len, o);
                        fclose(o);
                    }
                }
            }
#endif
        } else if (strcmp(dot, ".cpp") == 0 || strcmp(dot, ".cxx") == 0
            || strcmp(dot, ".cc") == 0) {
            pm_jit_cpp_toklist_t toks;
            pm_jit_cpp_ast_t *ast = NULL;
            if (pm_metal_jit_cpp_lex(arena, src, strlen(src), &toks,
                    errbuf, errbuf_len) != 0) {
                return PM_METAL_BUILD_ERR_COMPILE;
            }
            if (pm_metal_jit_cpp_parse(arena, &toks, &ast,
                    errbuf, errbuf_len) != 0) {
                return PM_METAL_BUILD_ERR_COMPILE;
            }
            if (pm_metal_jit_cpp_lower(arena, ast, &transpiled, &transpiled_len,
                    errbuf, errbuf_len) != 0) {
                return PM_METAL_BUILD_ERR_COMPILE;
            }
            csrc = transpiled;
        }
    }
    /* target 0 keeps the seat-native path; the matrix knobs route through
     * the target-aware jit.c face (the cross instance where linked, the
     * honest errbuf refusal where not — never a silent fallback). */
    if (target != 0) {
        rc = pm_metal_jit_c_object_compile_target(arena, csrc, strlen(csrc),
            includes, n_includes, defines, n_defines, target,
            obj_out, obj_len, errbuf, errbuf_len);
    } else {
        rc = pm_metal_jit_c_object_compile_opts(arena, csrc, strlen(csrc),
            includes, n_includes, defines, n_defines,
            obj_out, obj_len, errbuf, errbuf_len);
    }
    /* the transpiled C is compile scratch — the TCC object carries the
     * product now. Free it (and only after the window released — TCC's
     * last byte of this block was read inside the compile above): the
     * 1.9MB of generated C staying resident starved the next unit's
     * arena in ksweep's per-unit backings. The rsx compile's own tables
     * (tokens, AST, Lower) are arena leak-by-design for this card — the
     * unit's arena dies at the sweep row anyway; what must release is
     * the byte range a subsequent phase in THIS compile still needs to
     * grow into. */
    if (transpiled != NULL) {
        pm_util_mem_free(arena, transpiled);
    }
    if (rc != 0) {
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    return PM_METAL_BUILD_OK;
}

/* impl = py: Python source -> mpy bytecode (the µPy compiler in-process).
 * The product is bytecode, not a native image — artifact->bytes carries
 * the FIRST source's mpy (loadable via jit.py's object_load), later
 * sources' mpy lengths ride the record like object lengths. Seats
 * without MICROPY_PERSISTENT_CODE_SAVE refuse politely; that refusal is
 * reported, not papered over. */
static int32_t unit_compile_py(pm_util_mem_arena_t *arena,
    const pm_metal_build_unit_t *unit, pm_metal_build_artifact_t *artifact,
    char *errbuf, size_t errbuf_len) {
    const pm_metal_src_card_t *c = pm_metal_src_find(unit->fqn);
    uint32_t obj_i;
    pm_metal_build_record_t *rec;

    if (c == NULL) {
        err_set(errbuf, errbuf_len, "unit_compile: card not in embed", 0);
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    /* py units ride the same event ring (compile_start/end per source) so
     * the factory floor sees them like any other lane */
    build_event_emit(PM_METAL_BUILD_EVENT_UNIT_START, 0, unit->fqn, NULL, 0, 0);
    memset(artifact, 0, sizeof(*artifact));
    snprintf(artifact->fqn, sizeof(artifact->fqn), "%s", unit->fqn);
    artifact->is_mpy = 1;
    for (obj_i = 0; obj_i < unit->n_sources; obj_i++) {
        const char *src = NULL;
        uint8_t *mpy = NULL;
        size_t mpy_len = 0;
        uint32_t f;
        const char *dot = strrchr(unit->sources[obj_i], '.');
        if (dot != NULL && strcmp(dot, ".py") != 0) {
            continue;  /* a non-py file beside the muscle is not a TU here */
        }
        for (f = 0; f < c->nfiles; f++) {
            if (strcmp(c->files[f].rel, unit->sources[obj_i]) == 0) {
                src = (const char *)c->files[f].data;
                break;
            }
        }
        if (src == NULL) {
            err_set(errbuf, errbuf_len, "unit_compile: source not in embed", 0);
            return PM_METAL_BUILD_ERR_COMPILE;
        }
        {
            uint64_t t0 = pm_metal_coop_mono_us();
            build_event_emit(PM_METAL_BUILD_EVENT_COMPILE_START, 0,
                unit->fqn, unit->sources[obj_i], 0, 0);
            int32_t prc = pm_metal_jit_py_object_compile(arena, src,
                strlen(src), unit->fqn, &mpy, &mpy_len, errbuf, errbuf_len);
            if (prc != 0) {
                build_event_emit(PM_METAL_BUILD_EVENT_UNIT_FAIL, 0,
                    unit->fqn, unit->sources[obj_i],
                    (uint32_t)((pm_metal_coop_mono_us() - t0) & 0xffffffffu), 0);
                /* GIL busy is nothing about this unit: a runner tried to
                 * compile Python while Python code held the GIL. Report it
                 * as BUSY so a walk retries instead of reddening a row that
                 * builds fine on its own. */
                return prc == PM_METAL_JIT_PY_BUSY
                    ? PM_METAL_BUILD_ERR_BUSY : PM_METAL_BUILD_ERR_COMPILE;
            }
            build_event_emit(PM_METAL_BUILD_EVENT_COMPILE_END, 0,
                unit->fqn, unit->sources[obj_i],
                (uint32_t)((pm_metal_coop_mono_us() - t0) & 0xffffffffu),
                (uint32_t)mpy_len);
            /* A py unit's product is its .mpy, and it dies with this arena
             * exactly like a C object does — retain it on the same terms so
             * py-impl rows are downloadable too, not the only rows without
             * a button. */
            {
                const char *ksrc = unit->sources[obj_i];
                size_t klen = mpy_len;
                build_keep_objects(unit->fqn, &ksrc, &mpy, &klen, 1u);
            }
        }
        if (artifact->bytes == NULL) {
            artifact->bytes = mpy;
            artifact->len = mpy_len;
        }
        /* record the mpy lengths like object lengths (audit trail). One lock
         * section per source: pick + one path/len pair — the record stays
         * consistent for pane readers on other cores. First source
         * publishes valid; later sources only append fields. */
        {
            pm_metal_build_ctx_t *rctx = build_ctx_acquire();
            if (rctx != NULL) {
                pm_util_lock_acquire(&rctx->lock);
                rec = record_slot_acquire_locked(unit->fqn);
                if (rec != NULL && rec->n_sources < PM_METAL_BUILD_MAX_OBJS) {
                    snprintf(rec->src_paths[rec->n_sources],
                        PM_METAL_BUILD_MAX_SRC_PATH, "%s", unit->sources[obj_i]);
                    rec->obj_lens[rec->n_sources] = (uint32_t)mpy_len;
                    rec->n_sources++;
                }
                record_publish_locked(rec);
                pm_util_lock_release(&rctx->lock);
            }
        }
    }
    if (artifact->bytes == NULL) {
        err_set(errbuf, errbuf_len, "unit_compile: no py source produced mpy", 0);
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    build_event_emit(PM_METAL_BUILD_EVENT_UNIT_END, 0, unit->fqn, NULL, 0, 0);
    return PM_METAL_BUILD_OK;
}

int32_t pm_metal_build_unit_compile(pm_util_mem_arena_t *arena,
    const pm_metal_build_unit_t *unit,
    const pm_metal_build_compile_opts_t *opts,
    pm_metal_build_artifact_t *artifact,
    char *errbuf, size_t errbuf_len) {
    uint8_t **objs;
    size_t *lens;
    const char **compiled_srcs;
    const char **all_includes = NULL;
    uint32_t n_all_includes = 0;
    uint32_t i;
    uint32_t obj_i;
    uint32_t n_objs = 0;
    int32_t rc;
    pm_metal_build_record_t *rec = NULL;
    const char *unit_root;
    const char **include_dirs;
    uint32_t n_include_dirs;
    const char **extra_defines;
    uint32_t n_extra_defines;

    if (opts == NULL) {
        err_set(errbuf, errbuf_len, "unit_compile: bad args", 0);
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    unit_root = opts->unit_root;
    include_dirs = opts->include_dirs;
    n_include_dirs = opts->n_include_dirs;
    extra_defines = opts->defines;
    n_extra_defines = opts->n_defines;
    if (arena == NULL || unit == NULL || unit_root == NULL || artifact == NULL) {
        err_set(errbuf, errbuf_len, "unit_compile: bad args", 0);
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    if (strcmp(unit->impl, "c") != 0 && strcmp(unit->impl, "rs") != 0
        && strcmp(unit->impl, "cpp") != 0 && strcmp(unit->impl, "py") != 0) {
        char msg[96];
        snprintf(msg, sizeof(msg), "unknown impl=%s", unit->impl);
        err_set(errbuf, errbuf_len, msg, 0);
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    /* factory telemetry: every observable stage of this unit lands on the
     * event ring. target rides opts->target (0 = seat-native, the matrix
     * knobs 1..3 route the emit on cross seats). */
    uint16_t copts_target = (uint16_t)(opts->target & 0xffffu);
    build_event_emit(PM_METAL_BUILD_EVENT_UNIT_START,
        copts_target, unit->fqn, NULL, 0, 0);
    if (unit->n_sources == 0) {
        err_set(errbuf, errbuf_len, "unit_compile: no sources", 0);
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    /* impl = py: Python -> mpy bytecode, no native link (the module's
     * product is loadable bytecode; lookup/call are not applicable). */
    if (strcmp(unit->impl, "py") == 0) {
        return unit_compile_py(arena, unit, artifact, errbuf, errbuf_len);
    }
    /* impl = rs / cpp / c: every source lands in C (rs and cpp transpile
     * first), then TCC objects, then the native link. */
    objs = (uint8_t **)pm_util_mem_alloc(
        arena, unit->n_sources * sizeof(uint8_t *));
    lens = (size_t *)pm_util_mem_alloc(arena, unit->n_sources * sizeof(size_t));
    compiled_srcs = (const char **)pm_util_mem_alloc(
        arena, unit->n_sources * sizeof(const char *));
    if (objs == NULL || lens == NULL || compiled_srcs == NULL) {
        err_set(errbuf, errbuf_len, "unit_compile: arena exhausted", 0);
        return PM_METAL_BUILD_ERR_NOMEM;
    }

    /* merge unit->include_dirs (rooted at unit_root) + the seat fill. The
     * card's own dir rides first: generated headers in the card
     * (fig_small.inc.h, www_embed.inc.h) are relative to the muscle, so
     * TCC must search the same dir the host build compiles from. */
    n_all_includes = unit->n_include_dirs + n_include_dirs + 1;
    if (n_all_includes > 0) {
        all_includes = (const char **)pm_util_mem_alloc(
            arena, n_all_includes * sizeof(const char *));
        if (all_includes == NULL) {
            err_set(errbuf, errbuf_len, "unit_compile: arena exhausted", 0);
            return PM_METAL_BUILD_ERR_NOMEM;
        }
        all_includes[0] = join_path(arena, unit_root, ".");
        if (all_includes[0] == NULL) {
            err_set(errbuf, errbuf_len, "unit_compile: arena exhausted", 0);
            return PM_METAL_BUILD_ERR_NOMEM;
        }
        for (i = 0; i < unit->n_include_dirs; i++) {
            all_includes[1 + i] = join_path(arena, unit_root, unit->include_dirs[i]);
            if (all_includes[1 + i] == NULL) {
                err_set(errbuf, errbuf_len, "unit_compile: arena exhausted", 0);
                return PM_METAL_BUILD_ERR_NOMEM;
            }
        }
        for (i = 0; i < n_include_dirs; i++) {
            all_includes[1 + unit->n_include_dirs + i] = include_dirs[i];
        }
    }

    /* compile each embedded source through the opts seam (unit defines +
     * the seat fill's defines joined) */
    {
        const char **all_defines = NULL;
        uint32_t n_all_defines = unit->n_defines + n_extra_defines;
        if (n_all_defines > 0) {
            all_defines = (const char **)pm_util_mem_alloc(
                arena, n_all_defines * sizeof(const char *));
            if (all_defines == NULL) {
                err_set(errbuf, errbuf_len, "unit_compile: arena exhausted", 0);
                return PM_METAL_BUILD_ERR_NOMEM;
            }
            for (i = 0; i < unit->n_defines; i++) {
                all_defines[i] = unit->defines[i];
            }
            for (i = 0; i < n_extra_defines; i++) {
                all_defines[unit->n_defines + i] = extra_defines[i];
            }
        }
        /* impl = rs with a multi-module crate: the root TU's splice
         * inlines every companion the crate root's mod decls name
         * (cargo's own crate semantics — one TU). Those companions
         * must NOT also compile standalone: the symbols would collide
         * at link. Compute the root's inline set once, then skip
         * exactly those sources. A companion the root does not name
         * (cfg(test) benches, tests) keeps its own TU. */
        rs_splice_inc_t inc;
        const pm_metal_src_card_t *root_card = NULL;
        const char *root_src = NULL;
        memset(&inc, 0, sizeof(inc));
        if (strcmp(unit->impl, "rs") == 0) {
            root_card = pm_metal_src_find(unit->fqn);
            if (root_card != NULL) {
                uint32_t f;
                for (f = 0; f < root_card->nfiles; f++) {
                    if (strcmp(root_card->files[f].rel, "__impl__.rs") == 0) {
                        root_src = (const char *)root_card->files[f].data;
                        break;
                    }
                }
            }
            if (root_src != NULL) {
                char serr[PM_METAL_BUILD_ERR_MAX];
                char *root_sp = rs_splice(arena, unit->fqn, root_src, serr, sizeof(serr),
                    &inc);
                if (root_sp == NULL) {
                    /* the root's own compile below reports this honestly */
                    memset(&inc, 0, sizeof(inc));
                } else {
                    /* same contract as discover's splice probe: the inline
                     * set is the product, the buffer is scratch. */
                    pm_util_mem_free(arena, root_sp);
                }
            }
        }
        for (obj_i = 0; obj_i < unit->n_sources; obj_i++) {
            const pm_metal_src_card_t *c = pm_metal_src_find(unit->fqn);
            const char *src = NULL;
            const char *dot;
            if (c != NULL) {
                uint32_t f;
                for (f = 0; f < c->nfiles; f++) {
                    if (strcmp(c->files[f].rel, unit->sources[obj_i]) == 0) {
                        src = (const char *)c->files[f].data;
                        break;
                    }
                }
            }
            if (src == NULL) {
                err_set(errbuf, errbuf_len, "unit_compile: source not in embed", 0);
                return PM_METAL_BUILD_ERR_COMPILE;
            }
            dot = strrchr(unit->sources[obj_i], '.');
            if (dot != NULL && strcmp(dot, ".h") == 0) {
                continue;  /* headers ride the include path, never a TU */
            }
            if (dot != NULL && strcmp(dot, ".rs") == 0 && inc.n > 0u) {
                /* a companion the root TU already inlined rides the
                 * root's object; skip its standalone compile */
                uint32_t k;
                int inlined = 0;
                for (k = 0; k < inc.n; k++) {
                    if (strcmp(inc.rel[k], unit->sources[obj_i]) == 0) {
                        inlined = 1;
                        break;
                    }
                }
                if (inlined) {
                    continue;
                }
            }
            {
                uint64_t t0 = pm_metal_coop_mono_us();
                build_event_emit(PM_METAL_BUILD_EVENT_COMPILE_START,
                    copts_target, unit->fqn,
                    unit->sources[obj_i], 0, 0);
                if (unit_source_compile(arena, unit->fqn, unit->sources[obj_i], src,
                    all_includes, n_all_includes, all_defines, n_all_defines,
                    copts_target,
                    &objs[n_objs], &lens[n_objs], errbuf, errbuf_len)
                        != PM_METAL_BUILD_OK) {
                    build_event_emit(PM_METAL_BUILD_EVENT_UNIT_FAIL,
                        copts_target, unit->fqn,
                        unit->sources[obj_i],
                        (uint32_t)((pm_metal_coop_mono_us() - t0) & 0xffffffffu), 0);
                    return PM_METAL_BUILD_ERR_COMPILE;
                }
                build_event_emit(PM_METAL_BUILD_EVENT_COMPILE_END,
                    copts_target, unit->fqn,
                    unit->sources[obj_i],
                    (uint32_t)((pm_metal_coop_mono_us() - t0) & 0xffffffffu),
                    (uint32_t)lens[n_objs]);
            }
            compiled_srcs[n_objs] = unit->sources[obj_i];
            n_objs++;
        }
        if (n_objs == 0) {
            err_set(errbuf, errbuf_len, "unit_compile: no compilable sources", 0);
            return PM_METAL_BUILD_ERR_COMPILE;
        }
        /* The objects exist only here: the link below consumes them and the
         * caller's arena frees them with the request. Copy them out now so
         * a download route has something to serve. */
        build_keep_objects(unit->fqn, compiled_srcs, objs, lens, n_objs);
    }

    {
        uint64_t t0 = pm_metal_coop_mono_us();
        /* Same split the walk's actor makes (see its link gate): produce
         * stops at the objects, which are already kept above. Linking a
         * foreign object into this seat turned a successful emit into a
         * refusal — "loader refused object 0" for a wasm module whose
         * imports only exist in the seat it was produced for. */
        if (opts->mode == PM_METAL_BUILD_PRODUCE
            || build_target_is_cross(opts->target)) {
            rc = PM_METAL_BUILD_OK;
        } else {
            rc = pm_metal_build_link(arena, unit, objs, lens, n_objs,
                artifact, errbuf, errbuf_len);
        }
        if (rc != PM_METAL_BUILD_OK) {
            build_event_emit(PM_METAL_BUILD_EVENT_UNIT_FAIL,
                copts_target, unit->fqn, "link",
                (uint32_t)((pm_metal_coop_mono_us() - t0) & 0xffffffffu), 0);
            return rc;
        }
        build_event_emit(PM_METAL_BUILD_EVENT_LINK_END,
            copts_target, unit->fqn, NULL,
            (uint32_t)((pm_metal_coop_mono_us() - t0) & 0xffffffffu),
            artifact->len);
    }

    /* provenance record: source paths + object lengths + linked symbols.
     * Best-effort — a record overflow truncates the lists, never fails the
     * build (the artifact is the product; the record is the audit trail).
     * The whole write block is one ctx-lock section (pick, fields, sym
     * walk, valid publish) — the /build pane may read the table on
     * another core mid-walk. */
    {
        pm_metal_build_ctx_t *rctx = build_ctx_acquire();
        if (rctx != NULL) {
            pm_util_lock_acquire(&rctx->lock);
            rec = record_slot_acquire_locked(unit->fqn);
            if (rec != NULL) {
                uint32_t cap = n_objs;
                if (cap > PM_METAL_BUILD_MAX_OBJS) {
                    cap = PM_METAL_BUILD_MAX_OBJS;
                }
                for (i = 0; i < cap; i++) {
                    snprintf(rec->src_paths[i], PM_METAL_BUILD_MAX_SRC_PATH, "%s",
                        compiled_srcs[i]);
                    rec->obj_lens[i] = (uint32_t)lens[i];
                }
                rec->n_sources = cap;
#ifdef PM_METAL_BUILD_HAS_ELF
                {
                    pm_build_rec_sym_ctx_t sctx;
                    sctx.r = rec;
                    sctx.w = 0;
                    mp_wasm_elf_foreach_func(
                        (const mp_wasm_elf_image_t *)artifact->bytes,
                        record_sym_cb, &sctx);
                    rec->n_syms = sctx.w;
                }
#endif
                record_publish_locked(rec);
            }
            pm_util_lock_release(&rctx->lock);
        }
    }
    build_event_emit(PM_METAL_BUILD_EVENT_UNIT_END,
        copts_target, unit->fqn, NULL,
        0 /* the page sums stage durs; unit wall time is its span */, 0);
    return PM_METAL_BUILD_OK;
}

/* pm_build_at_slot_t, PM_METAL_BUILD_AT_SLOTS and the at-slot table are
 * hoisted above pm_metal_build_ctx_t; the slots + epoch live in the ctx. */



/* deps from the embedded manifest: parse the card's __pmm__.toml and copy
 * its depends[] into the info block (bounded, de-duplicated). The TOML
 * parse needs an arena; the ctx's deps scratch serves it (the parse is
 * single-threaded and self-contained). */
static void at_fill_deps(pm_metal_build_at_info_t *info) {
    const pm_metal_src_card_t *c = pm_metal_src_find(info->fqn);
    pm_metal_build_ctx_t *ctx = build_ctx_acquire();
    pm_util_mem_arena_t *arena;
    pm_metal_build_unit_t unit;
    char err[PM_METAL_BUILD_ERR_MAX];
    uint32_t i;
    if (c == NULL || c->toml == NULL || ctx == NULL) {
        return;
    }
    arena = pm_util_mem_arena_create(ctx->deps_scratch, sizeof(ctx->deps_scratch));
    if (arena == NULL) {
        return;
    }
    if (pm_metal_build_unit_parse(arena, (const uint8_t *)c->toml,
            strlen(c->toml), &unit, err, sizeof(err)) == PM_METAL_BUILD_OK) {
        for (i = 0; i < unit.n_depends && info->n_deps < PM_METAL_BUILD_AT_REFS_MAX; i++) {
            uint32_t j;
            int dup = 0;
            for (j = 0; j < info->n_deps; j++) {
                if (strcmp(info->deps[j], unit.depends[i]) == 0) {
                    dup = 1;
                    break;
                }
            }
            if (!dup) {
                snprintf(info->deps[info->n_deps], PM_METAL_BUILD_AT_REF_MAX,
                    "%s", unit.depends[i]);
                info->n_deps++;
            }
        }
    }
    pm_util_mem_arena_destroy(arena);
}

/* doc from the inspect extractor: JSON in, first prose line + file/line out.
 * The JSON shape is {"fqn":...,"name":...,"file":...,"line":N,"impl":...,
 * "prose":"...","params":[...],"example":"..."}. */
static void at_fill_doc(pm_metal_build_at_info_t *info) {
    const char *json = pm_metal_inspect_doc(info->fqn, info->name);
    const char *p;
    if (json == NULL) {
        return;
    }
    p = build_memfind(json, strlen(json), "\"prose\":\"");
    if (p != NULL) {
        const char *q = p + 9;
        uint32_t w = 0;
        while (*q != 0 && *q != '"' && w + 1u < sizeof(info->doc)) {
            if (q[0] == '\\' && q[1] != 0) {
                q++;
            }
            info->doc[w++] = *q++;
        }
        info->doc[w] = 0;
    }
    p = build_memfind(json, strlen(json), "\"file\":\"");
    if (p != NULL) {
        const char *q = p + 8;
        uint32_t w = 0;
        while (*q != 0 && *q != '"' && w + 1u < sizeof(info->file)) {
            info->file[w++] = *q++;
        }
        info->file[w] = 0;
    }
    p = build_memfind(json, strlen(json), "\"line\":");
    if (p != NULL) {
        info->line = (uint32_t)strtoul(p + 7, NULL, 10);
    }
}

/* notes from our own ledger (raw JSONL lines, target = fqn). Runs under
 * the caller's ctx lock when called from the at() fill — see
 * notes_query_locked, the lock-free body shared with the export face. */
static int32_t at_fill_notes_impl(pm_metal_build_at_info_t *info) {
    return pm_metal_build_notes_query_locked(info->fqn, -1,
        info->notes, sizeof(info->notes), &info->n_notes);
}

static void at_fill_notes(pm_metal_build_at_info_t *info) {
    int32_t rc = at_fill_notes_impl(info);
    if (rc < 0) {
        info->n_notes = 0;
    }
}

/* kind tag from the registry's export kind (same mapping as inspect's
 * kind_tag). */
static const char *at_kind_tag(pm_wasmmod_registry_export_kind_t k) {
    switch (k) {
        case PM_WASMMOD_REGISTRY_EXPORT_FN:     return "fn";
        case PM_WASMMOD_REGISTRY_EXPORT_MEM:    return "mem";
        case PM_WASMMOD_REGISTRY_EXPORT_OBJ:    return "obj";
        case PM_WASMMOD_REGISTRY_EXPORT_I64:    return "i64";
        case PM_WASMMOD_REGISTRY_EXPORT_F32:    return "f32";
        case PM_WASMMOD_REGISTRY_EXPORT_F64:    return "f64";
        case PM_WASMMOD_REGISTRY_EXPORT_BUFPTR: return "bufptr";
        default:                                return "?";
    }
}

/* Resolve fqn (+ optional export name) against the live registry first,
 * then layer the build record, the doc, the notes, and the manifest deps.
 * Returns a handle for at_info / at_ast, NONE when fqn is unknown to both
 * the registry and the embedded source table. */
pm_metal_build_at_handle_t pm_metal_build_at(const char *fqn, const char *name) {
    pm_metal_build_ctx_t *ctx = build_ctx_acquire();
    pm_build_at_slot_t *slot;
    pm_metal_build_at_info_t *info;
    const pm_metal_src_card_t *card;
    const pm_metal_build_record_t *rec;
    uint8_t fqnb[PM_METAL_BUILD_AT_FQN_MAX];
    uint32_t flen;
    uint32_t i;

    if (fqn == NULL || fqn[0] == 0 || ctx == NULL) {
        return PM_METAL_BUILD_AT_NONE;
    }
    flen = (uint32_t)strlen(fqn);
    if (flen >= sizeof(fqnb)) {
        return PM_METAL_BUILD_AT_NONE;
    }
    memcpy(fqnb, fqn, flen);

    /* resolve: the live registry first, then the embedded source table. A
     * fqn unknown to both is not resolvable. */
    card = pm_metal_src_find(fqn);
    if (pm_wasmmod_registry_has(fqnb, flen) != 1 && card == NULL) {
        return PM_METAL_BUILD_AT_NONE;
    }

    /* slot pick under the ctx lock: the epoch round-robin + memset + fill
     * + valid publish are one atomic slot transition — a concurrent
     * at_info/at_ast on another core sees either the old slot or the
     * fully-filled one, never a half-written reuse. The fills below call
     * record_find/notes_query/deps fill, none of which take the ctx lock
     * (they only read shared scratch), so the section cannot nest. */
    pm_util_lock_acquire(&ctx->lock);
    slot = &ctx->at[ctx->at_epoch % PM_METAL_BUILD_AT_SLOTS];
    ctx->at_epoch++;
    memset(slot, 0, sizeof(*slot));
    info = &slot->info;
    snprintf(info->fqn, sizeof(info->fqn), "%s", fqn);
    if (name != NULL && name[0] != 0) {
        snprintf(info->name, sizeof(info->name), "%s", name);
    } else {
        snprintf(info->name, sizeof(info->name), "%s", fqn);
        name = NULL;    /* card-level query */
    }
    info->lang[0] = 0;
    if (card != NULL && card->impl != NULL) {
        snprintf(info->lang, sizeof(info->lang), "%s", card->impl);
    }

    if (name != NULL) {
        /* face-level: find the export in the live registry */
        uint32_t nx = pm_wasmmod_registry_export_count(fqnb, flen);
        int found = 0;
        for (i = 0; i < nx; i++) {
            uint8_t ename[PM_METAL_BUILD_AT_NAME_MAX];
            uint32_t elen = sizeof(ename);
            uint8_t esig[PM_METAL_BUILD_AT_SIG_MAX];
            uint32_t slen = sizeof(esig);
            pm_wasmmod_registry_export_kind_t kind = PM_WASMMOD_REGISTRY_EXPORT_FN;
            if (pm_wasmmod_registry_export_at(fqnb, flen, i, ename, &elen,
                    &kind, esig, &slen) != 0
                    && elen == (uint32_t)strlen(name)
                    && memcmp(ename, name, elen) == 0) {
                snprintf(info->kind, sizeof(info->kind), "%s",
                    at_kind_tag(kind));
                if (slen > 0 && slen < sizeof(esig)) {
                    memcpy(info->sig, esig, slen);
                    info->sig[slen] = 0;
                }
                found = 1;
                break;
            }
        }
        if (!found) {
            /* not a live export: an embedded-source face (documented but not
             * registered, or a card-local helper). kind stays empty — info
             * still resolves with doc/notes/deps from the source table. */
            info->kind[0] = 0;
        }
        at_fill_doc(info);
    } else {
        snprintf(info->kind, sizeof(info->kind), "mod");
        /* card-level: no doc face, but the manifest may carry one */
    }

    /* provenance: the build record (present when fqn was unit_compiled).
     * record_slot is the lock-free matcher — this whole fill runs under the
     * ctx lock (the at() critical section), so the exported record_find
     * (which takes the lock itself) must not be used here. */
    rec = record_slot(fqn);
    if (rec != NULL) {
        info->has_record = 1;
        info->n_sources = rec->n_sources;
        info->n_syms = rec->n_syms;
    }

    at_fill_notes(info);
    at_fill_deps(info);
    slot->valid = 1;
    pm_util_lock_release(&ctx->lock);
    return (pm_metal_build_at_handle_t)((uintptr_t)(slot - ctx->at) + 1u);
}

int32_t pm_metal_build_at_info(pm_metal_build_at_handle_t handle,
    pm_metal_build_at_info_t *info) {
    pm_metal_build_ctx_t *ctx = build_ctx_acquire();
    pm_build_at_slot_t *slot;
    if (info == NULL || ctx == NULL
        || handle == PM_METAL_BUILD_AT_NONE
        || handle > PM_METAL_BUILD_AT_SLOTS) {
        return -1;
    }
    slot = &ctx->at[handle - 1u];
    /* copy under the ctx lock: the epoch round-robin may be mid-reuse of
     * this slot on another core (4 slots, high-churn panes) — the lock
     * makes the copy see one whole slot generation */
    pm_util_lock_acquire(&ctx->lock);
    if (!slot->valid) {
        pm_util_lock_release(&ctx->lock);
        return -1;
    }
    *info = slot->info;
    pm_util_lock_release(&ctx->lock);
    return 0;
}

int32_t pm_metal_build_at_ast(pm_metal_build_at_handle_t handle,
    char *lang_out, size_t lang_max) {
    pm_metal_build_ctx_t *ctx = build_ctx_acquire();
    pm_build_at_slot_t *slot;
    const char *lang;
    if (ctx == NULL || handle == PM_METAL_BUILD_AT_NONE
        || handle > PM_METAL_BUILD_AT_SLOTS) {
        return -1;
    }
    slot = &ctx->at[handle - 1u];
    pm_util_lock_acquire(&ctx->lock);
    if (!slot->valid) {
        pm_util_lock_release(&ctx->lock);
        return -1;
    }
    lang = slot->info.lang[0] != 0 ? slot->info.lang : "c";
    pm_util_lock_release(&ctx->lock);
    if (lang_out != NULL && lang_max > 0) {
        snprintf(lang_out, lang_max, "%s", lang);
    }
    /* Phase 12 fills the C leaf (TCC tree); Rust/C++ editors come later.
     * The spine itself is language-neutral: the dispatch is the contract. */
    if (strcmp(lang, "c") == 0) {
        return 1;
    }
    return 0;
}

PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_unit_parse, pm_metal_build_unit_parse,
    int32_t(pm_util_mem_arena_t *, const uint8_t *, size_t, pm_metal_build_unit_t *,
        char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_graph_resolve, pm_metal_build_graph_resolve,
    int32_t(pm_util_mem_arena_t *, pm_metal_build_unit_t *, uint32_t,
        const pm_metal_build_unit_t ***, uint32_t *, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_compile_source, pm_metal_build_compile_source,
    int32_t(pm_util_mem_arena_t *, const pm_metal_build_unit_t *, const char *,
        const char *, uint8_t **, size_t *, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_compile_source_target, pm_metal_build_compile_source_target,
    int32_t(pm_util_mem_arena_t *, const pm_metal_build_unit_t *, const char *,
        const char *, int32_t, uint8_t **, size_t *, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_link, pm_metal_build_link,
    int32_t(pm_util_mem_arena_t *, const pm_metal_build_unit_t *, uint8_t **,
        const size_t *, uint32_t, pm_metal_build_artifact_t *, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_discover, pm_metal_build_discover,
    int32_t(pm_util_mem_arena_t *, pm_metal_build_unit_t **, uint32_t *,
        char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_unit_compile, pm_metal_build_unit_compile,
    int32_t(pm_util_mem_arena_t *, const pm_metal_build_unit_t *, const pm_metal_build_compile_opts_t *, pm_metal_build_artifact_t *, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_record_find, pm_metal_build_record_find,
    const pm_metal_build_record_t *(const char *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_record_reset, pm_metal_build_record_reset,
    void(void));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_note_add, pm_metal_build_note_add,
    int32_t(const char *, pm_metal_build_note_kind_t, const char *,
        const char *const *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_notes_query, pm_metal_build_notes_query,
    int32_t(const char *, int32_t, char *, size_t, uint32_t *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_note_has, pm_metal_build_note_has,
    int32_t(const char *, pm_metal_build_note_kind_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_ledger_path, pm_metal_build_ledger_path,
    const char *(void));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_artifact_destroy, pm_metal_build_artifact_destroy,
    void(pm_metal_build_artifact_t *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_artifact_lookup, pm_metal_build_artifact_lookup,
    void *(const pm_metal_build_artifact_t *, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_artifact_call, pm_metal_build_artifact_call,
    int32_t(const pm_metal_build_artifact_t *, const char *, const int64_t *, uint32_t, int64_t *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_at, pm_metal_build_at,
    pm_metal_build_at_handle_t(const char *, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_at_info, pm_metal_build_at_info,
    int32_t(pm_metal_build_at_handle_t, pm_metal_build_at_info_t *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_at_ast, pm_metal_build_at_ast,
    int32_t(pm_metal_build_at_handle_t, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_actor_submit, pm_metal_build_actor_submit,
    int32_t(const pm_metal_build_unit_t *, const pm_metal_build_compile_opts_t *, pm_metal_build_actor_job_t **, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_actor_post, pm_metal_build_actor_post,
    int32_t(pm_metal_build_actor_job_t *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_actor_step, pm_metal_build_actor_step,
    pm_metal_coop_status_t(pm_metal_build_actor_job_t *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_actor_run, pm_metal_build_actor_run,
    int32_t(pm_metal_build_actor_job_t *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_actor_cancel, pm_metal_build_actor_cancel,
    int32_t(pm_metal_build_actor_job_t *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_actor_depth, pm_metal_build_actor_depth,
    int32_t(uint32_t *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_actor_release, pm_metal_build_actor_release,
    int32_t(pm_metal_build_actor_job_t *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_dag_run, pm_metal_build_dag_run,
    int32_t(pm_util_mem_arena_t *, pm_metal_build_unit_t *, uint32_t, const pm_metal_build_dag_opts_t *, pm_metal_build_dag_result_t *, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_events_since, pm_metal_build_events_since,
    uint32_t(uint32_t, pm_metal_build_event_t *, uint32_t, uint32_t *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_events_latest, pm_metal_build_events_latest,
    uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_walk_start, pm_metal_build_walk_start,
    int32_t(int32_t, const pm_metal_build_compile_opts_t *, pm_metal_build_root_fn_t, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_walk_state, pm_metal_build_walk_state,
    void(pm_metal_build_walk_info_t *));
PM_MOD_EXPORT_C(pymergetic.metal.build, pm_metal_build_walk_peak, pm_metal_build_walk_peak,
    uint32_t(void));

/*------------------ async compiler actor ----------------------------------
 * One dedicated actor owns EVERY TCC invocation in the process. Reason:
 * TCC's allocator routing is a single global (tcc_set_realloc in libtcc.c
 * plus the arena static in jit.c) — two compiles interleaved on different
 * arenas would corrupt each other's allocations, and no per-invocation
 * context can fix a process-global hook. Serializing here is not a missed
 * optimization: it is the only correct schedule. (Putting s_tcc_arena in
 * a struct would not fix this — the hook itself is global.)
 *
 * Phase 5 layered the correctness point into jit.c itself: the allocator
 * window (pm_metal_jit_c_arena_acquire/release) is one lock-guarded
 * exclusive window around every TCC allocation context transition. The
 * actor's serial section sits ABOVE it (queue discipline: one job at a
 * time, protecting the shared record table and boot-arena scratch). A
 * non-actor caller (direct unit_compile) still cannot corrupt an actor
 * job: the window refuses the overlap. Two locks, two layers, no gap.
 *
 * The queue is bounded (PM_METAL_BUILD_ACTOR_DEPTH). Backpressure is an
 * honest refusal: submit returns PM_METAL_BUILD_ERR_BUSY when full. Jobs
 * are stackless coro frames on the boot arena; a job that cannot get the
 * serial section PARKS (returns WAITING) — never blocks its runner.
 *
 * The serial section runs the compile blocking inside the step that won
 * it (tcc_compile_string is a blocking actor call — documented at the
 * actor_run face). Cancellation lands at phase boundaries only: between
 * unit sources, never inside a TCC invocation. */

typedef struct pm_build_actor {
    pm_util_lock_t lock;            /* guards the queue + serial section */
    pm_metal_build_actor_job_t *q[PM_METAL_BUILD_ACTOR_DEPTH];
    uint32_t q_head, q_tail, q_count;
    uint32_t serial_held;           /* 1 while a job owns the TCC section */
} pm_build_actor_t;

static pm_build_actor_t s_actor;    /* zero-state: lock word 0 = unlocked */

static pm_metal_coop_status_t actor_job_step(pm_metal_coop_coro_t *self);

/* Deep-copy one unit into the boot arena (submit's contract: the job must
 * outlive the caller's arena, so no field may point into it). */
static int32_t actor_unit_copy(pm_util_mem_arena_t *arena,
    const pm_metal_build_unit_t *unit, pm_metal_build_unit_t *out) {
    uint32_t i;
    *out = *unit;
    if (unit->n_sources > 0) {
        out->sources = (const char **)pm_util_mem_alloc(
            arena, unit->n_sources * sizeof(const char *));
        if (out->sources == NULL) {
            return PM_METAL_BUILD_ERR_NOMEM;
        }
        for (i = 0; i < unit->n_sources; i++) {
            out->sources[i] = dup_str(
                arena, unit->sources[i], strlen(unit->sources[i]));
            if (out->sources[i] == NULL) {
                return PM_METAL_BUILD_ERR_NOMEM;
            }
        }
    }
    if (unit->n_include_dirs > 0) {
        out->include_dirs = (const char **)pm_util_mem_alloc(
            arena, unit->n_include_dirs * sizeof(const char *));
        if (out->include_dirs == NULL) {
            return PM_METAL_BUILD_ERR_NOMEM;
        }
        for (i = 0; i < unit->n_include_dirs; i++) {
            out->include_dirs[i] = dup_str(
                arena, unit->include_dirs[i], strlen(unit->include_dirs[i]));
            if (out->include_dirs[i] == NULL) {
                return PM_METAL_BUILD_ERR_NOMEM;
            }
        }
    }
    if (unit->n_defines > 0) {
        out->defines = (const char **)pm_util_mem_alloc(
            arena, unit->n_defines * sizeof(const char *));
        if (out->defines == NULL) {
            return PM_METAL_BUILD_ERR_NOMEM;
        }
        for (i = 0; i < unit->n_defines; i++) {
            out->defines[i] = dup_str(
                arena, unit->defines[i], strlen(unit->defines[i]));
            if (out->defines[i] == NULL) {
                return PM_METAL_BUILD_ERR_NOMEM;
            }
        }
    }
    if (unit->n_depends > 0) {
        out->depends = (const char **)pm_util_mem_alloc(
            arena, unit->n_depends * sizeof(const char *));
        if (out->depends == NULL) {
            return PM_METAL_BUILD_ERR_NOMEM;
        }
        for (i = 0; i < unit->n_depends; i++) {
            out->depends[i] = dup_str(
                arena, unit->depends[i], strlen(unit->depends[i]));
            if (out->depends[i] == NULL) {
                return PM_METAL_BUILD_ERR_NOMEM;
            }
        }
    }
    return PM_METAL_BUILD_OK;
}

int32_t pm_metal_build_actor_submit(
    const pm_metal_build_unit_t *unit,
    const pm_metal_build_compile_opts_t *opts,
    pm_metal_build_actor_job_t **job_out, char *errbuf, size_t errbuf_len) {
    pm_util_mem_arena_t *arena = pm_metal_coop_arena();
    pm_metal_build_actor_job_t *job;
    pm_build_actor_t *a = &s_actor;
    const char *unit_root;
    const char **include_dirs;
    uint32_t n_include_dirs;
    const char **defines;
    uint32_t n_defines;
    uint32_t i;

    if (job_out == NULL || unit == NULL || opts == NULL
        || opts->unit_root == NULL) {
        err_set(errbuf, errbuf_len, "actor_submit: bad args", 0);
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    unit_root = opts->unit_root;
    include_dirs = opts->include_dirs;
    n_include_dirs = opts->n_include_dirs;
    defines = opts->defines;
    n_defines = opts->n_defines;
    *job_out = NULL;
    if (arena == NULL) {
        err_set(errbuf, errbuf_len, "actor_submit: no boot arena", 0);
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    pm_util_lock_acquire(&a->lock);
    if (a->q_count >= PM_METAL_BUILD_ACTOR_DEPTH) {
        pm_util_lock_release(&a->lock);
        err_set(errbuf, errbuf_len, "actor_submit: queue full (backpressure)", 0);
        return PM_METAL_BUILD_ERR_BUSY;
    }
    pm_util_lock_release(&a->lock);

    /* copy the whole request into the boot arena: the job must outlive the
     * caller's compile arena (same lifetime rule as the ctx) */
    job = (pm_metal_build_actor_job_t *)pm_util_mem_alloc(arena, sizeof(*job));
    if (job == NULL) {
        err_set(errbuf, errbuf_len, "actor_submit: arena exhausted", 0);
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    memset(job, 0, sizeof(*job));
    job->coro.step = actor_job_step;
    job->coro.status = PM_METAL_COOP_PENDING;
    job->state = PM_METAL_BUILD_ACTOR_NEW;
    {
        int32_t crc = actor_unit_copy(arena, unit, &job->unit);
        if (crc != PM_METAL_BUILD_OK) {
            err_set(errbuf, errbuf_len, "actor_submit: arena exhausted", 0);
            return crc;
        }
    }
    job->unit_root = dup_str(arena, unit_root, strlen(unit_root));
    if (job->unit_root == NULL) {
        err_set(errbuf, errbuf_len, "actor_submit: arena exhausted", 0);
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    if (n_include_dirs > 0) {
        job->include_dirs = (const char **)pm_util_mem_alloc(
            arena, n_include_dirs * sizeof(const char *));
        if (job->include_dirs == NULL) {
            err_set(errbuf, errbuf_len, "actor_submit: arena exhausted", 0);
            return PM_METAL_BUILD_ERR_NOMEM;
        }
        for (i = 0; i < n_include_dirs; i++) {
            job->include_dirs[i] = dup_str(
                arena, include_dirs[i], strlen(include_dirs[i]));
            if (job->include_dirs[i] == NULL) {
                err_set(errbuf, errbuf_len, "actor_submit: arena exhausted", 0);
                return PM_METAL_BUILD_ERR_NOMEM;
            }
        }
        job->n_include_dirs = n_include_dirs;
    }
    if (n_defines > 0) {
        job->defines = (const char **)pm_util_mem_alloc(
            arena, n_defines * sizeof(const char *));
        if (job->defines == NULL) {
            err_set(errbuf, errbuf_len, "actor_submit: arena exhausted", 0);
            return PM_METAL_BUILD_ERR_NOMEM;
        }
        for (i = 0; i < n_defines; i++) {
            job->defines[i] = dup_str(arena, defines[i], strlen(defines[i]));
            if (job->defines[i] == NULL) {
                err_set(errbuf, errbuf_len, "actor_submit: arena exhausted", 0);
                return PM_METAL_BUILD_ERR_NOMEM;
            }
        }
        job->n_defines = n_defines;
    }
    job->target = opts->target;
    job->mode = opts->mode;

    pm_util_lock_acquire(&a->lock);
    if (a->q_count >= PM_METAL_BUILD_ACTOR_DEPTH) {
        pm_util_lock_release(&a->lock);
        err_set(errbuf, errbuf_len, "actor_submit: queue full (backpressure)", 0);
        return PM_METAL_BUILD_ERR_BUSY;
    }
    a->q[a->q_tail] = job;
    a->q_tail = (a->q_tail + 1u) % PM_METAL_BUILD_ACTOR_DEPTH;
    a->q_count++;
    pm_util_lock_release(&a->lock);
    *job_out = job;
    return PM_METAL_BUILD_OK;
}

/* Give a submitted job its own ring task: the coop runners drive it from
 * here (one step per ring hop — WAITING parks, the section holder's finish
 * re-posts it). The task's root is the job's coro; the task block's
 * lifetime is the coop card's refcount (this face's ref drops when the
 * driving runner unrefs, terminal + auto_free reclaims the rest). The
 * submitter keeps ownership: it still settles + releases the job. */
int32_t pm_metal_build_actor_post(pm_metal_build_actor_job_t *job) {
    if (job == NULL) {
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    if (job->task != NULL) {
        return 0;  /* already posted */
    }
    if (job->state != PM_METAL_BUILD_ACTOR_NEW) {
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    /* the coro must not be auto-free while the job is queued: the job's
     * arena-copied unit (not the coro frame) is the release unit — the
     * submitter releases the whole job via actor_release once terminal */
    job->task = pm_metal_coop_create_task(&job->coro);
    if (job->task == NULL) {
        return PM_METAL_BUILD_ERR_BUSY;
    }
    return 0;
}

/* Dequeue job from the actor FIFO (lock held). A finished job leaves the
 * queue so occupancy reflects live work only. Ring compaction keeps the
 * head/tail arithmetic exact after a middle removal. */
static void actor_dequeue_locked(pm_build_actor_t *a,
    pm_metal_build_actor_job_t *job) {
    uint32_t i;
    for (i = 0; i < PM_METAL_BUILD_ACTOR_DEPTH; i++) {
        if (a->q[i] == job) {
            uint32_t j = i;
            while (j != a->q_tail) {
                uint32_t next = (j + 1u) % PM_METAL_BUILD_ACTOR_DEPTH;
                a->q[j] = a->q[next];
                a->q[next] = NULL;
                j = next;
            }
            a->q_tail = (a->q_tail + PM_METAL_BUILD_ACTOR_DEPTH - 1u)
                % PM_METAL_BUILD_ACTOR_DEPTH;
            a->q_count--;
            return;
        }
    }
}

static void actor_job_finish(pm_build_actor_t *a, pm_metal_build_actor_job_t *job,
    pm_metal_build_actor_state_t state) {
    /* lock held: drop the job from the queue and free the serial section.
     * Promotion is explicit now: every WAITING job still queued is re-posted
     * so a runner re-steps it and the section handoff costs one ring hop
     * instead of waiting for the parked job's next incidental poll. Under
     * fan-out that is the difference between a section-wait and a section
     * STALL: a parked job has no timer and no other driver, its ring ref
     * was consumed by the step that parked it — without this re-post the
     * section can sit idle while a ready job sleeps. */
    pm_metal_build_actor_job_t *wake[PM_METAL_BUILD_ACTOR_DEPTH];
    uint32_t n_wake = 0;
    uint32_t i;
    for (i = 0; i < PM_METAL_BUILD_ACTOR_DEPTH; i++) {
        if (a->q[i] != NULL
            && a->q[i]->state == PM_METAL_BUILD_ACTOR_WAITING) {
            wake[n_wake++] = a->q[i];
        }
    }
    actor_dequeue_locked(a, job);
    job->state = state;
    job->coro.status = state == PM_METAL_BUILD_ACTOR_DONE
        ? PM_METAL_COOP_DONE
        : (state == PM_METAL_BUILD_ACTOR_CANCELLED
            ? PM_METAL_COOP_CANCELLED
            : PM_METAL_COOP_ERROR);
    a->serial_held = 0u;
    pm_util_lock_release(&a->lock);
    /* re-post outside the lock: post takes the ring lock, never ours */
    for (i = 0; i < n_wake; i++) {
        (void)pm_metal_coop_post_task(wake[i]->task);
    }
}

/* The job's own compile, run while it owns the serial section. Mirrors
 * pm_metal_build_unit_compile's loop, but re-entrant at phase boundaries:
 * next_src is the cursor. */
/* Free one job's compile scratch. Every early exit of actor_job_run_locked
 * calls this: a cancelled or failed compile leaves no all_includes joins,
 * no arrays, no TCC object bytes behind in the boot arena. The blocks were
 * allocated from the tlsf heap, so the free genuinely reclaims — this is
 * the path the 10,000-job stress test measures as stable high-water. */
static void actor_scratch_free(pm_util_mem_arena_t *arena,
    uint8_t **objs, size_t *lens, const char **compiled_srcs,
    const char **all_includes, uint32_t n_all_includes,
    const char **all_defines, uint32_t n_compiled) {
    uint32_t i;
    if (objs != NULL) {
        for (i = 0; i < n_compiled; i++) {
            pm_util_mem_free(arena, objs[i]);
        }
        pm_util_mem_free(arena, objs);
    }
    if (lens != NULL) {
        pm_util_mem_free(arena, lens);
    }
    if (compiled_srcs != NULL) {
        pm_util_mem_free(arena, compiled_srcs);
    }
    if (all_includes != NULL) {
        /* [0] and [1..unit.n_include_dirs] are join_path products;
         * [1+n_unit_includes..] alias the caller's option arrays — not
         * ours to free (distinguish by tracking how many we joined) */
        for (i = 0; i < n_all_includes; i++) {
            pm_util_mem_free(arena, (void *)all_includes[i]);
        }
        pm_util_mem_free(arena, all_includes);
    }
    if (all_defines != NULL) {
        /* entries alias the caller's option arrays — only the array block */
        pm_util_mem_free(arena, all_defines);
    }
}

static int32_t actor_job_run_locked(pm_metal_build_actor_job_t *job) {
    pm_util_mem_arena_t *arena = pm_metal_coop_arena();
    const pm_metal_src_card_t *c = pm_metal_src_find(job->unit.fqn);
    uint8_t **objs = NULL;
    size_t *lens = NULL;
    const char **compiled_srcs = NULL;
    const char **all_includes = NULL;
    uint32_t n_all_includes = 0;
    /* how many of all_includes' entries are join_path products we own */
    uint32_t n_joined_includes = 0;
    const char **all_defines = NULL;
    uint32_t n_all_defines = job->unit.n_defines + job->n_defines;
    uint32_t i;
    uint32_t n_objs = 0;
    int32_t rc;

    if (arena == NULL) {
        err_set(job->err, sizeof(job->err), "actor: no boot arena", 0);
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    if (c == NULL) {
        err_set(job->err, sizeof(job->err), "actor: card not in embed", 0);
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    /* The compile runs in the job's own scratch span — ksweep's posture,
     * one fresh arena per unit: a big unit's TCC tokstr wants a 16 MiB
     * contiguous block that the shared boot arena cannot reliably serve
     * next to the seat's retained state (the walk's BUILD ALL crashed
     * exactly there: tal_new's 16 MiB ask against a fragmented 64 MiB
     * boot arena). The artifact is self-owned (its ELF image holds its
     * own mappings), so nothing the caller keeps survives here. When
     * malloc refuses, the boot arena is the fallback — firmware's shim
     * routes back to it, and that seat's cards fit. */
    if (job->scratch == NULL) {
        job->scratch_backing = malloc(PM_METAL_BUILD_JOB_SPAN);
        if (job->scratch_backing != NULL) {
            job->scratch = pm_util_mem_arena_create(
                job->scratch_backing, PM_METAL_BUILD_JOB_SPAN);
            if (job->scratch == NULL) {
                free(job->scratch_backing);
                job->scratch_backing = NULL;
            }
        }
    }
    if (job->scratch != NULL) {
        arena = job->scratch;
    }
    if (strcmp(job->unit.impl, "py") == 0) {
        /* the py path allocates from the caller arena in unit_compile_py;
         * the actor's serial section runs it identically — and the mpy
         * bytes are arena-owned (artifact_destroy only NULLs them), so py
         * stays on the boot arena: the scratch span dies at release */
        memset(&job->artifact, 0, sizeof(job->artifact));
        arena = pm_metal_coop_arena();
        rc = unit_compile_py(arena, &job->unit, &job->artifact,
            job->err, sizeof(job->err));
        return rc;
    }

    /* the include/define joins are compile-scratch: freed on every exit
     * path of this function (actor_scratch_free) — the boot arena's tlsf
     * heap reclaims them, keeping high-water stable across many jobs */
    n_all_includes = 1u + job->unit.n_include_dirs + job->n_include_dirs;
    all_includes = (const char **)pm_util_mem_alloc(
        arena, n_all_includes * sizeof(const char *));
    objs = (uint8_t **)pm_util_mem_alloc(
        arena, job->unit.n_sources * sizeof(uint8_t *));
    lens = (size_t *)pm_util_mem_alloc(
        arena, job->unit.n_sources * sizeof(size_t));
    compiled_srcs = (const char **)pm_util_mem_alloc(
        arena, job->unit.n_sources * sizeof(const char *));
    if (all_includes == NULL || objs == NULL || lens == NULL
        || compiled_srcs == NULL) {
        err_set(job->err, sizeof(job->err), "actor: arena exhausted", 0);
        actor_scratch_free(arena, objs, lens, compiled_srcs,
            all_includes, 0, all_defines, 0);
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    all_includes[0] = join_path(arena, job->unit_root, ".");
    if (all_includes[0] == NULL) {
        err_set(job->err, sizeof(job->err), "actor: arena exhausted", 0);
        actor_scratch_free(arena, objs, lens, compiled_srcs,
            all_includes, 0, all_defines, 0);
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    n_joined_includes = 1u;
    for (i = 0; i < job->unit.n_include_dirs; i++) {
        all_includes[1 + i] = join_path(
            arena, job->unit_root, job->unit.include_dirs[i]);
        if (all_includes[1 + i] == NULL) {
            err_set(job->err, sizeof(job->err), "actor: arena exhausted", 0);
            actor_scratch_free(arena, objs, lens, compiled_srcs,
                all_includes, n_joined_includes, all_defines, 0);
            return PM_METAL_BUILD_ERR_NOMEM;
        }
        n_joined_includes++;
    }
    for (i = 0; i < job->n_include_dirs; i++) {
        all_includes[1 + job->unit.n_include_dirs + i] = job->include_dirs[i];
    }
    if (n_all_defines > 0) {
        all_defines = (const char **)pm_util_mem_alloc(
            arena, n_all_defines * sizeof(const char *));
        if (all_defines == NULL) {
            err_set(job->err, sizeof(job->err), "actor: arena exhausted", 0);
            actor_scratch_free(arena, objs, lens, compiled_srcs,
                all_includes, n_joined_includes, all_defines, 0);
            return PM_METAL_BUILD_ERR_NOMEM;
        }
        for (i = 0; i < job->unit.n_defines; i++) {
            all_defines[i] = job->unit.defines[i];
        }
        for (i = 0; i < job->n_defines; i++) {
            all_defines[job->unit.n_defines + i] = job->defines[i];
        }
    }

    for (job->next_src = 0; job->next_src < job->unit.n_sources;
        job->next_src++) {
        const char *src = NULL;
        const char *dot;
        /* the only cancellation point: between unit sources */
        if (__atomic_load_n(&job->cancel, __ATOMIC_ACQUIRE) != 0u) {
            actor_scratch_free(arena, objs, lens, compiled_srcs,
                all_includes, n_joined_includes, all_defines, n_objs);
            return PM_METAL_BUILD_ERR_CANCELLED;
        }
        {
            uint32_t f;
            for (f = 0; f < c->nfiles; f++) {
                if (strcmp(c->files[f].rel, job->unit.sources[job->next_src]) == 0) {
                    src = (const char *)c->files[f].data;
                    break;
                }
            }
        }
        if (src == NULL) {
            err_set(job->err, sizeof(job->err), "actor: source not in embed", 0);
            actor_scratch_free(arena, objs, lens, compiled_srcs,
                all_includes, n_joined_includes, all_defines, n_objs);
            return PM_METAL_BUILD_ERR_COMPILE;
        }
        dot = strrchr(job->unit.sources[job->next_src], '.');
        if (dot != NULL && strcmp(dot, ".h") == 0) {
            continue;
        }
        {
            uint64_t t0 = pm_metal_coop_mono_us();
            build_event_emit(PM_METAL_BUILD_EVENT_COMPILE_START,
                (uint16_t)(job->target & 0xffffu), job->unit.fqn,
                job->unit.sources[job->next_src], 0, 0);
            if (unit_source_compile(arena, job->unit.fqn,
                    job->unit.sources[job->next_src], src,
                    all_includes, n_all_includes, all_defines, n_all_defines,
                    job->target,
                    &objs[n_objs], &lens[n_objs], job->err, sizeof(job->err))
                    != PM_METAL_BUILD_OK) {
                build_event_emit(PM_METAL_BUILD_EVENT_UNIT_FAIL,
                    (uint16_t)(job->target & 0xffffu), job->unit.fqn,
                    job->unit.sources[job->next_src],
                    (uint32_t)((pm_metal_coop_mono_us() - t0) & 0xffffffffu), 0);
                actor_scratch_free(arena, objs, lens, compiled_srcs,
                    all_includes, n_joined_includes, all_defines, n_objs);
                return PM_METAL_BUILD_ERR_COMPILE;
            }
            build_event_emit(PM_METAL_BUILD_EVENT_COMPILE_END,
                (uint16_t)(job->target & 0xffffu), job->unit.fqn,
                job->unit.sources[job->next_src],
                (uint32_t)((pm_metal_coop_mono_us() - t0) & 0xffffffffu),
                (uint32_t)lens[n_objs]);
        }
        compiled_srcs[n_objs] = job->unit.sources[job->next_src];
        n_objs++;
    }
    if (n_objs == 0) {
        err_set(job->err, sizeof(job->err), "actor: no compilable sources", 0);
        actor_scratch_free(arena, objs, lens, compiled_srcs,
            all_includes, n_joined_includes, all_defines, n_objs);
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    /* Same reason as unit_compile's keep: the link consumes the objects and
     * the scratch free below reclaims them. A walk's units are downloadable
     * on the same terms as a one-at-a-time rebuild. */
    build_keep_objects(job->unit.fqn, compiled_srcs, objs, lens, n_objs);

    /* Link only what this seat can load. A cross lane's product is the
     * object file itself — an armv7 ET_REL is a distribution artifact, and
     * this seat's relocator rightly refuses it ("e_machine != host arch").
     * Attempting it anyway turned a working emit into 80 failed rows and
     * made a cross-lane BUILD ALL look like a broken tree. The objects are
     * retained above, so the emit is what the caller downloads. */
    if (job->mode == PM_METAL_BUILD_PRODUCE
        || build_target_is_cross(job->target)) {
        rc = PM_METAL_BUILD_OK;
    } else {
        rc = pm_metal_build_link(arena, &job->unit, objs, lens, n_objs,
            &job->artifact, job->err, sizeof(job->err));
    }
    if (rc == PM_METAL_BUILD_OK) {
        /* provenance record (same shape as unit_compile) — written before
         * the scratch free: rec->src_paths/obj_lens copy out of
         * compiled_srcs/lens, they must still be alive here. One ctx-lock
         * section (pick, fields, sym walk, publish): the actor's job may
         * run on any core while a pane reads the table. */
        pm_metal_build_ctx_t *rctx = build_ctx_acquire();
        if (rctx != NULL) {
            pm_metal_build_record_t *rec;
            pm_util_lock_acquire(&rctx->lock);
            rec = record_slot_acquire_locked(job->unit.fqn);
            if (rec != NULL) {
                uint32_t cap = n_objs;
                if (cap > PM_METAL_BUILD_MAX_OBJS) {
                    cap = PM_METAL_BUILD_MAX_OBJS;
                }
                for (i = 0; i < cap; i++) {
                    snprintf(rec->src_paths[i], PM_METAL_BUILD_MAX_SRC_PATH, "%s",
                        compiled_srcs[i]);
                    rec->obj_lens[i] = (uint32_t)lens[i];
                }
                rec->n_sources = cap;
#ifdef PM_METAL_BUILD_HAS_ELF
                {
                    pm_build_rec_sym_ctx_t sctx;
                    sctx.r = rec;
                    sctx.w = 0;
                    mp_wasm_elf_foreach_func(
                        (const mp_wasm_elf_image_t *)job->artifact.bytes,
                        record_sym_cb, &sctx);
                    rec->n_syms = sctx.w;
                }
#endif
                record_publish_locked(rec);
            }
            pm_util_lock_release(&rctx->lock);
        }
    }
    /* the artifact owns its bytes (mmap'd image or loader handles) — the
     * TCC object buffers were consumed by the link; free them with the
     * rest of the scratch on every exit below. The record holds copies,
     * nothing below reads the freed scratch. */
    actor_scratch_free(arena, objs, lens, compiled_srcs,
        all_includes, n_joined_includes, all_defines, n_objs);
    return rc;
}

static pm_metal_coop_status_t actor_job_step(pm_metal_coop_coro_t *self) {
    pm_metal_build_actor_job_t *job =
        (pm_metal_build_actor_job_t *)self;
    pm_build_actor_t *a = &s_actor;

    if (job == NULL) {
        return PM_METAL_COOP_ERROR;
    }
    if (job->state == PM_METAL_BUILD_ACTOR_DONE
        || job->state == PM_METAL_BUILD_ACTOR_FAILED
        || job->state == PM_METAL_BUILD_ACTOR_CANCELLED) {
        return job->coro.status;
    }
    pm_util_lock_acquire(&a->lock);
    if (a->serial_held != 0u) {
        /* serial section busy: park. The holder releases and dequeues when
         * it finishes; a later step of this job re-checks the flag. */
        job->state = PM_METAL_BUILD_ACTOR_WAITING;
        job->coro.status = PM_METAL_COOP_WAITING;
        pm_util_lock_release(&a->lock);
        return PM_METAL_COOP_WAITING;
    }
    /* free serial section: this step takes it and runs the whole job
     * (blocking TCC inside the step — documented at actor_run). */
    a->serial_held = 1u;
    job->state = PM_METAL_BUILD_ACTOR_RUNNING;
    pm_util_lock_release(&a->lock);

    job->rc = actor_job_run_locked(job);

    pm_util_lock_acquire(&a->lock);
    if (job->rc == PM_METAL_BUILD_ERR_CANCELLED) {
        actor_job_finish(a, job, PM_METAL_BUILD_ACTOR_CANCELLED);
        return PM_METAL_COOP_CANCELLED;
    }
    if (job->rc != PM_METAL_BUILD_OK) {
        actor_job_finish(a, job, PM_METAL_BUILD_ACTOR_FAILED);
        return PM_METAL_COOP_ERROR;
    }
    actor_job_finish(a, job, PM_METAL_BUILD_ACTOR_DONE);
    return PM_METAL_COOP_DONE;
}

pm_metal_coop_status_t pm_metal_build_actor_step(pm_metal_build_actor_job_t *job) {
    if (job == NULL) {
        return PM_METAL_COOP_ERROR;
    }
    return actor_job_step(&job->coro);
}

int32_t pm_metal_build_actor_run(pm_metal_build_actor_job_t *job) {
    pm_metal_coop_status_t st;
    uint32_t guard = 0;

    if (job == NULL) {
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    for (;;) {
        st = actor_job_step(&job->coro);
        if (st == PM_METAL_COOP_DONE) {
            return job->rc;
        }
        if (st == PM_METAL_COOP_CANCELLED) {
            return job->rc == 0 ? PM_METAL_BUILD_ERR_CANCELLED : job->rc;
        }
        if (st == PM_METAL_COOP_ERROR) {
            return job->rc != 0 ? job->rc : PM_METAL_BUILD_ERR_COMPILE;
        }
        /* WAITING: the serial section is held by another job. Pump the
         * runner so the holder (and the rest of the ring) makes progress;
         * re-check with a bounded guard so a wedged holder cannot spin us
         * forever. This face is the documented blocking actor call. */
        pm_metal_coop_poll();
        guard++;
        if (guard > 10000000u) {
            err_set(job->err, sizeof(job->err),
                "actor_run: serial section holder wedged", 0);
            return PM_METAL_BUILD_ERR_BUSY;
        }
    }
}

int32_t pm_metal_build_actor_cancel(pm_metal_build_actor_job_t *job) {
    if (job == NULL) {
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    if (job->state == PM_METAL_BUILD_ACTOR_DONE
        || job->state == PM_METAL_BUILD_ACTOR_FAILED
        || job->state == PM_METAL_BUILD_ACTOR_CANCELLED) {
        return 0;
    }
    __atomic_store_n(&job->cancel, 1u, __ATOMIC_RELEASE);
    return 0;
}

int32_t pm_metal_build_actor_depth(uint32_t *depth) {
    if (depth == NULL) {
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    pm_util_lock_acquire(&s_actor.lock);
    *depth = s_actor.q_count;
    pm_util_lock_release(&s_actor.lock);
    return 0;
}

int32_t pm_metal_build_actor_release(pm_metal_build_actor_job_t *job) {
    pm_util_mem_arena_t *arena;
    uint32_t i;

    if (job == NULL) {
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    if (job->state != PM_METAL_BUILD_ACTOR_DONE
        && job->state != PM_METAL_BUILD_ACTOR_FAILED
        && job->state != PM_METAL_BUILD_ACTOR_CANCELLED) {
        /* a queued/running job is still reachable from the queue —
         * releasing it would leave a dangling pointer in q[] */
        return PM_METAL_BUILD_ERR_BUSY;
    }
    /* A posted job's ring task dies BEFORE the job block: the task's
     * root points at job->coro (inside this block), and the coop
     * reclaimer reads root->auto_free when the last ref drops. Detach
     * clears the root first (the reclaimer then frees the task block
     * alone, whenever the last in-flight ref drops — possibly on
     * another core, after this release returns), so freeing the job
     * here can never leave the reclaimer reading freed memory. The
     * job's coro is never auto_free (the release unit is this whole
     * block). */
    if (job->task != NULL) {
        /* Retire, not just detach: this function frees the job block, and
         * job->coro lives inside it. The runner that drove the final step
         * is still in its epilogue (it stores the step's status into the
         * coro and reads the root) at the moment the job's own state goes
         * terminal, which is what the caller settled on. Freeing here
         * would let that epilogue write into a released block and then
         * free a garbage pointer. */
        if (pm_metal_coop_task_retire(job->task) != 0) {
            return PM_METAL_BUILD_ERR_BUSY;  /* keep the block, retry */
        }
        job->task = NULL;
    }
    arena = pm_metal_coop_arena();
    if (arena == NULL) {
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    /* free the deep-copied spans first, then the arrays, then the job
     * block: the tlsf heap reclaims each (the boot arena never rewinds
     * at runtime, so this is the only reclaim path for spent jobs) */
    if (job->unit.sources != NULL) {
        for (i = 0; i < job->unit.n_sources; i++) {
            pm_util_mem_free(arena, (void *)job->unit.sources[i]);
        }
        pm_util_mem_free(arena, (void *)job->unit.sources);
    }
    if (job->unit.include_dirs != NULL) {
        for (i = 0; i < job->unit.n_include_dirs; i++) {
            pm_util_mem_free(arena, (void *)job->unit.include_dirs[i]);
        }
        pm_util_mem_free(arena, (void *)job->unit.include_dirs);
    }
    if (job->unit.defines != NULL) {
        for (i = 0; i < job->unit.n_defines; i++) {
            pm_util_mem_free(arena, (void *)job->unit.defines[i]);
        }
        pm_util_mem_free(arena, (void *)job->unit.defines);
    }
    if (job->unit.depends != NULL) {
        for (i = 0; i < job->unit.n_depends; i++) {
            pm_util_mem_free(arena, (void *)job->unit.depends[i]);
        }
        pm_util_mem_free(arena, (void *)job->unit.depends);
    }
    /* unit.fqn/impl/version are inline char[] (copied by value in
     * actor_unit_copy) — not arena blocks, never freed. The artifact's
     * bytes stay with the caller: release frees the job bookkeeping,
     * pm_metal_build_artifact_destroy stays the artifact's own face. */
    if (job->unit_root != NULL) {
        pm_util_mem_free(arena, (void *)job->unit_root);
    }
    if (job->include_dirs != NULL) {
        for (i = 0; i < job->n_include_dirs; i++) {
            pm_util_mem_free(arena, (void *)job->include_dirs[i]);
        }
        pm_util_mem_free(arena, (void *)job->include_dirs);
    }
    if (job->defines != NULL) {
        for (i = 0; i < job->n_defines; i++) {
            pm_util_mem_free(arena, (void *)job->defines[i]);
        }
        pm_util_mem_free(arena, (void *)job->defines);
    }
    /* The compile's scratch span dies with the job (the artifact is
     * self-owned; records hold copies — nothing retained survives it).
     * Freed after the boot-arena blocks above so the arena destroy's
     * own bookkeeping never races a live heap. */
    if (job->scratch != NULL) {
        pm_util_mem_arena_destroy(job->scratch);
        job->scratch = NULL;
    }
    if (job->scratch_backing != NULL) {
        free(job->scratch_backing);
        job->scratch_backing = NULL;
    }
    pm_util_mem_free(arena, job);
    return PM_METAL_BUILD_OK;
}

/*------------------ dependency DAG executor (Phase 5) ------------------*/

/* Find a unit's index by fqn (the DAG's identity map). */
static int32_t dag_find(const pm_metal_build_unit_t *units, uint32_t n_units,
    const char *fqn) {
    uint32_t i;
    for (i = 0; i < n_units; i++) {
        if (strcmp(units[i].fqn, fqn) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

/* Is unit i buildable right now? Every named dep must be DONE in rows. */
static int dag_deps_ready(const pm_metal_build_unit_t *u,
    const pm_metal_build_unit_t *units, uint32_t n_units,
    const pm_metal_build_dag_row_t *rows) {
    uint32_t d;
    for (d = 0; d < u->n_depends; d++) {
        int32_t di = dag_find(units, n_units, u->depends[d]);
        if (di < 0) {
            return 0;  /* graph_resolve already refused this shape */
        }
        if (rows[di].state != PM_METAL_BUILD_DAG_DONE) {
            return 0;
        }
    }
    return 1;
}

int32_t pm_metal_build_dag_run(pm_util_mem_arena_t *arena,
    pm_metal_build_unit_t *units, uint32_t n_units,
    const pm_metal_build_dag_opts_t *opts,
    pm_metal_build_dag_result_t *out,
    char *errbuf, size_t errbuf_len) {
    const pm_metal_build_unit_t **order = NULL;
    uint32_t n_order = 0;
    pm_metal_build_dag_row_t *rows = NULL;
    uint32_t n_done = 0;
    uint32_t n_failed = 0;
    uint32_t n_skipped = 0;
    uint32_t progressed;
    uint32_t i;
    int32_t rc;
    pm_metal_build_compile_opts_t unit_opts;
    pm_metal_build_root_fn_t root_fn;
    char *unit_root = NULL;

    if (arena == NULL || units == NULL || out == NULL || opts == NULL
        || opts->root_fn == NULL || opts->compile.unit_root == NULL) {
        err_set(errbuf, errbuf_len, "dag_run: bad args", 0);
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    root_fn = opts->root_fn;
    /* the per-job fill is the run's compile opts with unit_root resolved
     * per unit by the root resolver */
    unit_opts = opts->compile;
    out->rows = NULL;
    out->n_rows = 0;
    out->n_done = 0;
    out->n_failed = 0;
    out->n_skipped = 0;
    if (n_units == 0) {
        return PM_METAL_BUILD_OK;
    }

    /* the topological order first: cycles and missing deps refuse here,
     * before any compile runs (nothing partial) */
    rc = pm_metal_build_graph_resolve(arena, units, n_units,
        &order, &n_order, errbuf, errbuf_len);
    if (rc != PM_METAL_BUILD_OK) {
        return rc;
    }

    rows = (pm_metal_build_dag_row_t *)pm_util_mem_alloc(
        arena, n_units * sizeof(pm_metal_build_dag_row_t));
    if (rows == NULL) {
        err_set(errbuf, errbuf_len, "dag_run: arena exhausted", 0);
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    memset(rows, 0, n_units * sizeof(pm_metal_build_dag_row_t));
    for (i = 0; i < n_units; i++) {
        snprintf(rows[i].fqn, sizeof(rows[i].fqn), "%s", units[i].fqn);
        rows[i].state = PM_METAL_BUILD_DAG_PENDING;
    }

    /* Kahn-style sweep over the resolved order. Each pass consumes every
     * unit whose deps are settled, so one pass per dependency level —
     * the order array guarantees progress, and the sweep terminates when
     * a full pass adds nothing (every remaining unit is SKIPPED, its dep
     * failed). */
    unit_root = (char *)pm_util_mem_alloc(arena, PM_METAL_BUILD_ROOT_MAX);
    if (unit_root == NULL) {
        err_set(errbuf, errbuf_len, "dag_run: arena exhausted", 0);
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    /* every job's submit copies the root out of this one arena buffer
     * before the next iteration overwrites it */
    unit_opts.unit_root = unit_root;
    progressed = 1;
    while (progressed != 0) {
        progressed = 0;
        for (i = 0; i < n_order; i++) {
            const pm_metal_build_unit_t *u = order[i];
            int32_t ui = dag_find(units, n_units, u->fqn);
            pm_metal_build_actor_job_t *job = NULL;
            int32_t jrc;

            if (ui < 0) {
                continue;  /* cannot happen: order came from units */
            }
            if (rows[ui].state != PM_METAL_BUILD_DAG_PENDING) {
                continue;  /* already settled */
            }
            if (!dag_deps_ready(u, units, n_units, rows)) {
                /* a dep is not DONE: either still PENDING (a later pass
                 * settles it) or terminally failed/skipped — mark the
                 * isolation now, the row keeps its error for inspection */
                uint32_t d;
                int blocked = 0;
                for (d = 0; d < u->n_depends; d++) {
                    int32_t di = dag_find(units, n_units, u->depends[d]);
                    if (di >= 0 && (rows[di].state == PM_METAL_BUILD_DAG_FAILED
                            || rows[di].state == PM_METAL_BUILD_DAG_SKIPPED)) {
                        snprintf(rows[ui].err, sizeof(rows[ui].err),
                            "dependency %s %s", rows[di].fqn,
                            rows[di].state == PM_METAL_BUILD_DAG_FAILED
                                ? "failed" : "skipped");
                        blocked = 1;
                        break;
                    }
                }
                if (blocked) {
                    rows[ui].state = PM_METAL_BUILD_DAG_SKIPPED;
                    n_skipped++;
                    progressed = 1;
                }
                continue;
            }

            /* deps DONE: build this unit through the actor */
            if (root_fn(u->fqn, unit_root, PM_METAL_BUILD_ROOT_MAX) != 0) {
                rows[ui].state = PM_METAL_BUILD_DAG_FAILED;
                rows[ui].rc = PM_METAL_BUILD_ERR_COMPILE;
                snprintf(rows[ui].err, sizeof(rows[ui].err),
                    "root resolver refused");
                n_failed++;
                progressed = 1;
                continue;
            }
            rc = pm_metal_build_actor_submit(u, &unit_opts,
                &job, rows[ui].err, sizeof(rows[ui].err));
            if (rc == PM_METAL_BUILD_ERR_BUSY) {
                /* the queue holds only this run's live jobs and each is
                 * driven to completion before the next submit, so a full
                 * queue cannot legitimately happen — refuse loudly */
                err_set(errbuf, errbuf_len, "dag_run: actor queue wedged", 0);
                return PM_METAL_BUILD_ERR_BUSY;
            }
            if (rc != PM_METAL_BUILD_OK) {
                rows[ui].state = PM_METAL_BUILD_DAG_FAILED;
                rows[ui].rc = rc;
                n_failed++;
                progressed = 1;
                continue;
            }
            rows[ui].state = PM_METAL_BUILD_DAG_RUNNING;
            jrc = pm_metal_build_actor_run(job);
            if (jrc == PM_METAL_BUILD_OK) {
                rows[ui].state = PM_METAL_BUILD_DAG_DONE;
                rows[ui].image_len = job->artifact.len;
                n_done++;
                /* the image belongs to the run's caller via rows[] as
                 * lengths only — the linked image itself is the caller's
                 * to keep or destroy; the job block is reclaimable now */
                pm_metal_build_artifact_destroy(&job->artifact);
            } else if (jrc == PM_METAL_BUILD_ERR_CANCELLED) {
                rows[ui].state = PM_METAL_BUILD_DAG_SKIPPED;
                snprintf(rows[ui].err, sizeof(rows[ui].err), "cancelled");
                n_skipped++;
            } else {
                rows[ui].state = PM_METAL_BUILD_DAG_FAILED;
                rows[ui].rc = jrc;
                snprintf(rows[ui].err, sizeof(rows[ui].err), "%s", job->err);
                n_failed++;
            }
            /* every terminal job is released: the DAG never leaves job
             * blocks behind in the boot arena (rows[] carry the results,
             * the image was destroyed or belongs to a FAILED row's
             * nothing — FAILED rows have no image) */
            {
                int32_t rrc = pm_metal_build_actor_release(job);
                if (rrc != PM_METAL_BUILD_OK) {
                    err_set(errbuf, errbuf_len,
                        "dag_run: terminal job release refused", 0);
                    return PM_METAL_BUILD_ERR_BUSY;
                }
            }
            progressed = 1;
        }
    }

    out->rows = rows;
    out->n_rows = n_units;
    out->n_done = n_done;
    out->n_failed = n_failed;
    out->n_skipped = n_skipped;
    return PM_METAL_BUILD_OK;
}

/*------------------ background walk (async BUILD ALL) ------------------
 * One coop task per walk; the frame (units copy + per-unit cursor +
 * result counters) lives in the boot arena via coro_create. The step is
 * one reschedule quantum: settle ONE unit — submit its actor job as a
 * ring task, await it (the actor's serial section keeps TCC serialized
 * inside the job's own step), then return WAITING so the runner re-posts
 * the walk after the ring has drained everything else. The pane thread
 * answered long ago; the inspect console's factory-floor pane watches the
 * event ring.
 *
 * Row states ride the units copy (walk_row_state below) — same Kahn pass
 * shape as dag_run, spread over N runner quanta instead of one C call. */
typedef enum pm_build_walk_row {
    PM_BUILD_WALK_ROW_PENDING = 0,
    PM_BUILD_WALK_ROW_RUNNING = 1,
    PM_BUILD_WALK_ROW_DONE = 2,
    PM_BUILD_WALK_ROW_FAILED = 3,
    PM_BUILD_WALK_ROW_SKIPPED = 4,
} pm_build_walk_row_t;

/* Fan-out lanes: how many units the walk keeps in flight at once. TCC
 * itself stays one-at-a-time (the reallocator is a process-global), so
 * the parallelism is pipeline-shaped — one job compiles while the rest
 * park, and the section handoff costs one ring hop. The lane count
 * bounds the boot-arena high-water (each job holds arena-copied unit +
 * fill) and stays under PM_METAL_BUILD_ACTOR_DEPTH so the actor queue
 * is never the bottleneck the walk overflows. 4: the seats' runner
 * count (SMP boards boot 4), one holder + three parked. */
#define PM_METAL_BUILD_WALK_FANOUT 4u

typedef struct pm_build_walk {
    pm_metal_coop_coro_t coro;      /* first: the task's root coro */
    const pm_metal_build_unit_t **order;  /* graph-resolved, boot arena */
    pm_metal_build_unit_t *units;   /* deep copies, boot arena */
    uint32_t n_units;
    uint32_t *row;                  /* per-unit pm_build_walk_row_t, boot arena */
    /* Per-unit retries spent on a transient refusal (boot arena). A py-impl
     * unit compiles under the µPy GIL, which the caller's own Python holds
     * between polls, so one unit in a walk would fail at random with "GIL
     * busy" — a fact about timing, not about the unit. Bounded so a lane
     * that is genuinely stuck still settles as FAILED. */
    uint32_t *retry;
    pm_metal_build_actor_job_t *jobs[PM_METAL_BUILD_WALK_FANOUT]; /* lanes */
    /* Lane already counted in the census, but its job block is not free
     * yet: the release waits for the runner to leave the job's step, and
     * refuses while it is still inside. Without this the retry would count
     * the same unit again on every quantum. */
    uint32_t counted[PM_METAL_BUILD_WALK_FANOUT];
    uint32_t inline_cnt;            /* lanes the walk drives itself (ring full) */
    /* Most lanes ever live at once. The live count is a snapshot, so an
     * outside observer can only sample it and can miss an overlap that
     * opened and closed between two of its polls; the walk keeps the
     * high-water itself so "did this fan out" is answerable after the
     * fact. */
    uint32_t peak_running;
    pm_metal_build_root_fn_t root_fn;
    pm_metal_build_compile_opts_t opts;   /* deep-copied seat fill */
    char *unit_root;                /* one scratch buffer, reused per unit */
    /* the deep-copied string arrays, for the next start's free walk */
    const char **inc_copy;
    const char **def_copy;
    pm_metal_build_walk_info_t info;
} pm_build_walk_t;

static pm_build_walk_t *s_walk;

/* Reclaim a finished walk's boot-arena spans so a new start does not
 * grow the heap by one walk per BUILD ALL press. The strings inside
 * inc/def/units were dup'd individually — walk each array. The frame
 * itself is freed last (the arena is tlsf-backed, so these frees really
 * reclaim, same contract as actor_release). */
static void walk_release(pm_build_walk_t *w) {
    pm_util_mem_arena_t *arena;
    uint32_t i;
    if (w == NULL) {
        return;
    }
    arena = pm_metal_coop_arena();
    if (arena == NULL) {
        return;
    }
    /* Retire the walk's own coop task FIRST, before anything here frees the
     * frame it points into. The task's root is &w->coro, so a ring slot that
     * outlives this release has a runner step a freed frame: walk_step then
     * runs over a recycled w, settles whatever its jobs[] happens to contain
     * and releases those garbage pointers — which is how the seat aborted in
     * tlsf_free on a block that the allocator had never handed out. It takes
     * a previous walk to recycle the memory, so the first walk of a process
     * looked fine and the crash arrived a walk or two later.
     *
     * Marking the coro terminal is what lets detach accept it; detach clears
     * the root, so any surviving slot becomes a no-op (step_task returns on
     * a NULL root) and the last ref frees the task block on its own. */
    if (w->coro.task != NULL) {
        if (w->coro.status != (uint32_t)PM_METAL_COOP_DONE
            && w->coro.status != (uint32_t)PM_METAL_COOP_ERROR
            && w->coro.status != (uint32_t)PM_METAL_COOP_CANCELLED) {
            w->coro.status = (uint32_t)PM_METAL_COOP_CANCELLED;
        }
        /* Unlike the job case there is no later quantum to retry in: this
         * runs on the starter's own thread, so wait for the runner to
         * leave the step. Waiting here starves nothing — the runners are
         * their own threads and this one is not among them. If it somehow
         * does not clear, leave the frame alone: leaking one walk frame is
         * harmless next to freeing memory a runner is still writing. */
        uint64_t deadline = pm_metal_coop_mono_us() + 2000000u;
        while (pm_metal_coop_task_retire(w->coro.task) != 0) {
            if (pm_metal_coop_mono_us() > deadline) {
                return;
            }
        }
        w->coro.task = NULL;
    }
    /* lane jobs: a DONE walk drained its lanes already (the terminal
     * check requires live == 0), but a walk that never ran a full round
     * (an error start) may still hold terminal/queued jobs — release
     * them so their arena-copied unit + fill do not leak into the next
     * walk's high-water. Non-terminal jobs are cancelled first; the
     * release is best-effort by design (the actor queue may still drain
     * a queued job — its block stays valid until its step finishes). */
    for (i = 0; i < PM_METAL_BUILD_WALK_FANOUT; i++) {
        if (w->jobs[i] != NULL) {
            (void)pm_metal_build_actor_cancel(w->jobs[i]);
            (void)pm_metal_build_actor_release(w->jobs[i]);
            w->jobs[i] = NULL;
        }
    }
    if (w->units != NULL) {
        for (i = 0; i < w->n_units; i++) {
            uint32_t k;
            for (k = 0; k < w->units[i].n_sources; k++) {
                pm_util_mem_free(arena, (void *)w->units[i].sources[k]);
            }
            pm_util_mem_free(arena, (void *)w->units[i].sources);
            for (k = 0; k < w->units[i].n_include_dirs; k++) {
                pm_util_mem_free(arena, (void *)w->units[i].include_dirs[k]);
            }
            pm_util_mem_free(arena, (void *)w->units[i].include_dirs);
            for (k = 0; k < w->units[i].n_defines; k++) {
                pm_util_mem_free(arena, (void *)w->units[i].defines[k]);
            }
            pm_util_mem_free(arena, (void *)w->units[i].defines);
            for (k = 0; k < w->units[i].n_depends; k++) {
                pm_util_mem_free(arena, (void *)w->units[i].depends[k]);
            }
            pm_util_mem_free(arena, (void *)w->units[i].depends);
        }
        pm_util_mem_free(arena, w->units);
    }
    if (w->inc_copy != NULL) {
        uint32_t k;
        for (k = 0; k < w->opts.n_include_dirs; k++) {
            pm_util_mem_free(arena, (void *)w->inc_copy[k]);
        }
        pm_util_mem_free(arena, (void *)w->inc_copy);
    }
    if (w->def_copy != NULL) {
        uint32_t k;
        for (k = 0; k < w->opts.n_defines; k++) {
            pm_util_mem_free(arena, (void *)w->def_copy[k]);
        }
        pm_util_mem_free(arena, (void *)w->def_copy);
    }
    pm_util_mem_free(arena, (void *)w->order);
    pm_util_mem_free(arena, w->row);
    pm_util_mem_free(arena, w->retry);
    pm_util_mem_free(arena, w->unit_root);
    pm_util_mem_free(arena, w);
}

/* The walk's step: a supervisor quantum over the fan-out lanes.
 *
 * Every runnable unit gets its OWN ring task (actor_post): the coop
 * runners drive the jobs, one holds the TCC serial section at a time and
 * the rest park — the holder's finish re-posts them, so the section
 * hands off in one ring hop instead of waiting for the walk's next
 * poll. The walk itself settles terminal jobs (the rows + census), then
 * fills every free lane up to PM_METAL_BUILD_WALK_FANOUT, then yields
 * one quantum so the ring (httpd panes, net pumps, REPL) runs between
 * rounds. The task blocks are the coop card's refcounted property now
 * (the fx-6f reclaimer) — the old job-task UAF hazard is gone at the
 * card that owns it, and this card only releases the jobs it settled. */
/* Record the walk's first failure reason. Every path that marks a row FAILED
 * goes through here, so "1 failed" always carries a why — a lane's compile
 * error, an unresolved unit root, or a refusal from the actor. First only:
 * the later ones are usually dep fallout of this one. */
static void walk_note_fail(pm_build_walk_t *w, const char *fqn,
    const char *reason) {
    if (w->info.fail_err[0] != '\0') {
        return;
    }
    snprintf(w->info.fail_fqn, sizeof(w->info.fail_fqn), "%s",
        fqn != NULL ? fqn : "?");
    snprintf(w->info.fail_err, sizeof(w->info.fail_err), "%s",
        (reason != NULL && reason[0] != '\0') ? reason
                                              : "refused without a reason");
}

static pm_metal_coop_status_t walk_step(pm_metal_coop_coro_t *self) {
    pm_build_walk_t *w = (pm_build_walk_t *)self;
    uint32_t i;
    uint32_t live = 0;

    if (w == NULL) {
        return PM_METAL_COOP_ERROR;
    }

    /* Pass 1 — settle: every lane's terminal job closes its row here (the
     * runner that finished it left it terminal in the slot; the census
     * counts once, in the walk, never in the job's step). The state read
     * is acquire-atomic: a runner on another core writes the terminal
     * state under the actor lock, and the walk's census must not miss a
     * just-settled lane (or release a job a runner still holds mid-step). */
    for (i = 0; i < PM_METAL_BUILD_WALK_FANOUT; i++) {
        pm_metal_build_actor_job_t *job = w->jobs[i];
        pm_metal_build_actor_state_t st;
        int32_t ui;
        if (job == NULL) {
            continue;
        }
        st = (pm_metal_build_actor_state_t)__atomic_load_n(
            &job->state, __ATOMIC_ACQUIRE);
        if (st != PM_METAL_BUILD_ACTOR_DONE
            && st != PM_METAL_BUILD_ACTOR_FAILED
            && st != PM_METAL_BUILD_ACTOR_CANCELLED) {
            live++;
            continue;
        }
        ui = dag_find(w->units, w->n_units, job->unit.fqn);
        if (w->counted[i] != 0u) {
            /* census already done last quantum; only the release is left */
        } else if (st == PM_METAL_BUILD_ACTOR_DONE) {
            if (ui >= 0) {
                w->row[ui] = PM_BUILD_WALK_ROW_DONE;
            }
            w->info.n_done++;
            pm_metal_build_artifact_destroy(&job->artifact);
        } else if (job->rc == PM_METAL_BUILD_ERR_BUSY && ui >= 0
                   && w->retry != NULL
                   && w->retry[ui] < PM_METAL_BUILD_WALK_RETRIES) {
            /* Transient: nothing was attempted, so put the row back and let
             * a later quantum re-submit it. Not counted as done or failed —
             * the census stays honest about what actually built. */
            w->retry[ui]++;
            w->row[ui] = PM_BUILD_WALK_ROW_PENDING;
        } else {
            if (ui >= 0) {
                w->row[ui] = PM_BUILD_WALK_ROW_FAILED;
            }
            /* Before the release below takes the job's errbuf with it. */
            walk_note_fail(w, job->unit.fqn, job->err);
            w->info.n_failed++;
        }
        w->counted[i] = 1u;
        if (pm_metal_build_actor_release(job) == PM_METAL_BUILD_ERR_BUSY) {
            /* A runner is still inside this job's step. Keep the block and
             * the lane live so the walk cannot finish (or free) underneath
             * it; the next quantum retries the release. */
            live++;
            continue;
        }
        w->jobs[i] = NULL;
        w->counted[i] = 0u;
    }

    /* Pass 2 — SKIP-isolate: a FAILED/SKIPPED dep isolates its consumer
     * rows as SKIPPED (dep-failure isolation, never a cascade of compile
     * errors). Runs every quantum: a lane settling this round may
     * isolate rows that were PENDING last round. */
    for (i = 0; i < w->n_units; i++) {
        uint32_t d;
        int settled_skip = 0;
        int dep_open = 0;
        if (w->row[i] != PM_BUILD_WALK_ROW_PENDING) {
            continue;
        }
        for (d = 0; d < w->units[i].n_depends; d++) {
            int32_t di = dag_find(w->units, w->n_units,
                w->units[i].depends[d]);
            if (di < 0) {
                continue;  /* external dep: graph_resolve already ok'd it */
            }
            if (w->row[di] == PM_BUILD_WALK_ROW_FAILED
                || w->row[di] == PM_BUILD_WALK_ROW_SKIPPED) {
                settled_skip = 1;
                break;
            }
            if (w->row[di] != PM_BUILD_WALK_ROW_DONE) {
                dep_open = 1;  /* PENDING or RUNNING producer */
            }
        }
        if (settled_skip) {
            w->row[i] = PM_BUILD_WALK_ROW_SKIPPED;
            w->info.n_skipped++;
        }
        (void)dep_open;
    }

    /* Pass 3 — fill free lanes: the next PENDING rows whose deps are all
     * DONE become new jobs. One sweep takes as many as fit; the order
     * array is graph-resolved so independent rows sit adjacent in it. */
    for (i = 0; i < w->n_units && live < PM_METAL_BUILD_WALK_FANOUT; i++) {
        pm_metal_build_actor_job_t *job = NULL;
        const pm_metal_build_unit_t *u;
        uint32_t d;
        uint32_t slot;
        int32_t ui;
        int runnable;

        if (w->row[i] != PM_BUILD_WALK_ROW_PENDING) {
            continue;
        }
        runnable = 1;
        for (d = 0; d < w->units[i].n_depends; d++) {
            int32_t di = dag_find(w->units, w->n_units,
                w->units[i].depends[d]);
            if (di >= 0 && w->row[di] != PM_BUILD_WALK_ROW_DONE) {
                runnable = 0;
                break;
            }
        }
        if (!runnable) {
            continue;
        }
        /* free lane for this unit */
        for (slot = 0; slot < PM_METAL_BUILD_WALK_FANOUT; slot++) {
            if (w->jobs[slot] == NULL) {
                break;
            }
        }
        if (slot >= PM_METAL_BUILD_WALK_FANOUT) {
            break;  /* no free lane (live < FANOUT invariant guards this) */
        }
        u = &w->units[i];
        if (w->root_fn(u->fqn, w->unit_root, PM_METAL_BUILD_ROOT_MAX) != 0) {
            w->row[i] = PM_BUILD_WALK_ROW_FAILED;
            walk_note_fail(w, u->fqn, "walk: unit root not resolved");
            w->info.n_failed++;
            continue;
        }
        w->opts.unit_root = w->unit_root;
        w->opts.target = w->info.target;
        {
            char serr[PM_METAL_BUILD_ERR_MAX];
            serr[0] = '\0';
            ui = pm_metal_build_actor_submit(u, &w->opts, &job,
                serr, sizeof(serr));
            if (ui != PM_METAL_BUILD_OK) {
                w->row[i] = PM_BUILD_WALK_ROW_FAILED;
                walk_note_fail(w, u->fqn, serr);
                w->info.n_failed++;
                continue;
            }
        }
        if (pm_metal_build_actor_post(job) != 0) {
            /* ring full (or no runner ring): the walk drives this job
             * inline from its own quanta — same semantics as the pre-
             * fan-out walk, one actor_step per supervisor round. */
            w->inline_cnt++;
        }
        w->row[i] = PM_BUILD_WALK_ROW_RUNNING;
        w->jobs[slot] = job;
        live++;
    }

    /* Inline drive: unposted jobs (ring full / runner-less seat) step
     * here, one actor_step each — the walk's own quantum does the work
     * the runners would have. WAITING (section held by a posted job)
     * just parks for the next round. */
    if (w->inline_cnt > 0) {
        for (i = 0; i < PM_METAL_BUILD_WALK_FANOUT; i++) {
            pm_metal_build_actor_job_t *job = w->jobs[i];
            if (job == NULL || job->task != NULL) {
                continue;
            }
            (void)pm_metal_build_actor_step(job);
        }
    }

    /* Census published every quantum: the factory floor's running count
     * is live lanes, not a stale snapshot. */
    w->info.n_running = live;
    if (live > w->peak_running) {
        w->peak_running = live;
    }

    /* Terminal: every row settled and every lane drained. */
    if (live == 0) {
        for (i = 0; i < w->n_units; i++) {
            if (w->row[i] == PM_BUILD_WALK_ROW_PENDING
                || w->row[i] == PM_BUILD_WALK_ROW_RUNNING) {
                break;
            }
        }
        if (i >= w->n_units) {
            /* walk terminal: publish DONE on the coro first (the runner
             * overwrites status from the return value only AFTER this
             * returns — task_reclaim must see terminal now), then send
             * the task block home (w itself stays for walk_state
             * readers until the next start releases it) */
            w->info.state = PM_METAL_BUILD_WALK_DONE;
            w->info.n_running = 0;
            w->coro.status = (uint32_t)PM_METAL_COOP_DONE;
            {
                pm_metal_coop_task_t *wt = pm_metal_coop_current_task();
                if (wt != NULL) {
                    (void)pm_metal_coop_task_reclaim(wt);
                }
            }
            return PM_METAL_COOP_DONE;
        }
    }

    /* one quantum of ring rotation between supervisor rounds */
    return pm_metal_coop_yield_park(&w->coro);
}

static int32_t walk_start_locked(int32_t target,
    const pm_metal_build_compile_opts_t *opts,
    pm_metal_build_root_fn_t root_fn,
    char *errbuf, size_t errbuf_len) {
    pm_util_mem_arena_t *arena;
    pm_build_walk_t *w = NULL;
    pm_metal_build_unit_t *units = NULL;
    uint32_t n_units = 0;
    uint32_t next_id = 1;
    int32_t rc;

    if (errbuf != NULL && errbuf_len > 0) {
        errbuf[0] = '\0';
    }
    if (s_walk != NULL && s_walk->info.state == PM_METAL_BUILD_WALK_RUNNING) {
        err_set(errbuf, errbuf_len, "walk_start: a walk is running", 0);
        return PM_METAL_BUILD_ERR_BUSY;
    }
    if (root_fn == NULL || opts == NULL) {
        err_set(errbuf, errbuf_len, "walk_start: bad args", 0);
        return PM_METAL_BUILD_ERR_COMPILE;
    }
    arena = pm_metal_coop_arena();
    if (arena == NULL) {
        err_set(errbuf, errbuf_len, "walk_start: no boot arena", 0);
        return PM_METAL_BUILD_ERR_NOMEM;
    }
    /* a finished walk's spans are reclaimed before the new one allocates —
     * repeated BUILD ALL presses hold a stable high-water, not a growing one */
    if (s_walk != NULL) {
        next_id = s_walk->info.id + 1u;
        walk_release(s_walk);
        s_walk = NULL;
    }

    /* the units table is discovered into a scratch arena, then deep-copied
     * into the boot arena: the walk must outlive the starter's arena.
     *
     * 16 MiB, not 1: discover splices every rs card's `mod` parts to learn
     * which faces are spans of the unit's TU, and the compiler card's parts
     * are 36k lines. At 1 MiB that splice ran out mid-file, so the unit was
     * recorded with a short source list and the walk's compile of it died
     * on the truncated TU ("rsx: expected '}' before end of file") — while
     * the same unit rebuilt fine one at a time, because the /build/<fqn>
     * route discovers into its own 160 MiB span. The arena is transient:
     * freed a few lines below, before any job allocates. */
    {
        void *backing = malloc(PM_METAL_BUILD_DISCOVER_SPAN);
        pm_util_mem_arena_t *scratch;
        char disc_err[PM_METAL_BUILD_ERR_MAX];
        uint32_t i;
        if (backing == NULL) {
            err_set(errbuf, errbuf_len, "walk_start: no scratch", 0);
            return PM_METAL_BUILD_ERR_NOMEM;
        }
        scratch = pm_util_mem_arena_create(backing, PM_METAL_BUILD_DISCOVER_SPAN);
        if (scratch == NULL) {
            free(backing);
            err_set(errbuf, errbuf_len, "walk_start: no scratch arena", 0);
            return PM_METAL_BUILD_ERR_NOMEM;
        }
        rc = pm_metal_build_discover(scratch, &units, &n_units,
            disc_err, sizeof(disc_err));
        if (rc != PM_METAL_BUILD_OK) {
            pm_util_mem_arena_destroy(scratch);
            free(backing);
            snprintf(errbuf, errbuf_len, "walk_start: discover: %.80s",
                disc_err[0] != '\0' ? disc_err : "refused");
            return rc;
        }
        if (n_units == 0) {
            pm_util_mem_arena_destroy(scratch);
            free(backing);
            err_set(errbuf, errbuf_len, "walk_start: no units", 0);
            return PM_METAL_BUILD_ERR_PARSE;
        }
        w = (pm_build_walk_t *)pm_util_mem_alloc(arena, sizeof(*w));
        if (w == NULL) {
            pm_util_mem_arena_destroy(scratch);
            free(backing);
            err_set(errbuf, errbuf_len, "walk_start: arena exhausted (walk)", 0);
            return PM_METAL_BUILD_ERR_NOMEM;
        }
        memset(w, 0, sizeof(*w));
        w->units = (pm_metal_build_unit_t *)pm_util_mem_alloc(
            arena, n_units * sizeof(pm_metal_build_unit_t));
        w->row = (uint32_t *)pm_util_mem_alloc(
            arena, n_units * sizeof(uint32_t));
        w->retry = (uint32_t *)pm_util_mem_alloc(
            arena, n_units * sizeof(uint32_t));
        if (w->units == NULL || w->row == NULL || w->retry == NULL) {
            walk_release(w);
            pm_util_mem_arena_destroy(scratch);
            free(backing);
            err_set(errbuf, errbuf_len, "walk_start: arena exhausted (rows)", 0);
            return PM_METAL_BUILD_ERR_NOMEM;
        }
        memset(w->row, 0, n_units * sizeof(uint32_t));
        memset(w->retry, 0, n_units * sizeof(uint32_t));
        /* count first: walk_release walks w->n_units, and the graph step
         * below only fills w->n_units on success — a copy or graph refusal
         * must still free the half-built units array (boot arena has no
         * arena-destroy to reclaim it) */
        w->n_units = n_units;
        for (i = 0; i < n_units; i++) {
            rc = actor_unit_copy(arena, &units[i], &w->units[i]);
            if (rc != PM_METAL_BUILD_OK) {
                walk_release(w);
                pm_util_mem_arena_destroy(scratch);
                free(backing);
                err_set(errbuf, errbuf_len, "walk_start: arena exhausted (units)", 0);
                return PM_METAL_BUILD_ERR_NOMEM;
            }
        }
        /* graph-resolve into boot arena: the order array is the walk's
         * sweep sequence (cycle/missing-dep refusals stop the walk before
         * any compile runs) */
        rc = pm_metal_build_graph_resolve(arena, w->units, n_units,
            &w->order, &w->n_units, disc_err, sizeof(disc_err));
        if (rc != PM_METAL_BUILD_OK) {
            walk_release(w);
            pm_util_mem_arena_destroy(scratch);
            free(backing);
            snprintf(errbuf, errbuf_len, "walk_start: graph: %.80s",
                disc_err[0] != '\0' ? disc_err : "refused");
            return rc;
        }
        /* the seat fill is deep-copied: strings (includes/defines) must
         * outlive the starter's stack */
        {
            uint32_t k;
            const char **inc = NULL;
            const char **def = NULL;
            if (opts->n_include_dirs > 0) {
                inc = (const char **)pm_util_mem_alloc(
                    arena, opts->n_include_dirs * sizeof(const char *));
                if (inc == NULL) {
                    rc = PM_METAL_BUILD_ERR_NOMEM;
                    goto fill_fail;
                }
                for (k = 0; k < opts->n_include_dirs; k++) {
                    inc[k] = dup_str(arena, opts->include_dirs[k],
                        strlen(opts->include_dirs[k]));
                    if (inc[k] == NULL) {
                        rc = PM_METAL_BUILD_ERR_NOMEM;
                        goto fill_fail;
                    }
                }
            }
            if (opts->n_defines > 0) {
                def = (const char **)pm_util_mem_alloc(
                    arena, opts->n_defines * sizeof(const char *));
                if (def == NULL) {
                    rc = PM_METAL_BUILD_ERR_NOMEM;
                    goto fill_fail;
                }
                for (k = 0; k < opts->n_defines; k++) {
                    def[k] = dup_str(arena, opts->defines[k],
                        strlen(opts->defines[k]));
                    if (def[k] == NULL) {
                        rc = PM_METAL_BUILD_ERR_NOMEM;
                        goto fill_fail;
                    }
                }
            }
            w->opts = *opts;
            w->opts.include_dirs = inc;
            w->opts.defines = def;
            w->inc_copy = inc;
            w->def_copy = def;
            goto fill_done;
fill_fail:
            walk_release(w);
            pm_util_mem_arena_destroy(scratch);
            free(backing);
            err_set(errbuf, errbuf_len, "walk_start: arena exhausted (fill)", 0);
            return rc;
fill_done:
            ;
        }
        w->unit_root = (char *)pm_util_mem_alloc(arena, PM_METAL_BUILD_ROOT_MAX);
        if (w->unit_root == NULL) {
            walk_release(w);
            pm_util_mem_arena_destroy(scratch);
            free(backing);
            err_set(errbuf, errbuf_len, "walk_start: arena exhausted (root)", 0);
            return PM_METAL_BUILD_ERR_NOMEM;
        }
        w->root_fn = root_fn;
        w->coro.step = walk_step;
        w->coro.status = PM_METAL_COOP_PENDING;
        w->info.state = PM_METAL_BUILD_WALK_RUNNING;
        w->info.target = target;
        w->info.mode = opts != NULL ? opts->mode : PM_METAL_BUILD_LOCAL;
        w->info.n_total = n_units;
        w->info.id = next_id;
        pm_util_mem_arena_destroy(scratch);
        free(backing);
    }
    /* post: the ring owns the drive from here; s_walk publishes the state */
    if (pm_metal_coop_create_task(&w->coro) == NULL) {
        walk_release(w);
        err_set(errbuf, errbuf_len, "walk_start: runner ring full", 0);
        return PM_METAL_BUILD_ERR_BUSY;
    }
    s_walk = w;
    return (int32_t)w->info.id;
}

/* One start at a time.
 *
 * The "a walk is running" check above and everything after it — discover,
 * the deep copy into the boot arena, releasing the finished walk, posting
 * the new task, publishing s_walk — is a long non-atomic stretch, and two
 * seats' worth of HTTP handler threads can be inside it at once. Two POSTs
 * landing together both passed the check, both called walk_release() on the
 * SAME finished walk, both posted a task, and the loser's s_walk write was
 * overwritten by the winner's. What the callers saw was two different walks
 * (one local, one produce) reporting the same id; what the seat could be
 * left with was a published walk whose frame the other start had already
 * released — state "running" with nothing in flight and no progress, while
 * every runner sat idle. The page polls and a curl POST is enough to hit it.
 *
 * The gate is the claim the state read could not be: the loser gets the
 * same BUSY refusal it would have got a moment later. */
static uint32_t s_walk_gate;

int32_t pm_metal_build_walk_start(int32_t target,
    const pm_metal_build_compile_opts_t *opts,
    pm_metal_build_root_fn_t root_fn,
    char *errbuf, size_t errbuf_len) {
    uint32_t expect = 0u;
    int32_t rc;

    if (!__atomic_compare_exchange_n(&s_walk_gate, &expect, 1u, 0,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        err_set(errbuf, errbuf_len, "walk_start: a walk is starting", 0);
        return PM_METAL_BUILD_ERR_BUSY;
    }
    rc = walk_start_locked(target, opts, root_fn, errbuf, errbuf_len);
    __atomic_store_n(&s_walk_gate, 0u, __ATOMIC_RELEASE);
    return rc;
}

void pm_metal_build_walk_state(pm_metal_build_walk_info_t *out) {
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (s_walk != NULL) {
        *out = s_walk->info;
    }
}

/* Most lanes the current (or last) walk ever held at once. 1 says it ran
 * serially, 0 that no walk has started. */
uint32_t pm_metal_build_walk_peak(void) {
    return (s_walk != NULL) ? s_walk->peak_running : 0u;
}

/* Lifecycle: the ctx allocates lazily from the boot arena and is released
 * by unregistering here — never freed directly (its memory belongs to the
 * boot arena, which its owner destroys after this unwind). Clearing the
 * pointer at deinit means a post-teardown caller sees a fresh refusal, not
 * a dangling arena pointer. Init is a no-op: allocation stays lazy so a
 * seat that never builds keeps its boot-arena bytes. */
static int32_t pm_metal_build_boot_init(pm_util_mem_arena_t *arena) {
    (void)arena;
    return 0;
}

static void pm_metal_build_boot_deinit(void) {
    s_build_ctx = NULL;
}

PM_MOD_BOOT_C(pymergetic.metal.build, pm_metal_build_boot_init,
    pm_metal_build_boot_deinit);
