/*
 * Runtime contract — long-lived dynamic loader.
 * See docs/RUNTIME.md.
 */
#ifndef PYMERGETIC_METAL_RUNTIME_RUNTIME_H_
#define PYMERGETIC_METAL_RUNTIME_RUNTIME_H_

#include <stddef.h>
#include <stdint.h>

typedef struct pm_metal_runtime_config {
	uint64_t memory_bytes; /* WAMR pool — wasm linear memory + WAMR's own runtime structs */
	uint64_t bytecode_bytes; /* bytecode arena — raw .wasm module buffers, separate pool */
	const char *vfs_root;
} pm_metal_runtime_config_t;

typedef struct pm_metal_runtime_handle {
	uint32_t id;
} pm_metal_runtime_handle_t;

/* Size of the internal handle table runtime.c hands out ids from — handle
 * ids are "1..PM_METAL_RUNTIME_MAX_HANDLES", 0 meaning "no handle". Public
 * (not just an internal runtime.c detail) specifically so every caller that
 * needs its own per-handle bookkeeping array sized to match (e.g.
 * src/linux/thread_stress_test.c's worker count) can size against *this*
 * one definition instead of hardcoding — and silently drifting from —
 * their own copy of the same number. */
#define PM_METAL_RUNTIME_MAX_HANDLES 8

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

/* impl: common — src/common/pymergetic/metal/runtime/runtime.c
 *
 * Resolves a guest-style path (same rule as load_file() above — "/"
 * prefix optional, always vfs_root-relative, never an arbitrary host
 * path) to the real host path backing it — the exact string concat
 * load_file() already does internally before handing the result to
 * pm_metal_port_read_file(), exposed here for callers that need to hand
 * a host path to some *other* port primitive themselves — this call
 * does no I/O itself, so it works whether or not `guest_path` actually
 * exists. Returns 0/-1 (uninitialized, or out_len too small for the
 * resolved path). */
int pm_metal_runtime_resolve_path(const char *guest_path, char *out, size_t out_len);

/* impl: common — src/common/pymergetic/metal/runtime/runtime.c
 *
 * run(): guest stdio inherits the host's own fd 0/1/2, no WASI env vars —
 * one shared console for every handle. run_ex(): same, but the caller
 * supplies real fds for the guest's fd 0/1/2 instead (-1 in any slot
 * still means "inherit the host's", per-slot) and its own "KEY=VALUE"
 * WASI env list (envc==0/envp==NULL means none, same as run()) — the
 * seam a future per-process console (its own pipe/log fd per handle)
 * and runtime/process.h's env support both hang off, so run() can stay
 * a thin wrapper. `envp` is not retained past this call — copy it
 * yourself first if it needs to outlive a background caller (see
 * runtime/process.h, the one caller that actually needs to). */
int pm_metal_runtime_run(pm_metal_runtime_handle_t h, int argc, char **argv);
int pm_metal_runtime_run_ex(pm_metal_runtime_handle_t h, int argc, char **argv, int envc, const char **envp,
			     int64_t stdin_fd, int64_t stdout_fd, int64_t stderr_fd);

/* impl: common — src/common/pymergetic/metal/runtime/runtime.c
 *
 * hold()/release(): bump/drop a handle's busy refcount directly — the
 * same refcount run_ex() itself bumps for the duration of one actual
 * execution (see runtime.c's file header — unload() refuses while it's
 * above zero). Exists for runtime/process.h's spawn(), which starts a
 * process on its own worker thread and returns before that thread has
 * necessarily run far enough to reach run_ex()'s own refcount++: without
 * a synchronous hold() taken at spawn() time (released once that
 * thread's run_ex() call returns), unload() could see a not-yet-
 * scheduled process's handle as idle and race it out from under the
 * thread about to use it. Ordinary callers of run()/run_ex() directly
 * (e.g. thread_stress_test.c, which calls them synchronously on threads
 * it manages itself) have no such gap and never need these. hold()
 * returns 0/-1 (bad handle — same as any other call here); release() is
 * a no-op on a bad handle (the caller already holds a valid reference by
 * construction, but this stays defensive rather than assuming that). */
int pm_metal_runtime_hold(pm_metal_runtime_handle_t h);
void pm_metal_runtime_release(pm_metal_runtime_handle_t h);

/* impl: common — src/common/pymergetic/metal/runtime/runtime.c */
int pm_metal_runtime_unload(pm_metal_runtime_handle_t h);

#endif /* PYMERGETIC_METAL_RUNTIME_RUNTIME_H_ */
