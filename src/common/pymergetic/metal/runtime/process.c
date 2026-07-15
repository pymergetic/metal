/*
 * Runtime — processes. See process.h.
 */
#include "pymergetic/metal/runtime/process.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pymergetic/metal/port/lock.h"
#include "pymergetic/metal/port/pipe.h"
#include "pymergetic/metal/port/worker.h"

typedef struct pm_metal_process_slot {
	int used;
	pm_metal_process_id_t pid;
	pm_metal_runtime_handle_t handle;
	int argc;
	char **argv; /* owned, strdup'd copies — see spawn() */
	int envc;
	char **envp; /* owned, strdup'd copies plus one "PID=<n>" entry — see spawn() */
	int64_t stdin_fd, stdout_fd, stderr_fd;
	pm_metal_process_exit_cb on_exit;
	void *on_exit_ctx;
	pm_metal_port_worker_t worker;
	volatile int finished; /* set by the worker itself, right before it returns */
	int exit_code;         /* valid once finished == 1 */
	/* Owned by run_ex() itself once the worker below calls it — see
	 * pm_metal_runtime_exec_t's own doc comment (runtime.h) and
	 * kill()'s doc comment here for the locking rule that makes
	 * reading this from a different thread than the worker safe. */
	pm_metal_runtime_exec_t exec;
} pm_metal_process_slot_t;

static struct {
	int initialized;
	pm_metal_process_slot_t slots[PM_METAL_PROCESS_MAX];
} g_pm_metal_process;

/* Guards every field above except the const-after-spawn() argv/envp
 * pointers themselves — those are only ever read (never mutated) by
 * anyone after spawn() publishes them, including the worker thread, so
 * reading them unlocked from the worker is safe; only their *presence*
 * (slot->used) and the finished/exit_code pair need the lock. */
static pm_metal_port_mutex_t g_pm_metal_process_lock;

static void pm_metal_process_free_owned(pm_metal_process_slot_t *slot)
{
	int i;

	for (i = 0; i < slot->argc; i++) {
		free(slot->argv[i]);
	}
	free(slot->argv);
	for (i = 0; i < slot->envc; i++) {
		free(slot->envp[i]);
	}
	free(slot->envp);
	slot->argv = NULL;
	slot->envp = NULL;
}

static int pm_metal_process_worker(void *arg)
{
	pm_metal_process_slot_t *slot = arg;
	int exit_code = pm_metal_runtime_run_ex(slot->handle, slot->argc, slot->argv, slot->envc,
						 (const char **)slot->envp, slot->stdin_fd, slot->stdout_fd,
						 slot->stderr_fd, &slot->exec);

	/* Ownership transfer per spawn()'s own doc comment (process.h) —
	 * pm_metal_port_close() itself is a no-op on the -1 "inherit the
	 * host's own" sentinel, so this is safe unconditionally regardless
	 * of which slots were actually real fds. Done here, not by
	 * whoever called spawn(): this is the exact moment each fd's sole
	 * owner is done with it, which for a pipe()d stdout_fd is also
	 * exactly when the process on the *other* end needs to see EOF —
	 * see port/pipe.h's own file header. */
	pm_metal_port_close(slot->stdin_fd);
	pm_metal_port_close(slot->stdout_fd);
	pm_metal_port_close(slot->stderr_fd);

	/* Drops the hold spawn() took synchronously before this thread
	 * even started — see spawn()'s own comment on why that hold
	 * exists. run_ex() above has its own refcount bump/drop around the
	 * actual execution; this is the outer one, covering the full
	 * spawn()-to-here span regardless of how long this thread took to
	 * get scheduled. */
	pm_metal_runtime_release(slot->handle);

	pm_metal_port_mutex_lock(&g_pm_metal_process_lock);
	slot->exit_code = exit_code;
	slot->finished = 1;
	pm_metal_port_mutex_unlock(&g_pm_metal_process_lock);

	/* Outside the lock — see process.h's own doc comment on why an
	 * on_exit callback must never call back into this file: it runs
	 * here, on this same worker thread, with the lock already
	 * released, so a callback that *did* call e.g. try_wait() on some
	 * other pid would not deadlock against this lock specifically —
	 * but reentering during teardown races shutdown()'s own join loop
	 * regardless, so the rule stays "don't", not "usually fine". */
	if (slot->on_exit) {
		slot->on_exit(slot->pid, slot->handle, exit_code, slot->on_exit_ctx);
	}
	return exit_code;
}

