/*
 * App — see app.h. Ported straight from src/linux/main.c's own
 * run_scripted_mode(): raw pthread_create()/pthread_join() -> port/worker.h
 * (see runtime/process.h's spawn(), which owns that now).
 */
#include "pymergetic/metal/app/app.h"

#include <stdio.h>
#include <string.h>

#include "pymergetic/metal/mount/fstab.h"
#include "pymergetic/metal/mount/ops.h"
#include "pymergetic/metal/mount/populate.h"
#include "pymergetic/metal/mount/table.h"
#include "pymergetic/metal/runtime/process.h"
#include "pymergetic/metal/runtime/runtime.h"

static const char *pm_metal_app_basename_of(const char *path)
{
	const char *slash = strrchr(path, '/');

	return slash ? slash + 1 : path;
}

int pm_metal_app_run_scripted(const char *argv0, int wasm_argc, char **wasm_argv,
			       const pm_metal_app_cli_mount_t *cli_mounts, size_t cli_mount_count)
{
	int rc = 0;
	int i;

	/* Stage B — see docs/MOUNT.md "Boot sequence". Right after init()'s
	 * own Stage A root mount, before any mod is loaded, so a mod's own
	 * WASI I/O and this file's own load_file() calls both see every
	 * fstab-declared (and CLI --mount=, below) mount already in place.
	 * No-op if the just-mounted root has no /etc/fstab — every existing
	 * scripted-mode caller that never added one keeps behaving exactly
	 * as before. */
	pm_metal_mount_fstab_apply("/etc/fstab");

	/* CLI --mount= sugar — same apply function fstab lines themselves
	 * use, deliberately applied *after* the real fstab above (see
	 * app.h's own doc comment) so an ad hoc CLI mount overrides a
	 * conflicting fstab line rather than the other way around. */
	for (i = 0; i < (int)cli_mount_count; i++) {
		const pm_metal_app_cli_mount_t *m = &cli_mounts[i];

		pm_metal_mount_fstab_apply_fields(m->source, m->target, m->fstype, m->opts);
	}

	/* Ensure guest /proc (procfs hooks → /proc/mounts, …). fstab/CLI may
	 * already have mounted it; otherwise mount the proc fstype here so
	 * busybox `mount` and debugging always have a standard path. */
	if (!pm_metal_mount_exists("/proc")) {
		if (pm_metal_mount("/proc", PM_METAL_MOUNT_PROC, "proc", NULL) != 0) {
			fprintf(stderr, "%s: warning: failed to mount proc at /proc\n", argv0);
		}
	}

	/* Phase 4 — extract every registered ustar [+ lz4] embed against
	 * guest "/" now that Stage B mounts exist. No-op if nothing
	 * registered (every existing caller). See mount/populate.h. */
	pm_metal_mount_populate_all();

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
