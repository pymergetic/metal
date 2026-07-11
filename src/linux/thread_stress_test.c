/*
 * Concurrency stress test for pm_metal_runtime — proves (under
 * ThreadSanitizer) the threading contract documented in runtime.c and
 * docs/RUNTIME.md "Concurrency", rather than just asserting it in a
 * comment. Not part of the normal build — see
 * scripts/verify-linux-threads.sh, which builds this target with TSan
 * and fails the build if TSan reports anything.
 *
 * Exercises three things concurrently:
 *  - independent_worker: many threads each doing their own load/run/
 *    unload cycles on two different mods — the common case, should run
 *    fully in parallel.
 *  - shared_handle_runner: many threads calling run() on the SAME
 *    already-loaded handle at once — the case that needs the
 *    set_wasi_args()+instantiate() pairing to stay atomic (see runtime.c).
 *  - unload_attempt: tries to unload that same shared handle while the
 *    runners above are still in flight — must be safely rejected
 *    (refcount > 0), never crash or corrupt state.
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "pymergetic/metal/runtime/runtime.h"

#define NUM_WORKERS 6 /* + 1 shared handle == 7, under PM_METAL_RUNTIME_MAX_HANDLES (8) */
#define ITERATIONS 150

static pm_metal_runtime_handle_t g_shared_handle;

static void *independent_worker(void *arg)
{
	intptr_t id = (intptr_t)arg;
	const char *path = (id % 2 == 0) ? "/mods/t0_hello.wasm" : "/mods/t1_read.wasm";
	int i;

	for (i = 0; i < ITERATIONS; i++) {
		pm_metal_runtime_handle_t h;

		if (pm_metal_runtime_load_file(path, &h) != 0) {
			fprintf(stderr, "independent_worker %ld: load failed at iter %d\n",
				(long)id, i);
			continue;
		}

		char argv0[32];
		snprintf(argv0, sizeof(argv0), "w%ld", (long)id);
		char *mod_argv[1];
		mod_argv[0] = argv0;

		pm_metal_runtime_run(h, 1, mod_argv);

		if (pm_metal_runtime_unload(h) != 0) {
			fprintf(stderr, "independent_worker %ld: unload failed at iter %d\n",
				(long)id, i);
		}
	}

	return NULL;
}

static void *shared_handle_runner(void *arg)
{
	intptr_t id = (intptr_t)arg;
	int i;

	for (i = 0; i < ITERATIONS; i++) {
		char argv0[32];
		snprintf(argv0, sizeof(argv0), "shared%ld", (long)id);
		char *mod_argv[1];
		mod_argv[0] = argv0;

		/* Return value not checked: once unload_attempt() (below)
		 * wins the race, every later call here correctly gets -1
		 * (bad handle) — that's the point being tested, not a bug. */
		pm_metal_runtime_run(g_shared_handle, 1, mod_argv);
	}

	return NULL;
}

static void *unload_attempt(void *arg)
{
	(void)arg;

	int rc = pm_metal_runtime_unload(g_shared_handle);

	/* rc=-1 here is the wanted outcome, not a failure: it means we lost
	 * the race against an in-flight run() and runtime.c's refcount guard
	 * correctly refused the unload (see its "handle busy" message above).
	 * rc=0 just means every shared_handle_runner happened to finish first
	 * — also fine, nothing to assert either way. */
	printf("unload_attempt (racing shared_handle_runner threads): rc=%d (%s)\n", rc,
	       rc == 0 ? "won race, unloaded" : "lost race, correctly refused");
	return NULL;
}

int main(void)
{
	const char *vfs_root = getenv("PM_METAL_TEST_VFS_ROOT");
	if (!vfs_root) {
		fprintf(stderr, "PM_METAL_TEST_VFS_ROOT not set\n");
		return 1;
	}

	pm_metal_runtime_config_t cfg = {
		.memory_bytes = 64 * 1024 * 1024,
		.bytecode_bytes = 4 * 1024 * 1024,
		.vfs_root = vfs_root,
	};

	if (pm_metal_runtime_init(&cfg) != 0) {
		fprintf(stderr, "init failed\n");
		return 1;
	}

	if (pm_metal_runtime_load_file("/mods/t0_hello.wasm", &g_shared_handle) != 0) {
		fprintf(stderr, "shared handle load failed\n");
		pm_metal_runtime_shutdown();
		return 1;
	}

	pthread_t independent[NUM_WORKERS];
	pthread_t shared_runners[NUM_WORKERS];
	pthread_t racer;
	int i;

	for (i = 0; i < NUM_WORKERS; i++) {
		pthread_create(&independent[i], NULL, independent_worker, (void *)(intptr_t)i);
	}
	for (i = 0; i < NUM_WORKERS; i++) {
		pthread_create(&shared_runners[i], NULL, shared_handle_runner, (void *)(intptr_t)i);
	}
	pthread_create(&racer, NULL, unload_attempt, NULL);

	for (i = 0; i < NUM_WORKERS; i++) {
		pthread_join(independent[i], NULL);
	}
	for (i = 0; i < NUM_WORKERS; i++) {
		pthread_join(shared_runners[i], NULL);
	}
	pthread_join(racer, NULL);

	/* Idempotent-safe: -1 if unload_attempt() above already won the race. */
	pm_metal_runtime_unload(g_shared_handle);

	pm_metal_runtime_shutdown();

	printf("thread_stress: OK\n");
	return 0;
}
