/*
 * App — see app.h. Ported straight from src/linux/main.c's own
 * run_scripted_mode(): raw pthread_create()/pthread_join() -> port/worker.h
 * (see runtime/process.h's spawn(), which owns that now).
 */
#include "pymergetic/metal/app/app.h"

#include <stdio.h>
#include <string.h>

#include "pymergetic/metal/runtime/process.h"
#include "pymergetic/metal/runtime/runtime.h"

static const char *pm_metal_app_basename_of(const char *path)
{
	const char *slash = strrchr(path, '/');

	return slash ? slash + 1 : path;
}

int pm_metal_app_run_scripted(const char *argv0, int wasm_argc, char **wasm_argv)
{
	int rc = 0;
	int i;

	for (i = 0; i < wasm_argc; i++) {
		const char *path = wasm_argv[i];
		pm_metal_runtime_handle_t h;

		if (pm_metal_runtime_load_file(path, &h) != 0) {
			fprintf(stderr, "%s: load failed: %s\n", argv0, path);
			rc = 1;
			continue;
		}

		char *mod_argv[1];
		mod_argv[0] = (char *)pm_metal_app_basename_of(path);

		/* spawn()+wait() rather than a direct run(): one consistent
		 * model for every execution (see runtime/process.h) — stays
		 * exactly as sequential/blocking as a direct run() itself
		 * was, just now also visible to anything else that might
		 * list the process table while it's in flight. */
		pm_metal_process_id_t pid;
		int exit_code;

		if (pm_metal_process_spawn(h, 1, mod_argv, 0, NULL, -1, -1, -1, NULL, NULL, &pid) != 0
		    || pm_metal_process_wait(pid, &exit_code) != 0) {
			fprintf(stderr, "%s: run failed: %s\n", argv0, path);
			exit_code = -1;
		}

		printf("%s: exit=%d\n", path, exit_code);
		fflush(stdout);
		if (exit_code != 0) {
			rc = 1;
		}

		pm_metal_runtime_unload(h);
	}

	pm_metal_process_shutdown();
	pm_metal_runtime_shutdown();
	return rc;
}
