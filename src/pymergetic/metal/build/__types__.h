/* pymergetic.metal.build — parse __pmm__.toml manifests, resolve the
 * dependency graph, and (Phase 3) compile + link card sources in-process.
 *
 * The parser speaks exactly the Phase-1 external-manifest schema:
 *   key = "string" | 123 | ["a", "b"]   plus # comments and [table]
 *   headers (ignored — the manifests are flat).
 */
#ifndef PYMERGETIC_METAL_BUILD_TYPES_H
#define PYMERGETIC_METAL_BUILD_TYPES_H

#include "pymergetic/metal/coop/__types__.h"
#include "pymergetic/util/mem/__types__.h"
#include "pymergetic/wasmmod/registry/__types__.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_BUILD_ERR_MAX 160u
#define PM_METAL_BUILD_STR_MAX 128u
/* root path a root resolver may write (deep trees stay honest refusals) */
#define PM_METAL_BUILD_ROOT_MAX 2048u
#define PM_METAL_BUILD_MAX_OBJS 8u
/* One actor job's compile scratch span: a fresh malloc'd arena per unit
 * (ksweep's posture, ksweep's number — TCC's tokstr DOUBLES on growth,
 * and the big cards reach a 32 MiB block, so 160 MiB is the span that
 * survives the whole tree). The shared boot arena cannot reliably serve
 * that next to the seat's retained state. */
#define PM_METAL_BUILD_JOB_SPAN (160u * 1024u * 1024u)

typedef struct pm_metal_build_unit {
    char fqn[PM_METAL_BUILD_STR_MAX];
    char impl[8];   /* "c" | "cpp" | "rs" */
    char version[PM_METAL_BUILD_STR_MAX];
    const char **sources;    /* arena-owned */
    uint32_t n_sources;
    const char **include_dirs;
    uint32_t n_include_dirs;
    const char **defines;
    uint32_t n_defines;
    const char **depends;    /* fqns this unit must be built after */
    uint32_t n_depends;
} pm_metal_build_unit_t;

typedef struct pm_metal_build_artifact {
    char fqn[PM_METAL_BUILD_STR_MAX];
    uint8_t *bytes;          /* arena-owned */
    size_t len;
    int32_t is_wasm;
    /* impl = py: bytes are mpy bytecode (loadable via jit.py's
     * object_load), not a linked native image — destroy leaves them to
     * the arena, lookup/call are not applicable. */
    int32_t is_mpy;
    /* wasm-seat link: the loader handle of each loaded module (the registry
     * owns the published exports; destroy unloads the modules). Unused
     * (zeroed) on ELF seats. */
    pm_wasmmod_registry_handle_t loader_handles[PM_METAL_BUILD_MAX_OBJS];
    uint32_t n_loader_handles;
} pm_metal_build_artifact_t;

typedef enum pm_metal_build_status {
    PM_METAL_BUILD_OK = 0,
    PM_METAL_BUILD_ERR_PARSE = -1,
    PM_METAL_BUILD_ERR_CYCLE = -2,
    PM_METAL_BUILD_ERR_MISSING_DEP = -3,
    PM_METAL_BUILD_ERR_NOMEM = -4,
    PM_METAL_BUILD_ERR_COMPILE = -5,
    PM_METAL_BUILD_ERR_LINK = -6,
    /* actor-only statuses: bounded-queue backpressure (retry is the
     * caller's policy) and cancellation at a phase boundary */
    PM_METAL_BUILD_ERR_BUSY = -7,
    PM_METAL_BUILD_ERR_CANCELLED = -8,
} pm_metal_build_status_t;

/* Seat fill for every compile-shaped face: the unit's root directory and
 * the seat's include roots / extra defines (what differs per seat, like
 * io.fetch's fills — the module and the C face do not differ). Packed in
 * one struct so each export's signature fits the registry's fixed
 * SIG_CAP (160 bytes) — an over-cap sig is silently refused at
 * registration, which would hide the face on every seat. */
