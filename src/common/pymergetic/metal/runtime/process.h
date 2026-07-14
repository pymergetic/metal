/*
 * Runtime — processes. A "process" is one execution of a loaded handle:
 * spawn() starts it on its own background worker (see port/worker.h) and
 * hands back a pid, independent of — and decoupled from — the handle it
 * runs. Unlike a handle (one loaded module, "1..PM_METAL_RUNTIME_MAX_HANDLES",
 * see runtime.h), the *same* handle may have several processes running
 * against it at once, each with its own pid, argv, and (see run_ex()'s
 * envp) env — same relationship a real OS has between one binary on disk
 * and its N currently-running instances. Host-only convenience layer
 * built entirely on top of runtime.h's own public API (run_ex() + the
 * refcount it already maintains — see runtime.c's file header) — this
 * file adds no new synchronization *inside* the runtime itself, it only
 * tracks the worker threads its own spawn() starts.
 *
 * Concurrency: init()/shutdown() are controller-thread-only, same
 * contract as runtime.h's own init()/shutdown() (in fact shutdown() here
 * must run *before* runtime_shutdown() — see shutdown()'s own doc
 * comment). Once init() has returned, spawn()/try_wait()/wait()/list()
 * are all safe to call concurrently from multiple threads. wait()/
 * try_wait() on the *same* pid must not race each other — same
 * single-reaper precondition port/worker.h's own join()/try_join()
 * already documents, just inherited here rather than re-invented.
 */
#ifndef PYMERGETIC_METAL_RUNTIME_PROCESS_H_
#define PYMERGETIC_METAL_RUNTIME_PROCESS_H_

#include <stdint.h>
#include <stdio.h>

#include "pymergetic/metal/runtime/runtime.h"

typedef struct pm_metal_process_id {
	uint32_t pid; /* 0 == no process; 1..PM_METAL_PROCESS_MAX otherwise */
} pm_metal_process_id_t;

/* Independent of PM_METAL_RUNTIME_MAX_HANDLES on purpose — see file
 * header: several processes may share one handle, so this table is sized
 * against concurrent *executions*, not loaded modules. */
#define PM_METAL_PROCESS_MAX 32
#define PM_METAL_PROCESS_CMD_MAX 64 /* display-only copy of argv[0] for list() — truncated, never used for dispatch */

typedef struct pm_metal_process_info {
	pm_metal_process_id_t pid;
	pm_metal_runtime_handle_t handle;
	char cmd[PM_METAL_PROCESS_CMD_MAX];
	int running; /* 1 == still executing; 0 == finished, exit_code valid */
	int exit_code;
} pm_metal_process_info_t;

/* Invoked exactly once, from the worker thread itself, immediately after
 * a process finishes (right after its slot's running/exit_code are
 * updated) — e.g. shell/commands/run.c's `run` uses this to log "exit=%d"
 * onto the handle's own console pane the instant it happens, without
 * needing a poll loop. Runs on that process's own worker thread, not the
 * caller of spawn()'s thread — do not block for long in here, and never
 * call back into process.h from inside it (try_wait()/wait()/spawn() on
 * *any* pid) — this file's lock is not recursive and is still held by
 * the caller further up the same call stack at this point. Pass
 * on_exit=NULL for "no callback" (e.g. a caller that's going to
 * wait()/try_wait() this same pid itself instead — see shell/shell.c's
 * synchronous wasm-override dispatch). */
typedef void (*pm_metal_process_exit_cb)(pm_metal_process_id_t pid, pm_metal_runtime_handle_t handle, int exit_code,
					  void *cb_ctx);

/* impl: common — src/common/pymergetic/metal/runtime/process.c
 *
 * init(): call once, any time before the first spawn() — independent of
 * runtime_init() (this table's lifetime doesn't have to nest inside the
 * runtime's own, though in practice every caller today init()s/
 * shutdown()s both together). shutdown(): joins every still-running
 * process (blocking — same as calling wait() on each), then clears the
 * table. Call this *before* pm_metal_runtime_shutdown() (and, if the
 * caller has its own handle-table teardown like shell/handles.c's
 * handles_shutdown(), before that too) — a still-running process holds
 * its handle's refcount above zero, so runtime_shutdown()'s own unload
 * loop (see runtime.c) must never run concurrently with one. */
int pm_metal_process_init(void);
void pm_metal_process_shutdown(void);