/* Caller must hold g_pm_metal_process_lock. Non-blocking best-effort
 * sweep: reaps every slot that has already finished and is actually
 * try_join()-able right now, silently skipping (leaving for a later
 * sweep) the rare slot whose worker set finished=1 but hasn't quite
 * returned from pthread's own exit path yet — see port/worker.h's own
 * try_join() doc comment. */
static void pm_metal_process_reap_finished_locked(void)
{
	int i;

	for (i = 0; i < PM_METAL_PROCESS_MAX; i++) {
		pm_metal_process_slot_t *slot = &g_pm_metal_process.slots[i];

		if (slot->used && slot->finished && pm_metal_port_worker_try_join(&slot->worker) == 0) {
			pm_metal_process_free_owned(slot);
			slot->used = 0;
		}
	}
}

int pm_metal_process_init(void)
{
	if (g_pm_metal_process.initialized) {
		fprintf(stderr, "pm_metal_process: already initialized\n");
		return -1;
	}
	memset(&g_pm_metal_process, 0, sizeof(g_pm_metal_process));
	pm_metal_port_mutex_init(&g_pm_metal_process_lock);
	g_pm_metal_process.initialized = 1;
	return 0;
}

void pm_metal_process_shutdown(void)
{
	if (!g_pm_metal_process.initialized) {
		return;
	}

	int i;

	/* Controller-thread-only, per the file header — blocks out any
	 * process still executing, same as calling wait() on each in turn,
	 * so the caller's own handle-table/runtime teardown that follows
	 * never races a handle whose refcount a still-running process here
	 * is holding above zero (see runtime.c's file header). */
	for (i = 0; i < PM_METAL_PROCESS_MAX; i++) {
		pm_metal_process_slot_t *slot = &g_pm_metal_process.slots[i];

		if (!slot->used) {
			continue;
		}
		pm_metal_port_worker_join(&slot->worker);
		pm_metal_process_free_owned(slot);
		slot->used = 0;
	}

	pm_metal_port_mutex_destroy(&g_pm_metal_process_lock);
	memset(&g_pm_metal_process, 0, sizeof(g_pm_metal_process));
}