typedef struct pm_metal_build_compile_opts {
    const char *unit_root;
    const char **include_dirs;
    uint32_t n_include_dirs;
    const char **defines;
    uint32_t n_defines;
    /* cross-emit target (the every-seat-builds-every-arch matrix): 0 = the
     * seat's own backend, 1 = wasm32, 2 = arm-eabi, 3 = x86_64 — same
     * values as pm_metal_jit_c_target_t. 0 on seats that never pass it. */
    int32_t target;
} pm_metal_build_compile_opts_t;

/* Parse one manifest into unit (arena-backed; strings are copied into the
 * arena). Returns PM_METAL_BUILD_OK or a negative status; errbuf carries the
 * parse error with a line number when non-NULL. */
int32_t pm_metal_build_unit_parse(pm_util_mem_arena_t *arena,
    const uint8_t *bytes, size_t len, pm_metal_build_unit_t *unit,
    char *errbuf, size_t errbuf_len);

/* Topologically order units on their depends edges (dependencies first).
 * order receives an arena-owned array of pointers into units. A cycle, a
 * depends-edge naming an unknown fqn, or a duplicate fqn is an error. */
int32_t pm_metal_build_graph_resolve(pm_util_mem_arena_t *arena,
    pm_metal_build_unit_t *units, uint32_t n_units,
    const pm_metal_build_unit_t ***order, uint32_t *n_order,
    char *errbuf, size_t errbuf_len);

/* Compile one source of unit into an object (arena-owned bytes in obj_out).
 * unit_root is the directory the unit's relative include_dirs resolve
 * against (the manifest's own directory); source is C source text.
 * Phase 3 drives the jit.c card's TCC object path. */
int32_t pm_metal_build_compile_source(pm_util_mem_arena_t *arena,
    const pm_metal_build_unit_t *unit, const char *unit_root, const char *source,
    uint8_t **obj_out, size_t *obj_len, char *errbuf, size_t errbuf_len);

/* compile_source with the cross-compile knob: target selects which TCC
 * backend makes the object. JIT_C_TARGET_SEAT is exactly compile_source;
 * JIT_C_TARGET_WASM32 / JIT_C_TARGET_ARM_EABI cross-compile on ELF seats
 * that link the second (prefixed) libtcc instance for that backend. The
 * object format follows the target — an ELF ET_REL for the seat, a
 * serialized wasm module for wasm32 — and the link face accepts either
 * shape (ELF relocator on ELF seats, loader publish on wasm-capable
 * seats). */
int32_t pm_metal_build_compile_source_target(pm_util_mem_arena_t *arena,
    const pm_metal_build_unit_t *unit, const char *unit_root, const char *source,
    int32_t target,
    uint8_t **obj_out, size_t *obj_len, char *errbuf, size_t errbuf_len);

/* Link the unit's compiled objects through the in-tree ELF relocator and
 * return the loaded, executable image. artifact->bytes carries the image
 * pointer — release it with pm_metal_build_artifact_destroy. */
int32_t pm_metal_build_link(pm_util_mem_arena_t *arena,
    const pm_metal_build_unit_t *unit, uint8_t **objects, const size_t *lens,
    uint32_t n_objects, pm_metal_build_artifact_t *artifact,
    char *errbuf, size_t errbuf_len);

/* Release a link artifact (munmap the image). Safe on NULL / empty. */
void pm_metal_build_artifact_destroy(pm_metal_build_artifact_t *artifact);

/* Look up a function symbol in a linked artifact. Returns NULL when the
 * name is not present. */
void *pm_metal_build_artifact_lookup(const pm_metal_build_artifact_t *artifact,
    const char *name);

/* Call a function in a linked artifact with scalar (i64-transport) args.
 * Returns 0 on a completed call, negative on refusal. The result lands in
 * *res when res is non-NULL (0-2 args on ELF seats; i32 spine on the wasm
 * seat, widened to i64 on return). */
int32_t pm_metal_build_artifact_call(const pm_metal_build_artifact_t *artifact,
    const char *name, const int64_t *args, uint32_t n_args, int64_t *res);

