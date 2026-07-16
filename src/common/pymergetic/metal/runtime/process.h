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
 * file adds no new synchronization *inside* the runtime itself beyond
 * what run_ex()'s own pm_metal_runtime_exec_t out-param already gives
 * kill() (see below); otherwise it only tracks the worker threads its
 * own spawn() starts.
 *
 * getpid(): there is deliberately no such call here — spawn() instead
 * injects a "PID=<n>" entry into the process's own env (on top of
 * whatever the caller passed), so any guest gets its own pid back from
 * its language's ordinary getenv("PID"), exactly like every other WASI
 * env var, no new host import needed for something libc already has a
 * standard door for.
 *
 * Concurrency: init()/shutdown() are controller-thread-only, same
 * contract as runtime.h's own init()/shutdown() (in fact shutdown() here
 * must run *before* runtime_shutdown() — see shutdown()'s own doc
 * comment). Once init() has returned, spawn()/try_wait()/wait()/kill()/
 * list() are all safe to call concurrently from multiple threads. wait()/
 * try_wait() on the *same* pid must not race each other — same
 * single-reaper precondition port/worker.h's own join()/try_join()
 * already documents, just inherited here rather than re-invented.
 */
#ifndef PYMERGETIC_METAL_RUNTIME_PROCESS_H_
#define PYMERGETIC_METAL_RUNTIME_PROCESS_H_

#include <stdint.h>

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
 * updated) — a caller that wants to react to completion the instant it
 * happens (e.g. logging "exit=%d") without a poll loop hooks this. Runs
 * on that process's own worker thread, not the caller of spawn()'s
 * thread — do not block for long in here, and never call back into
 * process.h from inside it (try_wait()/wait()/spawn() on *any* pid) —
 * this file's lock is not recursive and is still held by the caller
 * further up the same call stack at this point. Pass on_exit=NULL for
 * "no callback" (e.g. a caller that's going to wait()/try_wait() this
 * same pid itself instead). */
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
 * caller has its own handle-table teardown, before that too) — a
 * still-running process holds its handle's refcount above zero, so
 * runtime_shutdown()'s own unload loop (see runtime.c) must never run
 * concurrently with one. */
int pm_metal_process_init(void);
void pm_metal_process_shutdown(void);

/* impl: common — src/common/pymergetic/metal/runtime/process.c
 *
 * spawn(): starts run_ex(handle, argc, argv, envc, envp, stdin_fd,
 * stdout_fd, stderr_fd) on a new worker thread and hands back that pid
 * in *out_pid. argv/envp are copied internally before this returns —
 * the caller's own arrays (e.g. a tokenized command line living on some
 * other thread's stack) need not outlive this call, unlike run_ex()
 * itself. A "PID=<n>" entry (this process's own *out_pid, decimal) is
 * appended to the copied env automatically — on top of whatever envc/
 * envp the caller passed, never replacing it — see this file's own
 * getpid() note above. Opportunistically reaps any already-finished,
 * not-yet-waited process first (bounded, best-effort zero-effort hygiene
 * — see process.c — so a caller that never calls try_wait()/wait()/ps
 * still cannot leak the table indefinitely as long as spawn() keeps
 * getting called). Returns 0/-1 (bad args, or the table is still
 * genuinely full even after that sweep).
 *
 * Ownership of stdin_fd/stdout_fd/stderr_fd passes to this call: any
 * value >= 0 (never -1, the "inherit the host's own" sentinel — that
 * one is never touched) is closed automatically, by this process's own
 * worker thread, the moment its run_ex() call returns — the caller must
 * not close it itself, and must not reuse that fd number for anything
 * else until this pid has actually finished. This is what makes
 * port/pipe.h's pipe() safe to chain two spawn()s with no dup()ing: hand
 * the write end to one as stdout_fd, the read end to the other as
 * stdin_fd, then forget both — each is closed by its own sole owner
 * right when it's done, which is also exactly when the *other* end
 * needs to see EOF. See docs/RUNTIME.md "Processes" for the worked
 * example. */
int pm_metal_process_spawn(pm_metal_runtime_handle_t handle, int argc, char **argv, int envc, const char **envp,
			    int64_t stdin_fd, int64_t stdout_fd, int64_t stderr_fd,
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
 * kill(): a hard stop only, not a real POSIX signal — see
 * pm_metal_runtime_terminate()'s own doc comment (runtime.h) for exactly
 * what that means and why it is safe to call concurrently with the
 * target's own worker thread. Does not wait for the target to actually
 * exit — call wait()/try_wait() afterward for that, same as always.
 * Returns 0 (found and asked to stop — including the harmless case
 * where it had already finished on its own, or hadn't reached
 * run_ex()'s own instantiate() yet, in which case this is a silent
 * no-op), -1 (no such pid). */
int pm_metal_process_kill(pm_metal_process_id_t pid);

/* impl: common — src/common/pymergetic/metal/runtime/process.c
 *
 * 1 if this pid's run_ex() has published its exec token (instantiate
 * done, guest may be in the interp loop) and has not finished yet —
 * the window where kill() actually interrupts a hot loop. 0 otherwise.
 * Used by Zephyr process smoke to avoid kill()-before-ready (a no-op)
 * without sleeping (native_sim cannot advance time under a busy guest). */
int pm_metal_process_exec_live(pm_metal_process_id_t pid);

/* impl: common — src/common/pymergetic/metal/runtime/process.c
 *
 * Visits every currently-tracked process (running, or finished but not
 * yet reaped by try_wait()/wait()) in table order — read-only, never
 * reaps anything itself (see spawn()'s own opportunistic sweep for
 * that), e.g. for a `ps`-style caller. */
void pm_metal_process_list(void (*visit)(const pm_metal_process_info_t *info, void *ctx), void *ctx);

#endif /* PYMERGETIC_METAL_RUNTIME_PROCESS_H_ */
