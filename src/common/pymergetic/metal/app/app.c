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
#include "pymergetic/metal/mount/pkg.h"
#include "pymergetic/metal/mount/populate.h"
#include "pymergetic/metal/mount/table.h"
#include "pymergetic/metal/runtime/process.h"
#include "pymergetic/metal/runtime/runtime.h"

/* Trailing-slash paths yield an empty basename — unsupported for argv[0]. */
static const char *pm_metal_app_basename_of(const char *path)
{
	const char *slash;
	const char *base;

	if (!path || !path[0]) {
		return NULL;
	}
	slash = strrchr(path, '/');
	base = slash ? slash + 1 : path;
	if (!base[0]) {
		return NULL;
	}
	return base;
}

int pm_metal_app_run_scripted(const char *argv0, int wasm_argc, char **wasm_argv,
			       const pm_metal_app_cli_mount_t *cli_mounts, size_t cli_mount_count,
			       int guest_argc, char **guest_argv, int guest_envc,
			       const char **guest_envp)
{
	int rc = 0;
	int i;
	size_t mi;
	int single_guest = (guest_argc > 0 || guest_envc > 0);

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
	for (mi = 0; mi < cli_mount_count; mi++) {
		const pm_metal_app_cli_mount_t *m = &cli_mounts[mi];

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

	/* Product default: /tmp as a named tmpfs ("tmp"). Test-only scratch
	 * mounts stay in verify fstab, not here. */
	if (!pm_metal_mount_exists("/tmp")) {
		if (pm_metal_mount("/tmp", PM_METAL_MOUNT_TMPFS, "tmp", NULL) != 0) {
			fprintf(stderr, "%s: warning: failed to mount tmpfs at /tmp\n", argv0);
		}
	}

	/* Named lz4 packages (mods, python, …) then anonymous populate
	 * embeds (verify-only fixtures). */
	if (pm_metal_pkg_apply_all() != 0) {
		fprintf(stderr, "%s: warning: guest package apply failed\n", argv0);
	}
	pm_metal_mount_populate_all();

	if (single_guest && wasm_argc != 1) {
		fprintf(stderr, "%s: guest argv/env requires exactly one .wasm path\n", argv0);
		pm_metal_process_shutdown();
		pm_metal_runtime_shutdown();
		return 1;
	}

	for (i = 0; i < wasm_argc; i++) {
		const char *path = wasm_argv[i];
		const char *base;
		pm_metal_runtime_handle_t h;
		pm_metal_process_id_t pid;
		int exit_code;
		int spawn_argc;
		char **spawn_argv;
		char *basename_argv[1];

		base = pm_metal_app_basename_of(path);
		if (!base) {
			fprintf(stderr, "%s: bad mod path (empty basename): %s\n", argv0,
				path ? path : "(null)");
			rc = 1;
			continue;
		}

		if (pm_metal_runtime_load_file(path, &h) != 0) {
			fprintf(stderr, "%s: load failed: %s\n", argv0, path);
			rc = 1;
			continue;
		}

		if (single_guest) {
			spawn_argc = guest_argc > 0 ? guest_argc : 1;
			if (guest_argc > 0) {
				spawn_argv = guest_argv;
			} else {
				basename_argv[0] = (char *)base;
				spawn_argv = basename_argv;
			}
		} else {
			basename_argv[0] = (char *)base;
			spawn_argc = 1;
			spawn_argv = basename_argv;
		}

		/* spawn()+wait() rather than a direct run(): one consistent
		 * model for every execution (see runtime/process.h) — stays
		 * exactly as sequential/blocking as a direct run() itself
		 * was, just now also visible to anything else that might
		 * list the process table while it's in flight. */
		if (pm_metal_process_spawn(h, spawn_argc, spawn_argv, guest_envc, guest_envp, -1, -1, -1,
					    NULL, NULL, &pid) != 0
		    || pm_metal_process_wait(pid, &exit_code) != 0) {
			fprintf(stderr, "%s: run failed: %s\n", argv0, path);
			exit_code = -1;
		}

		printf("%s: exit=%d\n", path, exit_code);
		fflush(stdout);
		if (exit_code != 0) {
			rc = 1;
		}

		if (pm_metal_runtime_unload(h) != 0) {
			fprintf(stderr, "%s: unload failed: %s\n", argv0, path);
			rc = 1;
		}
	}

	pm_metal_process_shutdown();
	pm_metal_runtime_shutdown();
	return rc;
}