/* Runtime card discovery: walk the embedded card source table (every seat
 * ships it — tools/embed_src.py), parse each card's raw __pmm__.toml with
 * pm_metal_build_unit_parse, and synthesize one unit per card. Units receive
 * an arena-owned array in *units; n_units is its length.
 *
 * impl="c" cards become buildable units: sources are the card's embedded
 * muscle file names, include_dirs/defines empty — the caller supplies the
 * seat's include roots + defines at compile time (the seat fill, like
 * io.fetch differs per seat). impl="rs"/"py" cards are listed too, with
 * impl copied verbatim: pm_metal_build_unit_compile refuses them with a
 * clear "not yet buildable" error rather than silently skipping. */
int32_t pm_metal_build_discover(pm_util_mem_arena_t *arena,
    pm_metal_build_unit_t **units, uint32_t *n_units,
    char *errbuf, size_t errbuf_len);

/* Compile every source of unit (via pm_metal_build_compile_source, rooted at
 * opts->unit_root + opts->include_dirs / opts->defines — the seat fill) and
 * link the objects through the in-tree ELF relocator with the process
 * resolver. One call: sources -> artifact. */
int32_t pm_metal_build_unit_compile(pm_util_mem_arena_t *arena,
    const pm_metal_build_unit_t *unit,
    const pm_metal_build_compile_opts_t *opts,
    pm_metal_build_artifact_t *artifact,
    char *errbuf, size_t errbuf_len);

/*------------------ async compiler actor (bounded queue) ------------------
 * TCC's reallocator is ONE global (tcc_set_realloc), so every TCC invocation
 * in the process must be serialized. The actor is that serialization point:
 * jobs queue in a bounded FIFO, the actor drains them one at a time, and
 * waiting jobs PARK (stackless coro frames on the boot arena) instead of
 * blocking a runner. Backpressure is a refusal: submit fails with
 * PM_METAL_BUILD_ERR_BUSY when the queue is at capacity — callers decide
 * their own retry policy, the queue never grows unbounded. */

#define PM_METAL_BUILD_ACTOR_DEPTH 16u

typedef enum pm_metal_build_actor_state {
    PM_METAL_BUILD_ACTOR_NEW = 0,        /* queued, never stepped */
    PM_METAL_BUILD_ACTOR_RUNNING = 1,    /* owns the serial section */
    PM_METAL_BUILD_ACTOR_WAITING = 2,    /* parked for the serial section */
    PM_METAL_BUILD_ACTOR_DONE = 3,       /* artifact filled, job complete */
    PM_METAL_BUILD_ACTOR_FAILED = 4,     /* errbuf carries the refusal */
    PM_METAL_BUILD_ACTOR_CANCELLED = 5,  /* cancelled at a phase boundary */
} pm_metal_build_actor_state_t;

/* One queued compile job. The submitter keeps the pointer and polls it (or
 * parks a coro of its own on it via pm_metal_coop_await-style chaining —
 * the actor never blocks the calling runner). All fields are written by the
 * actor under the queue lock; state transitions are atomic releases. */
typedef struct pm_metal_build_actor_job {
    pm_metal_coop_coro_t coro;    /* step = actor_job_step; parks on WAITING */
    pm_metal_build_actor_state_t state;
    int32_t rc;                    /* PM_METAL_BUILD_* once DONE/FAILED */
    pm_metal_build_unit_t unit;    /* arena-copied manifest */
    const char *unit_root;         /* arena-copied */
    const char **include_dirs;     /* arena-copied (count below) */
    uint32_t n_include_dirs;
    const char **defines;          /* arena-copied (count below) */
    uint32_t n_defines;
    int32_t target;                /* cross-emit knob from opts (0 = native) */
    pm_metal_build_artifact_t artifact;
    char err[PM_METAL_BUILD_ERR_MAX];
    uint32_t cancel;               /* 1 = cancel at the next phase boundary */
    uint32_t next_src;             /* phase cursor: next source index */
    /* The compile's own scratch span: a fresh malloc'd arena per job
     * (ksweep's proven posture — a big unit's TCC tokstr wants a 16 MiB
     * contiguous block, which a shared, long-lived arena cannot reliably
     * supply next to the rest of the seat). The artifact does NOT live
     * here (its image is self-owned), so destroying this span at release
     * never dangles. NULL when malloc refused: the boot arena is the
     * fallback (firmware's shim routes back to it anyway). */
    void *scratch_backing;
    pm_util_mem_arena_t *scratch;
} pm_metal_build_actor_job_t;