int pm_metal_process_spawn(pm_metal_runtime_handle_t handle, int argc, char **argv, int envc, const char **envp,
			    int64_t stdin_fd, int64_t stdout_fd, int64_t stderr_fd,
			    pm_metal_process_exit_cb on_exit, void *on_exit_ctx, pm_metal_process_id_t *out_pid)
{
	if (!g_pm_metal_process.initialized || !out_pid || argc <= 0 || !argv || envc < 0) {
		return -1;
	}

	/* Taken synchronously, before this function does anything else —
	 * closes the exact race this file exists to avoid: without it,
	 * unload() could see handle's refcount still at zero (run_ex()'s
	 * own bump only happens once the worker thread below actually gets
	 * scheduled, which is not guaranteed to be before this call
	 * returns) and free the module out from under a process that has
	 * been spawn()ed but not yet actually started running. Released in
	 * pm_metal_process_worker() once run_ex() returns, or right here on
	 * any failure path below that means no worker thread will ever run
	 * to release it itself. See runtime.h's own doc comment on
	 * hold()/release(). */
	if (pm_metal_runtime_hold(handle) != 0) {
		return -1; /* bad handle — runtime.c already logged */
	}

	pm_metal_port_mutex_lock(&g_pm_metal_process_lock);
	pm_metal_process_reap_finished_locked();

	int slot_idx = -1;
	int i;

	for (i = 0; i < PM_METAL_PROCESS_MAX; i++) {
		if (!g_pm_metal_process.slots[i].used) {
			slot_idx = i;
			break;
		}
	}
	if (slot_idx < 0) {
		pm_metal_port_mutex_unlock(&g_pm_metal_process_lock);
		pm_metal_runtime_release(handle);
		fprintf(stderr, "pm_metal_process: process table full\n");
		return -1;
	}

	pm_metal_process_slot_t *slot = &g_pm_metal_process.slots[slot_idx];

	memset(slot, 0, sizeof(*slot));
	slot->pid.pid = (uint32_t)(slot_idx + 1);

	/* One extra slot for the "PID=<n>" entry spawn() always appends —
	 * see process.h's own getpid() note. */
	char pid_env[16]; /* "PID=" + up to 10 digits + NUL */
	int total_envc = envc + 1;

	snprintf(pid_env, sizeof(pid_env), "PID=%u", (unsigned)slot->pid.pid);

	/* Owned copies: argv/envp typically point at a caller's stack
	 * buffer (e.g. a tokenized command line) that will not outlive
	 * this call — the worker thread below runs concurrently with the
	 * caller and needs its own, independently-lifetimed strings. */
	slot->argv = malloc(sizeof(char *) * (size_t)argc);
	slot->envp = malloc(sizeof(char *) * (size_t)total_envc);
	if (!slot->argv || !slot->envp) {
		free(slot->argv);
		free(slot->envp);
		pm_metal_port_mutex_unlock(&g_pm_metal_process_lock);
		pm_metal_runtime_release(handle);
		fprintf(stderr, "pm_metal_process: out of memory\n");
		return -1;
	}
	for (i = 0; i < argc; i++) {
		size_t len = strlen(argv[i]) + 1;

		slot->argv[i] = malloc(len);
		if (slot->argv[i]) {
			memcpy(slot->argv[i], argv[i], len);
		}
	}
	for (i = 0; i < envc; i++) {
		size_t len = strlen(envp[i]) + 1;

		slot->envp[i] = malloc(len);
		if (slot->envp[i]) {
			memcpy(slot->envp[i], envp[i], len);
		}
	}
	slot->envp[envc] = malloc(strlen(pid_env) + 1);
	if (slot->envp[envc]) {
		memcpy(slot->envp[envc], pid_env, strlen(pid_env) + 1);
	}

	slot->handle = handle;
	slot->argc = argc;
	slot->envc = total_envc;
	slot->stdin_fd = stdin_fd;
	slot->stdout_fd = stdout_fd;
	slot->stderr_fd = stderr_fd;
	slot->on_exit = on_exit;
	slot->on_exit_ctx = on_exit_ctx;

	if (pm_metal_port_worker_spawn(&slot->worker, pm_metal_process_worker, slot) != 0) {
		pm_metal_process_free_owned(slot);
		pm_metal_port_mutex_unlock(&g_pm_metal_process_lock);
		pm_metal_runtime_release(handle);
		fprintf(stderr, "pm_metal_process: failed to start worker thread\n");
		return -1;
	}
	slot->used = 1;
	*out_pid = slot->pid;
	pm_metal_port_mutex_unlock(&g_pm_metal_process_lock);
	return 0;
}

int pm_metal_process_try_wait(pm_metal_process_id_t pid, int *out_exit_code)
{
	if (!g_pm_metal_process.initialized || pid.pid == 0 || pid.pid > PM_METAL_PROCESS_MAX) {
		return -1;
	}

	pm_metal_port_mutex_lock(&g_pm_metal_process_lock);

	pm_metal_process_slot_t *slot = &g_pm_metal_process.slots[pid.pid - 1];
	int rc;

	if (!slot->used || slot->pid.pid != pid.pid) {
		rc = -1;
	} else if (!slot->finished) {
		rc = 0;
	} else if (pm_metal_port_worker_try_join(&slot->worker) != 0) {
		rc = 0; /* finished, but the OS thread isn't join()-able quite yet — rare, retry later */
	} else {
		if (out_exit_code) {
			*out_exit_code = slot->exit_code;
		}
		pm_metal_process_free_owned(slot);
		slot->used = 0;
		rc = 1;
	}

	pm_metal_port_mutex_unlock(&g_pm_metal_process_lock);
	return rc;
}

