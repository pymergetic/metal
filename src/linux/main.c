/*
 * Linux runtime entry — arg parsing -> init -> app/app.h's scripted run
 * mode -> shutdown. See docs/RUNTIME.md.
 *
 * Usage: pm-linux-runtime --memory=<bytes> --bytecode-memory=<bytes>
 *                          --vfs-root=<dir> /mod.wasm [...]
 *
 * --memory sizes the kheap pool (wasm linear memory + WAMR's own runtime
 * structs); --bytecode-memory sizes the separate arena raw .wasm module
 * buffers are read into (see pymergetic/metal/memory/{kheap,bytecode}.h) —
 * the two never share space, so a big mod can't starve WAMR's own
 * bookkeeping or vice versa.
 *
 * Each positional .wasm path is guest-style, resolved against --vfs-root
 * by the runtime (see pm_metal_runtime_load_file) — loaded, run once
 * (argv[0] = its basename), and unloaded, all inside a single
 * init()/shutdown() pair. This is what scripts/verify-linux.sh and
 * verify-linux-threads.sh exercise.
 *
 * This file itself only does what is inherently linux-CLI-specific:
 * parsing argv (incl. resolving --vfs-root to an absolute path via
 * realpath(), POSIX-only) into a pm_metal_runtime_config_t, then handing
 * off to pymergetic/metal/app/app.h for the actual run — impl: common
 * (see there), reachable identically from any future target's own main.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "pymergetic/metal/app/app.h"
#include "pymergetic/metal/memory/memory.h"
#include "pymergetic/metal/runtime/process.h"
#include "pymergetic/metal/runtime/runtime.h"
#include "pymergetic/metal/util/size.h"

static void print_usage(const char *argv0)
{
	fprintf(stderr, "usage: %s --memory=<bytes> --bytecode-memory=<bytes> --vfs-root=<dir> /mod.wasm [...]\n",
		argv0);
}

int main(int argc, char **argv)
{
	uint64_t memory_bytes = 0;
	uint64_t bytecode_bytes = 0;
	const char *vfs_root_arg = NULL;
	int wasm_argc = 0;
	char **wasm_argv;
	int i;

	wasm_argv = malloc(sizeof(char *) * (size_t)(argc > 0 ? argc : 1));
	if (!wasm_argv) {
		return 1;
	}

	for (i = 1; i < argc; i++) {
		if (!strncmp(argv[i], "--memory=", 9)) {
			memory_bytes = strtoull(argv[i] + 9, NULL, 0);
		} else if (!strncmp(argv[i], "--bytecode-memory=", 18)) {
			bytecode_bytes = strtoull(argv[i] + 18, NULL, 0);
		} else if (!strncmp(argv[i], "--vfs-root=", 11)) {
			vfs_root_arg = argv[i] + 11;
		} else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
			print_usage(argv[0]);
			free(wasm_argv);
			return 0;
		} else {
			wasm_argv[wasm_argc++] = argv[i];
		}
	}

	if (memory_bytes == 0 || bytecode_bytes == 0 || !vfs_root_arg || wasm_argc == 0) {
		print_usage(argv[0]);
		free(wasm_argv);
		return 1;
	}

	char vfs_root_abs[PATH_MAX];
	if (!realpath(vfs_root_arg, vfs_root_abs)) {
		fprintf(stderr, "%s: bad --vfs-root: %s\n", argv[0], vfs_root_arg);
		free(wasm_argv);
		return 1;
	}

	pm_metal_runtime_config_t cfg = {
		.memory_bytes = memory_bytes,
		.bytecode_bytes = bytecode_bytes,
		.vfs_root = vfs_root_abs,
	};

	if (pm_metal_runtime_init(&cfg) != 0) {
		fprintf(stderr, "%s: runtime init failed\n", argv[0]);
		free(wasm_argv);
		return 1;
	}
	if (pm_metal_process_init() != 0) {
		fprintf(stderr, "%s: process table init failed\n", argv[0]);
		pm_metal_runtime_shutdown();
		free(wasm_argv);
		return 1;
	}

	/* machine_ram = real total host RAM (diagnostics only); kheap_pool =
	 * what WAMR actually got; bytecode_pool = the separate arena raw
	 * .wasm buffers are read into — never the same number on linux, and
	 * deliberately not derived from one another. Three ops tables, one
	 * per concern — see pymergetic/metal/memory/ and docs/RUNTIME.md. */
	char ram_human[48];
	char pool_human[48];
	char bytecode_human[48];
	pm_metal_util_size_format_bytes(ram_human, sizeof(ram_human), pm_metal_memory_ram_ops()->probe());
	pm_metal_util_size_format_bytes(pool_human, sizeof(pool_human), pm_metal_memory_kheap_ops()->bytes());
	pm_metal_util_size_format_bytes(bytecode_human, sizeof(bytecode_human),
					 pm_metal_memory_bytecode_ops()->bytes());
	printf("machine_ram=%s kheap_pool=%s bytecode_pool=%s vfs_root=%s\n", ram_human, pool_human,
	       bytecode_human, vfs_root_abs);
	fflush(stdout);

	int rc = pm_metal_app_run_scripted(argv[0], wasm_argc, wasm_argv);

	free(wasm_argv);
	return rc;
}