/* Submit a compile job to the actor's queue. Returns PM_METAL_BUILD_OK and
 * the job pointer (state NEW) on accept, PM_METAL_BUILD_ERR_BUSY when the
 * bounded queue is full (backpressure — retry is the caller's policy),
 * negative otherwise. opts is deep-copied into the boot arena with the
 * unit; the caller's arena may die afterwards. */
int32_t pm_metal_build_actor_submit(
    const pm_metal_build_unit_t *unit,
    const pm_metal_build_compile_opts_t *opts,
    pm_metal_build_actor_job_t **job_out, char *errbuf, size_t errbuf_len);

/* Step a submitted job once: NEW/WAITING -> try to take the serial section
 * (runs the whole unit compile inside the call when it gets it — TCC is
 * blocking, documented), or parks (returns WAITING, no runner blocked).
 * DONE/FAILED/CANCELLED are sticky. This is the face a runner loop or a
 * waiting parent coro drives; pm_metal_build_actor_run does it for you. */
pm_metal_coop_status_t pm_metal_build_actor_step(pm_metal_build_actor_job_t *job);

/* Drive the actor until the job reaches a terminal state (blocking call —
 * it pumps the async ring while the serial section is held by another job).
 * tcc_compile_string inside the actor is a blocking call: the actor owns
 * the serial section, this face just waits for it. */
int32_t pm_metal_build_actor_run(pm_metal_build_actor_job_t *job);

/* Request cancellation: the job stops at its next phase boundary (between
 * unit sources, never inside a TCC invocation). Returns 0 when the flag is
 * set; a job already terminal is left untouched. */
int32_t pm_metal_build_actor_cancel(pm_metal_build_actor_job_t *job);

/* Actor queue occupancy for inspectors: *depth is the current fill,
 * PM_METAL_BUILD_ACTOR_DEPTH the cap. Returns 0. */
int32_t pm_metal_build_actor_depth(uint32_t *depth);

/* Reclaim a terminal job's memory. The submit deep-copies the unit and
 * the seat fill into the boot arena so the job outlives the caller's
 * arena; the boot arena's heap is tlsf-backed, so this free genuinely
 * reclaims — a submit/release loop holds a stable high-water instead of
 * growing the boot arena per job. Only a terminal (DONE/FAILED/CANCELLED)
 * job may be released, and exactly once: after this call the job pointer
 * is dangling (the queue no longer holds it — terminal jobs are dequeued
 * inside the step that finished them). Returns 0, or negative when the
 * job is not terminal (release the job after run/step reports its
 * terminal state, never while queued). */
int32_t pm_metal_build_actor_release(pm_metal_build_actor_job_t *job);

/*------------------ dependency DAG executor (Phase 5) ------------------
 * graph_resolve orders units; the DAG executor RUNS that order with
 * dependency-failure isolation: a unit whose dependency failed is SKIPPED
 * (isolated, reported — never silently dropped and never a cascade of
 * misleading compile errors). Scheduling goes through the bounded actor,
 * so TCC stays serialized on every seat (see the parallelism note in the
 * phase report — that is the honest posture, not a limitation to lift).
 *
 * The executor is synchronous: it drives each unit to completion through
 * the actor (actor_run semantics). A bounded-queue backpressure refusal
 * is fatal to the run (the queue drains before the next submit, so it
 * cannot legitimately trigger; seeing one is a bug). */

#define PM_METAL_BUILD_DAG_MAX PM_METAL_BUILD_ACTOR_DEPTH

