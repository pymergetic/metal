/*
 * Linux runtime entry — init → loader loop → shutdown.
 * See docs/RUNTIME.md.
 *
 * Usage: pm-linux-runtime --memory=<bytes> --bytecode-memory=<bytes>
 *                          --vfs-root=<dir> /mod.wasm [/mod2.wasm ...]
 *
 * --memory sizes the kheap pool (wasm linear memory + WAMR's own runtime
 * structs); --bytecode-memory sizes the separate arena raw .wasm module
 * buffers are read into (see pymergetic/metal/memory/{kheap,bytecode}.h) —
 * the two never share space, so a big mod can't starve WAMR's own
 * bookkeeping or vice versa.
 *
 * Each positional .wasm path is guest-style, resolved against --vfs-root by
 * the runtime (see pm_metal_runtime_load_file) — not a host path outside the
 * VFS tree. Each is loaded, run once (argv[0] = its basename), and unloaded —
 * all inside a single init()/shutdown() pair, proving the persistent-host
 * model: one process, many mods, shared vfs_root.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "pymergetic/metal/memory/memory.h"
#include "pymergetic/metal/runtime/runtime.h"
#include "pymergetic/metal/util/size.h"

static void print_usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s --memory=<bytes> --bytecode-memory=<bytes> --vfs-root=<dir> "
		"/mod.wasm [/mod2.wasm ...]\n",
		argv0);
}

static const char *basename_of(const char *path)
{
	const char *slash = strrchr(path, '/');

	return slash ? slash + 1 : path;
}

int main(int argc, char **argv)
{
	uint64_t memory_bytes = 0;
	uint64_t bytecode_bytes = 0;
	const char *vfs_root_arg = NULL;
	int wasm_argc = 0;
	char **wasm_argv;
	int i;
	int rc = 0;

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

	/* machine_ram = real total host RAM (diagnostics only); kheap_pool =
	 * what WAMR actually got; bytecode_pool = the separate arena raw
	 * .wasm buffers are read into — never the same number on linux, and
	 * deliberately not derived from one another. Three ops tables, one
	 * per concern — see pymergetic/metal/memory/ and docs/RUNTIME.md. */
	char ram_human[48];
	char pool_human[48];
	char bytecode_human[48];
	pm_metal_util_size_format_bytes(ram_human, sizeof(ram_human),
					 pm_metal_memory_ram_ops()->probe());
	pm_metal_util_size_format_bytes(pool_human, sizeof(pool_human),
					 pm_metal_memory_kheap_ops()->bytes());
	pm_metal_util_size_format_bytes(bytecode_human, sizeof(bytecode_human),
					 pm_metal_memory_bytecode_ops()->bytes());
	printf("machine_ram=%s kheap_pool=%s bytecode_pool=%s vfs_root=%s\n",
	       ram_human, pool_human, bytecode_human, vfs_root_abs);
	fflush(stdout);

	for (i = 0; i < wasm_argc; i++) {
		const char *path = wasm_argv[i];
		pm_metal_runtime_handle_t h;

		if (pm_metal_runtime_load_file(path, &h) != 0) {
			fprintf(stderr, "%s: load failed: %s\n", argv[0], path);
			rc = 1;
			continue;
		}

		char *mod_argv[1];
		mod_argv[0] = (char *)basename_of(path);

		int exit_code = pm_metal_runtime_run(h, 1, mod_argv);
		printf("%s: exit=%d\n", path, exit_code);
		fflush(stdout);
		if (exit_code != 0) {
			rc = 1;
		}

		pm_metal_runtime_unload(h);
	}

	pm_metal_runtime_shutdown();
	free(wasm_argv);

	return rc;
}
