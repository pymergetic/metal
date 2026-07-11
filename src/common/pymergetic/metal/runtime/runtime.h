/*
 * Runtime contract — long-lived dynamic loader.
 * See docs/RUNTIME.md.
 */
#ifndef PYMERGETIC_METAL_RUNTIME_RUNTIME_H_
#define PYMERGETIC_METAL_RUNTIME_RUNTIME_H_

#include <stdint.h>

typedef struct pm_metal_runtime_config {
	uint64_t memory_bytes; /* WAMR pool — wasm linear memory + WAMR's own runtime structs */
	uint64_t bytecode_bytes; /* bytecode arena — raw .wasm module buffers, separate pool */
	const char *vfs_root;
} pm_metal_runtime_config_t;

typedef struct pm_metal_runtime_handle {
	uint32_t id;
} pm_metal_runtime_handle_t;

/* impl: common — src/common/pymergetic/metal/runtime/runtime.c — lifecycle */
int pm_metal_runtime_init(const pm_metal_runtime_config_t *cfg);
int pm_metal_runtime_shutdown(void);

/* impl: common — src/common/pymergetic/metal/runtime/runtime.c — dynamic loader.
 * load_file(): path is guest-style (e.g. "/mods/foo.wasm"), resolved against
 * cfg->vfs_root — never a host path outside the VFS tree. Same tree the
 * guest's own WASI opens resolve against. */
int pm_metal_runtime_load_file(const char *path, pm_metal_runtime_handle_t *out);
int pm_metal_runtime_load_bytes(const uint8_t *wasm, uint32_t len,
				pm_metal_runtime_handle_t *out);

/* run(): guest stdio inherits the host's own fd 0/1/2 — one shared console
 * for every handle (see docs/RUNTIME.md "Console model"). run_ex(): same,
 * but the caller supplies real fds for the guest's fd 0/1/2 instead (-1 in
 * any slot still means "inherit the host's", per-slot) — the seam a future
 * per-process console (its own pipe/log fd per handle) hangs off, so run()
 * can stay a thin wrapper and this signature doesn't need to change again. */
int pm_metal_runtime_run(pm_metal_runtime_handle_t h, int argc, char **argv);
int pm_metal_runtime_run_ex(pm_metal_runtime_handle_t h, int argc, char **argv,
			     int64_t stdin_fd, int64_t stdout_fd, int64_t stderr_fd);

int pm_metal_runtime_unload(pm_metal_runtime_handle_t h);

#endif /* PYMERGETIC_METAL_RUNTIME_RUNTIME_H_ */