typedef enum pm_metal_build_dag_state {
    PM_METAL_BUILD_DAG_PENDING = 0,   /* not reached yet */
    PM_METAL_BUILD_DAG_RUNNING = 1,   /* in the actor */
    PM_METAL_BUILD_DAG_DONE = 2,      /* compiled + linked */
    PM_METAL_BUILD_DAG_FAILED = 3,    /* its own compile/link refused */
    PM_METAL_BUILD_DAG_SKIPPED = 4,   /* a dependency FAILED/SKIPPED */
} pm_metal_build_dag_state_t;

/* Per-unit outcome row (arena-owned; the run fills n_rows of them). */
typedef struct pm_metal_build_dag_row {
    char fqn[PM_METAL_BUILD_STR_MAX];
    pm_metal_build_dag_state_t state;
    int32_t rc;                       /* PM_METAL_BUILD_* for FAILED rows */
    char err[PM_METAL_BUILD_ERR_MAX]; /* refusal detail for FAILED rows */
    size_t image_len;                 /* artifact bytes for DONE rows */
} pm_metal_build_dag_row_t;

typedef struct pm_metal_build_dag_result {
    pm_metal_build_dag_row_t *rows;  /* arena array, n_rows long */
    uint32_t n_rows;
    uint32_t n_done;
    uint32_t n_failed;
    uint32_t n_skipped;
} pm_metal_build_dag_result_t;

/* DAG run seat fill: the compile opts every unit's job is submitted with,
 * plus the root resolver. root_fn(fqn, buf, cap) resolves a unit's root
 * directory (the manifest's own dir) — returning nonzero refuses that unit
 * as FAILED (isolation: the run continues). */
typedef int32_t (*pm_metal_build_root_fn_t)(const char *fqn, char *buf, size_t cap);

typedef struct pm_metal_build_dag_opts {
    pm_metal_build_compile_opts_t compile;   /* the seat fill */
    pm_metal_build_root_fn_t root_fn;        /* unit -> its source dir */
} pm_metal_build_dag_opts_t;

/* Run every unit in topological order through the actor. units/n_units
 * come from pm_metal_build_discover (or a caller-built set); opts carries
 * the seat fill and the root resolver.
 *
 * On return every row carries its terminal state. Returns 0 when the whole
 * run reached a terminal state (individual failures are in the rows), a
 * negative status only for setup refusals (graph_resolve failure, actor
 * submit backpressure, bad args). */
int32_t pm_metal_build_dag_run(pm_util_mem_arena_t *arena,
    pm_metal_build_unit_t *units, uint32_t n_units,
    const pm_metal_build_dag_opts_t *opts,
    pm_metal_build_dag_result_t *out,
    char *errbuf, size_t errbuf_len);

/*------------------ build records (provenance chain) ------------------
 * Every unit_compile retains a record: the unit's sources, the per-source
 * object bytes, the linked image's exported symbols. The inspector serves
 * these as /build/<fqn> — authored source stays the primary pane, the
 * record is the build-product pane with the provenance chain in between.
 *
 * 64 slots: a whole-tree BUILD ALL walk is a first-class operation now
 * (~80 units), and the factory floor's per-unit pane serves the record —
 * an eviction-wrapped table of 8 would leave the last 56 rows' provenance
 * dark right after the walk that built them. ~5 KB/record, ~320 KB in the
 * boot arena. */
#define PM_METAL_BUILD_MAX_RECORDS 64u
#define PM_METAL_BUILD_MAX_SRC_PATH 96u
#define PM_METAL_BUILD_MAX_SYMS 64u
#define PM_METAL_BUILD_SYM_NAME_MAX 64u

/*------------------ build event ring (factory floor telemetry) ----------
 * Every observable transition in a unit_compile appends one fixed-size
 * event to a ring on the build ctx: unit start/end, per-source compile,
 * link, record. The factory page polls /build/events?since=<seq> and
 * replays the ring's tail — the ring is the ONLY live build state (no
 * per-lane coroutines), so the UI is a pure read face. Fixed-size, no
 * pointers: the ring is copied by value under the ctx, never arena-owned,
 * so a caller's arena dying mid-build cannot strand half an event. */