/* impl: common — src/common/pymergetic/metal/runtime/process.c
 *
 * spawn(): starts run_ex(handle, argc, argv, envc, envp, stdin_fd,
 * stdout_fd, stderr_fd, <this process's own pid>) on a new worker thread
 * and hands back that pid in *out_pid. argv/envp are copied internally
 * before this returns — the caller's own arrays (e.g. a tokenized
 * command line living on some other thread's stack) need not outlive
 * this call, unlike run_ex() itself. Opportunistically reaps any
 * already-finished, not-yet-waited process first (bounded, best-effort
 * zero-effort hygiene — see process.c — so a caller that never calls
 * try_wait()/wait()/ps still cannot leak the table indefinitely as long
 * as spawn() keeps getting called). Returns 0/-1 (bad args, or the table
 * is still genuinely full even after that sweep).
 *
 * `guest_out`: the FILE* shell/guest_exec.c's native-import bridge
 * should write a guest-triggered command's output onto, for *this*
 * process specifically (see pm_metal_process_guest_out() below) — not
 * involved in the guest's own WASI stdio at all (that's `stdout_fd`
 * above); this is purely for output guest_exec.c's bridge produces on
 * this process's behalf. Pass whatever FILE* this process's own stdout
 * already effectively goes to: an open console sink's `->out` for a
 * caller with one (shell/commands/run.c's `run`, shell.c's wasm-override
 * dispatch), or plain `stdout` for one that doesn't (scripted mode, see
 * app/app.c) — same "real stdio" fallback run_ex()'s own `-1` sentinel
 * already uses for stdout_fd, just as an already-open FILE* instead of a
 * raw fd, so a caller with a real sink is never juggling two competing
 * FILE*s over the one pipe fd. NULL is safe (guest_exec.c's bridge then
 * returns NOT_FOUND for this process, exactly as if the name didn't
 * exist — never a crash) but should not be needed by any real caller. */
int pm_metal_process_spawn(pm_metal_runtime_handle_t handle, int argc, char **argv, int envc, const char **envp,
			    int64_t stdin_fd, int64_t stdout_fd, int64_t stderr_fd, FILE *guest_out,
			    pm_metal_process_exit_cb on_exit, void *on_exit_ctx, pm_metal_process_id_t *out_pid);

/* impl: common — src/common/pymergetic/metal/runtime/process.c
 *
 * try_wait(): non-blocking. Returns 1 (already finished, *out_exit_code
 * filled, pid now reaped/invalid — spawn() may reuse its slot, and thus
 * this exact pid number, for a future process, same reuse rule
 * runtime.h's own handle ids already follow), 0 (still running, pid
 * still valid, call again later), -1 (no such pid — never issued, or
 * already reaped by an earlier wait()/try_wait() call). */
int pm_metal_process_try_wait(pm_metal_process_id_t pid, int *out_exit_code);

/* impl: common — src/common/pymergetic/metal/runtime/process.c
 *
 * wait(): blocks until it finishes, then reaps it (same pid-reuse note as
 * try_wait() above). Returns 0/-1 (no such pid). */
int pm_metal_process_wait(pm_metal_process_id_t pid, int *out_exit_code);

/* impl: common — src/common/pymergetic/metal/runtime/process.c
 *
 * Visits every currently-tracked process (running, or finished but not
 * yet reaped by try_wait()/wait()) in table order — read-only, never
 * reaps anything itself (see spawn()'s own opportunistic sweep for that)
 * so calling this (e.g. shell/commands/ps.c's `ps`) has no side effects. */
void pm_metal_process_list(void (*visit)(const pm_metal_process_info_t *info, void *ctx), void *ctx);

/* impl: common — src/common/pymergetic/metal/runtime/process.c
 *
 * Returns the `guest_out` FILE* spawn() was given for `pid` (see above),
 * or NULL if `pid` names no currently-tracked process. The one caller is
 * shell/guest_exec.c's native-import bridge, resolving the pid it was
 * tagged with (runtime.h's run_ex() `custom_tag`, set by this file's own
 * spawn() right before starting the worker below) back to somewhere to
 * write that guest's requested command's output. Safe to call from
 * inside that exact process's own execution (i.e. from the worker thread
 * this pid's own run_ex() call is running on, synchronously, from a
 * native import the guest itself just called) without racing this pid
 * ever being reaped: reaping only ever happens after the worker function
 * this pid belongs to has returned, which cannot happen while that same
 * function is still on the stack waiting for this exact call to return. */
FILE *pm_metal_process_guest_out(pm_metal_process_id_t pid);

#endif /* PYMERGETIC_METAL_RUNTIME_PROCESS_H_ */