int pm_metal_process_wait(pm_metal_process_id_t pid, int *out_exit_code)
{
	if (!g_pm_metal_process.initialized || pid.pid == 0 || pid.pid > PM_METAL_PROCESS_MAX) {
		return -1;
	}

	pm_metal_port_mutex_lock(&g_pm_metal_process_lock);
	pm_metal_process_slot_t *slot = &g_pm_metal_process.slots[pid.pid - 1];

	if (!slot->used || slot->pid.pid != pid.pid) {
		pm_metal_port_mutex_unlock(&g_pm_metal_process_lock);
		return -1;
	}
	pm_metal_port_mutex_unlock(&g_pm_metal_process_lock);

	/* Blocks outside the lock — a long-running guest must not stall
	 * every other process table operation (spawn()/list()/try_wait()
	 * on other pids) for as long as this one call blocks. Safe: slot
	 * itself cannot be freed/reused out from under us here — the only
	 * thing that could reap *this* pid is another wait()/try_wait() on
	 * it, which is the same "don't race yourself" precondition
	 * port/worker.h's own join() already documents. */
	pm_metal_port_worker_join(&slot->worker);

	pm_metal_port_mutex_lock(&g_pm_metal_process_lock);
	if (out_exit_code) {
		*out_exit_code = slot->exit_code;
	}
	pm_metal_process_free_owned(slot);
	slot->used = 0;
	pm_metal_port_mutex_unlock(&g_pm_metal_process_lock);
	return 0;
}

int pm_metal_process_kill(pm_metal_process_id_t pid)
{
	if (!g_pm_metal_process.initialized || pid.pid == 0 || pid.pid > PM_METAL_PROCESS_MAX) {
		return -1;
	}

	pm_metal_port_mutex_lock(&g_pm_metal_process_lock);

	pm_metal_process_slot_t *slot = &g_pm_metal_process.slots[pid.pid - 1];
	int rc;

	if (!slot->used || slot->pid.pid != pid.pid) {
		rc = -1;
	} else {
		/* Held for the whole call below, not just this lookup —
		 * this is what keeps a concurrent wait()/try_wait() from
		 * reaping (and a future spawn() from reusing) this exact
		 * slot out from under us between the check above and
		 * pm_metal_runtime_terminate() actually touching
		 * slot->exec. Lock nesting order is always this lock
		 * first, then g_pm_metal_runtime_lock (runtime.c's own,
		 * taken inside terminate() itself) — never the reverse,
		 * so this can never deadlock against anything runtime.c
		 * itself does. */
		pm_metal_runtime_terminate(&slot->exec);
		rc = 0;
	}

	pm_metal_port_mutex_unlock(&g_pm_metal_process_lock);
	return rc;
}

void pm_metal_process_list(void (*visit)(const pm_metal_process_info_t *info, void *ctx), void *ctx)
{
	if (!g_pm_metal_process.initialized || !visit) {
		return;
	}

	pm_metal_port_mutex_lock(&g_pm_metal_process_lock);

	int i;

	for (i = 0; i < PM_METAL_PROCESS_MAX; i++) {
		pm_metal_process_slot_t *slot = &g_pm_metal_process.slots[i];

		if (!slot->used) {
			continue;
		}

		pm_metal_process_info_t info;

		info.pid = slot->pid;
		info.handle = slot->handle;
		info.running = !slot->finished;
		info.exit_code = slot->exit_code;
		snprintf(info.cmd, sizeof(info.cmd), "%s", slot->argc > 0 ? slot->argv[0] : "");
		visit(&info, ctx);
	}

	pm_metal_port_mutex_unlock(&g_pm_metal_process_lock);
}