#define PM_METAL_BUILD_EVENTS 64u
#define PM_METAL_BUILD_EVENT_FQN 64u
#define PM_METAL_BUILD_EVENT_SRC 40u

typedef enum pm_metal_build_event_kind {
    PM_METAL_BUILD_EVENT_UNIT_START = 0,
    PM_METAL_BUILD_EVENT_COMPILE_START = 1,
    PM_METAL_BUILD_EVENT_COMPILE_END = 2,
    PM_METAL_BUILD_EVENT_LINK_END = 3,
    PM_METAL_BUILD_EVENT_UNIT_END = 4,
    PM_METAL_BUILD_EVENT_UNIT_FAIL = 5,
} pm_metal_build_event_kind_t;

typedef struct pm_metal_build_event {
    uint32_t seq;                         /* monotonic, starts at 1 */
    uint16_t kind;                        /* pm_metal_build_event_kind_t */
    uint16_t target;                      /* 0 seat / 1 wasm32 / 2 arm / 3 x64 */
    uint32_t t_us;                        /* boot mono clock at append */
    uint32_t dur_us;                      /* stage duration (end events) */
    uint32_t bytes;                       /* emitted object/link size */
    char fqn[PM_METAL_BUILD_EVENT_FQN];
    char src[PM_METAL_BUILD_EVENT_SRC];   /* file tail for compile events */
} pm_metal_build_event_t;

/* Copy events with seq > since into out (up to max), return the count.
 * Also returns the ring's newest seq in *latest either way — the page's
 * next poll passes it as since. Ring wraps: the tail is the truth. */
uint32_t pm_metal_build_events_since(uint32_t since,
    pm_metal_build_event_t *out, uint32_t max, uint32_t *latest);

/* The newest seq currently in the ring (0 when nothing was ever built). */
uint32_t pm_metal_build_events_latest(void);

/*------------------ background walk (async BUILD ALL) --------------------
 * The factory floor's BUILD ALL must not block the pane thread: the walk
 * is one coop task (the runner's ring drives it), one unit per step —
 * the actor keeps TCC serialized inside each step, and the step yields
 * between units so the rest of the ring (httpd panes, net pumps) runs
 * between every two compiles. The walk owns no arena: the submit-time
 * deep copies (actor_submit) put each job in the boot arena, and the
 * units table is copied there too — a walk outlives its starter.
 *
 * The walk is the DAG semantics on a task: dependency-ordered (the same
 * graph_resolve), SKIP isolation on failed deps, per-unit telemetry on
 * the event ring (the UI's live pane is the ring, same as the sync run). */
typedef enum pm_metal_build_walk_state {
    PM_METAL_BUILD_WALK_IDLE = 0,     /* no walk ever ran (or finished+read) */
    PM_METAL_BUILD_WALK_RUNNING = 1,  /* the task is on the runner ring */
    PM_METAL_BUILD_WALK_DONE = 2,     /* every row terminal; results held */
} pm_metal_build_walk_status_t;

typedef struct pm_metal_build_walk_info {
    pm_metal_build_walk_status_t state;
    int32_t target;                    /* lane the walk was started on */
    uint32_t id;                       /* walk id, 1..N (0 = never) */
    uint32_t n_total;                  /* units discovered for this walk */
    uint32_t n_done;                   /* rows DONE */
    uint32_t n_failed;                 /* rows FAILED */
    uint32_t n_skipped;                /* rows SKIPPED (dep isolation) */
} pm_metal_build_walk_info_t;

/* Start a background walk of every discovered unit on lane `target`
 * (0 seat-native / 1 wasm32 / 2 arm-eabi / 3 x86_64 — same values as
 * pm_metal_jit_c_target_t). Returns the walk id (> 0), or negative:
 *   -PM_METAL_BUILD_ERR_BUSY    a walk is already running (poll state)
 *   -PM_METAL_BUILD_ERR_NOMEM   discover/copy refused (no walk started)
 *   -PM_METAL_BUILD_ERR_PARSE   no units to build
 * The starter's arena/deps (includes, defines) are deep-copied into the
 * boot arena at start; the caller's arena may die right after. */
int32_t pm_metal_build_walk_start(int32_t target,
    const pm_metal_build_compile_opts_t *opts,
    pm_metal_build_root_fn_t root_fn,
    char *errbuf, size_t errbuf_len);

/* The current walk's state (IDLE state zero-init when no walk ran). */
void pm_metal_build_walk_state(pm_metal_build_walk_info_t *out);


typedef struct pm_metal_build_record {
    char fqn[PM_METAL_BUILD_STR_MAX];
    int32_t valid;                       /* slot in use */
    /* per-source objects: path + the .o length it compiled to */
    char src_paths[PM_METAL_BUILD_MAX_OBJS][PM_METAL_BUILD_MAX_SRC_PATH];
    uint32_t obj_lens[PM_METAL_BUILD_MAX_OBJS];
    uint32_t n_sources;
    /* the linked image's exported function symbols (name only — addresses
     * are seat-local and not stable across runs) */
    const char *sym_names[PM_METAL_BUILD_MAX_SYMS];
    char sym_names_buf[PM_METAL_BUILD_MAX_SYMS][PM_METAL_BUILD_SYM_NAME_MAX];
    uint32_t n_syms;
} pm_metal_build_record_t;

/* The record of the most recent unit_compile of fqn (NULL when never built
 * or after record_reset). The record table is retained until reset — build
 * products stay inspectable as long as the images are live. */
const pm_metal_build_record_t *pm_metal_build_record_find(const char *fqn);

/* Drop every retained record (called by the record owner at teardown; the
 * linked images themselves are freed via artifact_destroy). */
void pm_metal_build_record_reset(void);

/*------------------ change ledger (fs-backed, JSON-lines) ------------------
 * Every source mutation carries a note: what kind of change, why, and the
 * things it touches. The ledger is one card-owned file in the fs card
 * (/src/.changes.jsonl) — one JSON object per line, appended by the build
 * card (it owns write-back) and queried by anyone. Phase 11's AST editor
 * refuses a write-back without a matching note for the target. */

#define PM_METAL_BUILD_NOTE_TARGET_MAX 160u
#define PM_METAL_BUILD_NOTE_REASON_MAX 256u
#define PM_METAL_BUILD_NOTE_REFS_MAX 8u
#define PM_METAL_BUILD_LEDGER_MAX (64u * 1024u)

typedef enum pm_metal_build_note_kind {
    PM_METAL_BUILD_NOTE_CHANGE = 0,
    PM_METAL_BUILD_NOTE_DECISION = 1,
    PM_METAL_BUILD_NOTE_WARNING = 2,
    PM_METAL_BUILD_NOTE_TODO = 3,
} pm_metal_build_note_kind_t;

/* Append one note as a JSON line to /src/.changes.jsonl. kind is validated;
 * an empty reason is refused (a note without a reason is noise). refs are
 * optional targets the note touches (card fqns, file paths). Returns 0 on
 * append, a negative status on refusal. */
int32_t pm_metal_build_note_add(const char *target,
    pm_metal_build_note_kind_t kind, const char *reason,
    const char *const *refs, uint32_t n_refs);

/* Query notes: lines whose target matches (exact) or all when target is
 * NULL, filtered by kind when kind >= 0. Matching lines are concatenated
 * (newline-joined) into out, which receives the byte count. Returns the
 * number of matching lines, or a negative status on error. */
int32_t pm_metal_build_notes_query(const char *target,
    int32_t kind, char *out, size_t out_len, uint32_t *out_n);

/* True when target has at least one note of kind (the write-back gate:
 * Phase 11's editor refuses a mutation without this). */
int32_t pm_metal_build_note_has(const char *target,
    pm_metal_build_note_kind_t kind);

/* The ledger path in the fs card — the one file the build card owns. */
const char *pm_metal_build_ledger_path(void);

/*------------------ accessor spine (Phase 11) ------------------
 * One language-neutral query shape over everything the earlier phases
 * built: the live registry (Phase 4/8: what actually runs + provenance),
 * the doc extractor (Phase 9), the change ledger (Phase 10), and the
 * embedded source table. `b.at(fqn, name)` resolves; at_info carries the
 * joined answer; at_ast dispatches to the per-language editor (Phase 12).
 *
 * at() prefers the live registry — what actually executes — and layers the
 * build record on top for provenance: the same query before and after a
 * runtime rebuild returns the same identity pointed at the new record. */

typedef uint32_t pm_metal_build_at_handle_t;
#define PM_METAL_BUILD_AT_NONE 0u

#define PM_METAL_BUILD_AT_FQN_MAX 128u
#define PM_METAL_BUILD_AT_NAME_MAX 128u
#define PM_METAL_BUILD_AT_SIG_MAX 192u
#define PM_METAL_BUILD_AT_DOC_MAX 512u
#define PM_METAL_BUILD_AT_NOTES_MAX 512u
#define PM_METAL_BUILD_AT_REFS_MAX 12u
#define PM_METAL_BUILD_AT_REF_MAX 128u
#define PM_METAL_BUILD_AT_FILE_MAX 96u

typedef struct pm_metal_build_at_info {
    /* identity */
    char fqn[PM_METAL_BUILD_AT_FQN_MAX];
    char name[PM_METAL_BUILD_AT_NAME_MAX];
    char kind[8];       /* "fn" | "mem" | "obj" | "i64" | "f32" | "f64" | "mod" */
    char lang[8];       /* card impl: "c" | "rs" | "py" */
    /* registry face */
    char sig[PM_METAL_BUILD_AT_SIG_MAX];    /* empty when the face is not an fn */
    /* provenance (build record; present when fqn was unit_compiled) */
    int32_t has_record;
    uint32_t n_sources;
    uint32_t n_syms;
    /* Phase 9: doc prose first-line + the file/line the extractor found */
    char doc[PM_METAL_BUILD_AT_DOC_MAX];    /* empty when undocumented */
    char file[PM_METAL_BUILD_AT_FILE_MAX];  /* embedded-source relative path */
    uint32_t line;
    /* Phase 10: the ledger notes for this target (raw JSONL lines) */
    char notes[PM_METAL_BUILD_AT_NOTES_MAX];
    uint32_t n_notes;
    /* call-graph: fqns this face's card imports from (connect_import edges) */
    char deps[PM_METAL_BUILD_AT_REFS_MAX][PM_METAL_BUILD_AT_REF_MAX];
    uint32_t n_deps;
} pm_metal_build_at_info_t;

/* Resolve fqn (+ optional export name; NULL = the card itself, kind "mod")
 * against the live registry, then layer the build record + doc + notes +
 * deps. Returns a handle for at_info / at_ast, PM_METAL_BUILD_AT_NONE when
 * fqn is unknown to both the registry and the embedded source table. */
pm_metal_build_at_handle_t pm_metal_build_at(const char *fqn, const char *name);

/* Fill info from a handle returned by at(). Returns 0 on success, -1 when
 * the handle is stale (record_reset between at() and at_info()). */
int32_t pm_metal_build_at_info(pm_metal_build_at_handle_t handle,
    pm_metal_build_at_info_t *info);

/* The per-language editor leaf. Tonight's spine returns the language and a
 * flag saying whether an editor exists for it (Phase 12 fills the C leaf;
 * Rust/C++ later). lang_out receives "c" | "rs" | "cpp" | "py". Returns 1
 * when an editor exists, 0 when the language has none yet, -1 on a bad
 * handle. */
int32_t pm_metal_build_at_ast(pm_metal_build_at_handle_t handle,
    char *lang_out, size_t lang_max);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BUILD_TYPES_H */
