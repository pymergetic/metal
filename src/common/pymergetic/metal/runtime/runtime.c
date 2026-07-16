/*
 * Runtime — common implementations (WAMR dynamic loader).
 * See docs/RUNTIME.md.
 *
 * WASI dir/argv are fixed at wasm_runtime_instantiate() time, but this API
 * hands argv to run() — after load() already produced a handle. So
 * instantiate is deferred from load() to run(): load() only parses the
 * module (wasm_runtime_load) and keeps the byte buffer alive; run() sets
 * WASI args for that call, instantiates, executes main, then deinstantiates.
 * unload() unloads the parsed module and frees the buffer the runtime has
 * owned since load(). Module bytecode buffers live in a dedicated arena —
 * separate from the kheap pool handed to wasm_runtime_full_init() — so a
 * mod's raw .wasm bytes never compete with WAMR's own runtime structs or
 * guest linear memory for space; see pm_metal_memory_bytecode_ops() in
 * memory/bytecode.h. Both pools are established at init() and released
 * together at shutdown(), untouched by anything in between.
 *
 * load_file() takes a guest-style path (e.g. "/mods/foo.wasm") resolved
 * against the mount table (see mount/table.h) — the exact same longest-
 * prefix resolution WASI itself does against build_map_dir_list()'s own
 * output for the guest. There is no separate "host path" for mod
 * bytecode: the loader and the guest read from the same mounted storage,
 * so this keeps working unchanged whether the matched mount is today's
 * single host-dir root or (later) a mounted tmpfs/littlefs on zephyr —
 * see docs/MOUNT.md. Path resolution (string concat) stays here — OS-
 * independent — but the actual bytes-from-storage read is a port call
 * (pm_metal_port_read_file): bare libc stdio is not guaranteed to reach
 * mounted storage on embedded targets, so this file must never call
 * fopen()/fread() etc. directly.
 *
 * Concurrency — see docs/RUNTIME.md "Concurrency" for the full writeup.
 * Short version: init()/shutdown() are controller-thread-only — call each
 * exactly once, with no other pm_metal_runtime_*() call in flight (same
 * rule WAMR itself uses for wasm_runtime_full_init()/destroy()). Once
 * init() has returned, load_file()/load_bytes()/run()/unload() are safe
 * to call concurrently from multiple threads, including on different
 * handles at the same time.
 *
 * g_pm_metal_runtime_lock guards the shared handle table AND every call
 * into wasm_runtime_load()/instantiate()/deinstantiate()/unload() — i.e.
 * those four calls are fully serialized process-wide, even across
 * different handles. This is more conservative than WAMR's own docs
 * suggest (they describe the shared Alloc_With_Pool heap as internally
 * locked, implying concurrent load/instantiate on different modules
 * should be safe without a caller-side lock) — but running the actual
 * stress test in scripts/verify-linux-threads.sh under ThreadSanitizer
 * caught real data races inside WAMR's own EMS allocator
 * (external/wamr/core/shared/mem-alloc/ems/ems_alloc.c, e.g. alloc_hmu())
 * when load/instantiate/deinstantiate/unload ran concurrently on
 * different modules in this vendored build — so this file doesn't trust
 * that assumption. wasm_application_execute_main() itself showed no
 * races in the same run and stays unlocked here, so two different
 * handles' guest code still runs fully in parallel — only the
 * WAMR-internal setup/teardown calls around it are serialized. A handle
 * mid-run() (refcount > 0) cannot be unload()ed — unload() fails with -1
 * rather than racing the module out from under a running instance; the
 * caller must retry. hold()/release() below let a caller bump/drop that
 * same refcount directly, synchronously, without an actual run_ex() call
 * of their own — see their own doc comment in runtime.h for why
 * runtime/process.h's spawn() needs exactly that. set_wasi_args() is
 * included in the same serialized section for a second, independent
 * reason: it writes onto slot->module itself (not a future instance),
 * consumed by the very next instantiate() call — those two must stay
 * paired with no other run() on the same handle sneaking its own
 * set_wasi_args() in between.
 */
#include "pymergetic/metal/runtime/runtime.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pymergetic/metal/memory/memory.h"
#include "pymergetic/metal/mount/mount.h"
#include "pymergetic/metal/mount/table.h"
#include "pymergetic/metal/mount/ops.h"
#include "pymergetic/metal/mount/populate.h"
#include "pymergetic/metal/mount/proc.h"
#include "pymergetic/metal/port/lock.h"
#include "pymergetic/metal/port/platform.h"
#include "pymergetic/metal/util/arena.h"
#include "pymergetic/metal/util/log.h"
#include "pymergetic/metal/util/lz4.h"
#include "pymergetic/metal/util/size.h"
#include "pymergetic/metal/util/tar.h"
#include "wasm_export.h"

/* PM_METAL_RUNTIME_MAX_HANDLES itself now lives in runtime.h — public, see
 * there — not redefined here. */
#define PM_METAL_RUNTIME_MAX_PATH 256 /* resolved vfs path buffer — not PATH_MAX (POSIX-only, and too large for small stacks) */
#define PM_METAL_RUNTIME_STACK_SIZE (64 * 1024)
#define PM_METAL_RUNTIME_HEAP_SIZE 0 /* wasi-libc manages its own heap in linear memory */
#define PM_METAL_RUNTIME_ERROR_BUF_SIZE 128

typedef struct pm_metal_runtime_slot {
	int used;
	int refcount; /* # of run() calls currently in flight on this handle — unload() refuses while > 0 */
	wasm_module_t module;
	uint8_t *buf;
	uint32_t buf_len;
} pm_metal_runtime_slot_t;

static struct {
	int initialized;
	char **addr_pool; /* owned copies of cfg->addr_pool's strings, see runtime.h */
	uint32_t addr_pool_count;
	char **ns_lookup_pool; /* owned copies of cfg->ns_lookup_pool's strings */
	uint32_t ns_lookup_pool_count;
	pm_metal_runtime_slot_t slots[PM_METAL_RUNTIME_MAX_HANDLES];
} g_pm_metal_runtime;

/* Guards g_pm_metal_runtime.slots[] (used/refcount/module/buf/buf_len) and
 * the set_wasi_args()+instantiate() pair in run() — see file header. Not
 * needed for the mount table (see mount/table.h — write-once/rarely at
 * init()/fstab-apply time, read-only after, same controller-thread
 * contract) nor for `initialized` itself (checked unlocked as a fast
 * precondition — see file header). */
static pm_metal_port_mutex_t g_pm_metal_runtime_lock;

/* Caller must hold g_pm_metal_runtime_lock. */
static pm_metal_runtime_slot_t *pm_metal_runtime_slot_for(pm_metal_runtime_handle_t h)
{
	if (h.id == 0 || h.id > PM_METAL_RUNTIME_MAX_HANDLES) {
		return NULL;
	}

	pm_metal_runtime_slot_t *slot = &g_pm_metal_runtime.slots[h.id - 1];

	return slot->used ? slot : NULL;
}

static int pm_metal_runtime_load_common(uint8_t *buf, uint32_t len,
					 pm_metal_runtime_handle_t *out)
{
	pm_metal_runtime_slot_t *slot = NULL;
	uint32_t i;

	/* wasm_runtime_load() stays inside the lock along with the slot
	 * claim — see file header re: WAMR's shared pool allocator not
	 * actually being safe for concurrent load() on different modules in
	 * this vendored build (empirically found via ThreadSanitizer). */
	pm_metal_port_mutex_lock(&g_pm_metal_runtime_lock);
	for (i = 0; i < PM_METAL_RUNTIME_MAX_HANDLES; i++) {
		if (!g_pm_metal_runtime.slots[i].used) {
			slot = &g_pm_metal_runtime.slots[i];
			break;
		}
	}
	if (!slot) {
		pm_metal_port_mutex_unlock(&g_pm_metal_runtime_lock);
		fprintf(stderr, "pm_metal_runtime: handle table full\n");
		return -1;
	}

	char error_buf[PM_METAL_RUNTIME_ERROR_BUF_SIZE];
	wasm_module_t module =
		wasm_runtime_load(buf, len, error_buf, sizeof(error_buf));
	if (!module) {
		pm_metal_port_mutex_unlock(&g_pm_metal_runtime_lock);
		fprintf(stderr, "pm_metal_runtime: load failed: %s\n", error_buf);
		return -1;
	}
	slot->used = 1;
	slot->module = module;
	slot->buf = buf;
	slot->buf_len = len;
	slot->refcount = 0;
	pm_metal_port_mutex_unlock(&g_pm_metal_runtime_lock);

	out->id = (uint32_t)(i + 1);
	return 0;
}

/* load_file() paths are guest-style, resolved through the mount table
 * exactly like WASI resolves a guest open() (same longest-prefix rule —
 * see mount/table.h) — never an arbitrary host path. Keeps working
 * unchanged whether the matched mount is a host dir or (later) a mounted
 * tmpfs/littlefs — see docs/MOUNT.md. */
static int pm_metal_runtime_resolve_vfs_path(const char *guest_path, char *out, size_t out_len)
{
	return pm_metal_mount_resolve(guest_path, out, out_len);
}

#ifdef PM_METAL_RUNTIME_MULTI_MODULE
/* Multi-module: one .wasm's own (import "libb" "add" (func ...)) names
 * "libb" as a *module*, not a file — WAMR calls back here to actually turn
 * that name into bytes the moment it first needs them (i.e. lazily, while
 * loading whatever module imported it), by the exact same convention
 * load_file() already uses for a top-level mod: "<module_name>.wasm",
 * resolved against vfs_root under /mods — so a dependency named "libb" is
 * just another ordinary /mods/libb.wasm, loadable standalone too, nothing
 * multi-module-specific about how it's stored. Reuses the bytecode arena
 * (not malloc) for the same reason load_file() does — see this file's own
 * header — and module_destroyer below is WAMR's own paired callback to
 * free exactly that allocation once the dependency is unloaded. */
static bool pm_metal_runtime_module_reader(package_type_t module_type, const char *module_name, uint8_t **p_buffer,
					    uint32_t *p_size)
{
	(void)module_type;

	char guest_path[PM_METAL_RUNTIME_MAX_PATH];
	int n = snprintf(guest_path, sizeof(guest_path), "/mods/%s.wasm", module_name);
	if (n <= 0 || (size_t)n >= sizeof(guest_path)) {
		return false;
	}

	char host_path[PM_METAL_RUNTIME_MAX_PATH];
	if (pm_metal_runtime_resolve_vfs_path(guest_path, host_path, sizeof(host_path)) != 0) {
		return false;
	}

	if (pm_metal_port_read_file(host_path, p_buffer, p_size) != 0) {
		fprintf(stderr, "pm_metal_runtime: dependency module read failed: %s\n", guest_path);
		return false;
	}
	return true;
}

static void pm_metal_runtime_module_destroyer(uint8_t *buffer, uint32_t size)
{
	(void)size;
	pm_metal_memory_bytecode_ops()->free(buffer);
}
#endif /* PM_METAL_RUNTIME_MULTI_MODULE */

/* Copies `count` strings out of `src` (caller-owned, need not outlive this
 * call) into a freshly malloc'd array of freshly malloc'd strings — used
 * for cfg->addr_pool/ns_lookup_pool below, same reasoning as vfs_root's
 * own hand-rolled copy just above (no strdup() — POSIX/BSD extension, not
 * ISO C). count == 0 is not an error: *out is set to NULL, matching what
 * a config that opts out of sockets entirely passes to
 * wasm_runtime_set_wasi_{addr,ns_lookup}_pool() every run_ex() call. On
 * failure, anything already allocated this call is freed before
 * returning -1 — never leaves a partially-built array in *out. */
static int pm_metal_runtime_dup_string_array(const char **src, uint32_t count, char ***out)
{
	if (count == 0) {
		*out = NULL;
		return 0;
	}
	if (!src) {
		return -1;
	}

	char **dst = malloc(sizeof(char *) * count);
	if (!dst) {
		return -1;
	}

	uint32_t i;
	for (i = 0; i < count; i++) {
		size_t len;

		if (!src[i]) {
			while (i > 0) {
				free(dst[--i]);
			}
			free(dst);
			return -1;
		}
		len = strlen(src[i]) + 1;
		dst[i] = malloc(len);
		if (!dst[i]) {
			while (i > 0) {
				free(dst[--i]);
			}
			free(dst);
			return -1;
		}
		memcpy(dst[i], src[i], len);
	}
	*out = dst;
	return 0;
}

static void pm_metal_runtime_free_string_array(char **arr, uint32_t count)
{
	uint32_t i;

	for (i = 0; i < count; i++) {
		free(arr[i]);
	}
	free(arr);
}

static void pm_metal_runtime_unload_slot(pm_metal_runtime_slot_t *slot)
{
	wasm_runtime_unload(slot->module);
	pm_metal_memory_bytecode_ops()->free(slot->buf);
	slot->used = 0;
	slot->module = NULL;
	slot->buf = NULL;
	slot->buf_len = 0;
}

int pm_metal_runtime_init(const pm_metal_runtime_config_t *cfg)
{
	if (g_pm_metal_runtime.initialized) {
		fprintf(stderr, "pm_metal_runtime: already initialized\n");
		return -1;
	}
	if (!cfg || !cfg->vfs_root || cfg->memory_bytes == 0
	    || cfg->memory_bytes > UINT32_MAX || cfg->bytecode_bytes == 0
	    || cfg->bytecode_bytes > UINT32_MAX
	    || (cfg->addr_pool_count > 0 && !cfg->addr_pool)
	    || (cfg->ns_lookup_pool_count > 0 && !cfg->ns_lookup_pool)) {
		fprintf(stderr, "pm_metal_runtime: bad config\n");
		return -1;
	}

	const pm_metal_memory_ops_t *kheap = pm_metal_memory_kheap_ops();
	const pm_metal_memory_ops_t *bytecode = pm_metal_memory_bytecode_ops();

	uint64_t pool_bytes = 0;
	void *pool = kheap->establish(cfg->memory_bytes, &pool_bytes);
	if (!pool || pool_bytes == 0 || pool_bytes > UINT32_MAX) {
		fprintf(stderr, "pm_metal_runtime: pool establish failed\n");
		if (pool) {
			kheap->release();
		}
		return -1;
	}

	uint64_t bytecode_bytes = 0;
	void *bytecode_pool = bytecode->establish(cfg->bytecode_bytes, &bytecode_bytes);
	if (!bytecode_pool || bytecode_bytes == 0) {
		fprintf(stderr, "pm_metal_runtime: bytecode pool establish failed\n");
		if (bytecode_pool) {
			bytecode->release();
		}
		kheap->release();
		return -1;
	}

	/* Stage A (root mount) — see docs/MOUNT.md "Boot sequence". Establishes
	 * the mount table's own "/" entry from cfg->vfs_root exactly like
	 * today's single vfs_root, via the HOSTDIR kind's trivial passthrough
	 * (mount/hostdir.c) — no behavior change, just routed through the
	 * mount table instead of a hand-rolled pair of strings. Stage B
	 * (/etc/fstab, later mounts) is layered on top of this by callers
	 * (see app.c/main.c), not by init() itself. */
	if (pm_metal_mount("/", PM_METAL_MOUNT_HOSTDIR, cfg->vfs_root, NULL) != 0) {
		fprintf(stderr, "pm_metal_runtime: root mount failed: %s\n", cfg->vfs_root);
		bytecode->release();
		kheap->release();
		return -1;
	}

	char **addr_pool = NULL;
	if (pm_metal_runtime_dup_string_array(cfg->addr_pool, cfg->addr_pool_count, &addr_pool) != 0) {
		pm_metal_umount("/");
		bytecode->release();
		kheap->release();
		return -1;
	}

	char **ns_lookup_pool = NULL;
	if (pm_metal_runtime_dup_string_array(cfg->ns_lookup_pool, cfg->ns_lookup_pool_count, &ns_lookup_pool) != 0) {
		pm_metal_runtime_free_string_array(addr_pool, cfg->addr_pool_count);
		pm_metal_umount("/");
		bytecode->release();
		kheap->release();
		return -1;
	}

	RuntimeInitArgs init_args;
	memset(&init_args, 0, sizeof(init_args));
	init_args.mem_alloc_type = Alloc_With_Pool;
	init_args.mem_alloc_option.pool.heap_buf = pool;
	init_args.mem_alloc_option.pool.heap_size = (uint32_t)pool_bytes;

	if (!wasm_runtime_full_init(&init_args)) {
		fprintf(stderr, "pm_metal_runtime: wasm_runtime_full_init failed\n");
		pm_metal_runtime_free_string_array(ns_lookup_pool, cfg->ns_lookup_pool_count);
		pm_metal_runtime_free_string_array(addr_pool, cfg->addr_pool_count);
		pm_metal_umount("/");
		bytecode->release();
		kheap->release();
		return -1;
	}

#ifdef PM_METAL_RUNTIME_MULTI_MODULE
	/* Must happen before any load() of a module that might depend on
	 * another one by name — see pm_metal_runtime_module_reader() above
	 * for the actual "module_name" -> bytes convention. */
	wasm_runtime_set_module_reader(pm_metal_runtime_module_reader, pm_metal_runtime_module_destroyer);
#endif

	/* Must happen before any load()/instantiate() of a module that
	 * might import these — every mod's own compile already resolved
	 * them to unresolved imports under that module's own
	 * PM_METAL_UTIL_{ARENA,LOG,LZ4,SIZE,TAR}_WASI_MODULE name (see
	 * util/{arena,log,lz4,size,tar}.h), so this is the one place that
	 * has to run first, exactly once per init()/shutdown() cycle. Each
	 * of the five registers its own small NativeSymbol table under its
	 * own name (see that module's own .c) rather than one shared table
	 * here — nothing about resolving an import needs a central table,
	 * and this way each module's wasm-import glue lives right next to
	 * its host impl. wasm_runtime_destroy() below frees WAMR's own
	 * registration bookkeeping for us; nothing to undo here on our
	 * side. */
	if (pm_metal_util_arena_native_register() != 0
	    || pm_metal_util_log_native_register() != 0
	    || pm_metal_util_lz4_native_register() != 0
	    || pm_metal_util_size_native_register() != 0
	    || pm_metal_util_tar_native_register() != 0
	    || pm_metal_mount_native_register() != 0) {
		fprintf(stderr, "pm_metal_runtime: native registration failed\n");
		wasm_runtime_destroy();
		pm_metal_runtime_free_string_array(ns_lookup_pool, cfg->ns_lookup_pool_count);
		pm_metal_runtime_free_string_array(addr_pool, cfg->addr_pool_count);
		pm_metal_umount("/");
		bytecode->release();
		kheap->release();
		return -1;
	}

	memset(g_pm_metal_runtime.slots, 0, sizeof(g_pm_metal_runtime.slots));
	g_pm_metal_runtime.addr_pool = addr_pool;
	g_pm_metal_runtime.addr_pool_count = cfg->addr_pool_count;
	g_pm_metal_runtime.ns_lookup_pool = ns_lookup_pool;
	g_pm_metal_runtime.ns_lookup_pool_count = cfg->ns_lookup_pool_count;

	/* Init last, right before publishing `initialized` — nothing past
	 * this point can fail, so this runs exactly once per init()/
	 * shutdown() cycle (see destroy() in shutdown()); re-init()ing a
	 * live mutex without destroy() first is undefined behavior. */
	pm_metal_port_mutex_init(&g_pm_metal_runtime_lock);
	g_pm_metal_runtime.initialized = 1;
	pm_metal_mount_proc_note_boot();

	return 0;
}

int pm_metal_runtime_shutdown(void)
{
	if (!g_pm_metal_runtime.initialized) {
		return -1;
	}

	uint32_t i;

	/* Controller-thread-only, per the file-header contract — no worker
	 * should still be calling load/run/unload at this point, so the
	 * lock below is defensive, not load-bearing. */
	for (i = 0; i < PM_METAL_RUNTIME_MAX_HANDLES; i++) {
		if (g_pm_metal_runtime.slots[i].used) {
			pm_metal_runtime_unload_slot(&g_pm_metal_runtime.slots[i]);
		}
	}

	wasm_runtime_destroy();

	pm_metal_runtime_free_string_array(g_pm_metal_runtime.ns_lookup_pool, g_pm_metal_runtime.ns_lookup_pool_count);
	pm_metal_runtime_free_string_array(g_pm_metal_runtime.addr_pool, g_pm_metal_runtime.addr_pool_count);
	pm_metal_mount_shutdown_all();
	pm_metal_mount_populate_clear();
	pm_metal_memory_bytecode_ops()->release();
	pm_metal_memory_kheap_ops()->release();

	pm_metal_port_mutex_destroy(&g_pm_metal_runtime_lock);
	memset(&g_pm_metal_runtime, 0, sizeof(g_pm_metal_runtime));

	return 0;
}

int pm_metal_runtime_load_file(const char *path, pm_metal_runtime_handle_t *out)
{
	if (!g_pm_metal_runtime.initialized || !path || !out || !path[0]) {
		return -1;
	}

	char host_path[PM_METAL_RUNTIME_MAX_PATH];
	if (pm_metal_runtime_resolve_vfs_path(path, host_path, sizeof(host_path)) != 0) {
		fprintf(stderr, "pm_metal_runtime: path too long: %s\n", path);
		return -1;
	}

	uint8_t *buf;
	uint32_t len;
	if (pm_metal_port_read_file(host_path, &buf, &len) != 0) {
		fprintf(stderr, "pm_metal_runtime: read failed: %s (host_path=%s)\n", path, host_path);
		return -1;
	}

	if (pm_metal_runtime_load_common(buf, len, out) != 0) {
		pm_metal_memory_bytecode_ops()->free(buf);
		return -1;
	}

	return 0;
}

int pm_metal_runtime_load_bytes(const uint8_t *wasm, uint32_t len,
				pm_metal_runtime_handle_t *out)
{
	if (!g_pm_metal_runtime.initialized || !wasm || len == 0 || !out) {
		return -1;
	}

	const pm_metal_memory_ops_t *bytecode = pm_metal_memory_bytecode_ops();

	/* Runtime owns writable wasm buffers until unload — copy caller bytes
	 * into the bytecode arena so wasm_runtime_load() may mutate/reference
	 * them independent of the caller's original buffer lifetime. */
	uint8_t *buf = bytecode->alloc(len);
	if (!buf) {
		fprintf(stderr, "pm_metal_runtime: bytecode arena exhausted (%u bytes)\n", len);
		return -1;
	}
	memcpy(buf, wasm, len);

	if (pm_metal_runtime_load_common(buf, len, out) != 0) {
		bytecode->free(buf);
		return -1;
	}

	return 0;
}

int pm_metal_runtime_resolve_path(const char *guest_path, char *out, size_t out_len)
{
	if (!g_pm_metal_runtime.initialized || !guest_path || !out) {
		return -1;
	}
	return pm_metal_runtime_resolve_vfs_path(guest_path, out, out_len);
}

int pm_metal_runtime_run(pm_metal_runtime_handle_t h, int argc, char **argv)
{
	return pm_metal_runtime_run_ex(h, argc, argv, 0, NULL, -1, -1, -1, NULL);
}

int pm_metal_runtime_run_ex(pm_metal_runtime_handle_t h, int argc, char **argv, int envc, const char **envp,
			     int64_t stdin_fd, int64_t stdout_fd, int64_t stderr_fd,
			     pm_metal_runtime_exec_t *exec_out)
{
	if (!g_pm_metal_runtime.initialized) {
		return -1;
	}

	/* Worker threads this codebase spawns to call run()/run_ex()
	 * concurrently (e.g. runtime/process.c's spawn(), one new worker
	 * thread per process) are plain pthreads WAMR knows nothing
	 * about — unlike the thread that called wasm_runtime_full_init()
	 * (which gets this set up for free as part of that call), each of
	 * these needs its own thread signal/guard-page env set up before any
	 * instantiate()/execute_main() call below, and torn down after —
	 * that is exactly what wasm_runtime_{init,destroy}_thread_env() are
	 * for (see wasm_export.h's own doc comment on them). A no-op on a
	 * thread that already has one (the thread that called
	 * wasm_runtime_full_init(), or *any* thread on a build with hardware
	 * bounds checking compiled out, e.g. -DWAMR_DISABLE_HW_BOUND_CHECK=1
	 * — see scripts/verify-linux-threads.sh) — wasm_runtime_thread_env_
	 * inited() says so up front, so this only does the real work where
	 * it is actually needed. */
	int owns_thread_env = !wasm_runtime_thread_env_inited();

	if (owns_thread_env && !wasm_runtime_init_thread_env()) {
		fprintf(stderr, "pm_metal_runtime: thread env init failed\n");
		return -1;
	}

	/* Bind this worker's guest argv/env for /proc/self before any WASI
	 * file op; virtual proc answers on open from hooks (no refresh). */
	pm_metal_mount_proc_bind_current(argc, argv, envc, envp);

	/* Built fresh from the mount table every call — cheap (table is
	 * PM_METAL_MOUNT_MAX entries, no allocation) and means a mount
	 * added since the *last* run_ex() on some other handle is picked up
	 * by the very next one, same "only new processes see new mounts"
	 * rule as any other WASI preopen limitation (see docs/MOUNT.md "One
	 * hard limitation"). map_dir_bufs must outlive set_wasi_args_ex()
	 * below — it does, since instantiate() (the only thing that reads
	 * wasi_args->map_dir_list) runs later in this same stack frame,
	 * same lifetime WAMR already assumed of the old single-entry
	 * pointer this replaces. */
	char map_dir_bufs[PM_METAL_MOUNT_MAX][PM_METAL_MOUNT_MAP_DIR_ENTRY_MAX];
	const char *map_dir_list[PM_METAL_MOUNT_MAX];
	size_t map_dir_count = 0;
	size_t map_dir_i;

	if (pm_metal_mount_build_map_dir_list(map_dir_bufs, PM_METAL_MOUNT_MAX, &map_dir_count) != 0) {
		fprintf(stderr, "pm_metal_runtime: map_dir_list build failed\n");
		pm_metal_mount_proc_unbind_current();
		if (owns_thread_env) {
			wasm_runtime_destroy_thread_env();
		}
		return -1;
	}
	for (map_dir_i = 0; map_dir_i < map_dir_count; map_dir_i++) {
		map_dir_list[map_dir_i] = map_dir_bufs[map_dir_i];
	}

	/* set_wasi_args() + instantiate() locked: set_wasi_args() writes
	 * argv/map_dir onto slot->module itself (not a future instance), and
	 * instantiate() is what consumes it, so those two must stay paired
	 * with no other run() on this handle sneaking its own
	 * set_wasi_args() in between — plus instantiate() itself needs the
	 * lock regardless, see file header re: WAMR's shared pool allocator.
	 * The lock is released before the actual wasm execution so other
	 * handles keep running fully in parallel; re-taken only for
	 * deinstantiate() (same allocator concern), not for execute_main(). */
	pm_metal_port_mutex_lock(&g_pm_metal_runtime_lock);
	pm_metal_runtime_slot_t *slot = pm_metal_runtime_slot_for(h);
	if (!slot) {
		pm_metal_port_mutex_unlock(&g_pm_metal_runtime_lock);
		fprintf(stderr, "pm_metal_runtime: bad handle\n");
		pm_metal_mount_proc_unbind_current();
		if (owns_thread_env) {
			wasm_runtime_destroy_thread_env();
		}
		return -1;
	}
	slot->refcount++;

	/* -1 means "inherit the host's own fd" (WAMR's own sentinel, see
	 * os_convert_std{in,out,err}_handle() in WAMR's posix platform) —
	 * i.e. today's one-shared-console behavior. A caller wanting a
	 * private console per handle (see docs/RUNTIME.md "Console model")
	 * passes real fds here instead (e.g. a pipe() it owns and reads
	 * from); nothing else in this function needs to change for that. */
	wasm_runtime_set_wasi_args_ex(slot->module, NULL, 0, map_dir_list, (uint32_t)map_dir_count, envp,
				       (uint32_t)envc, argv, argc, stdin_fd, stdout_fd, stderr_fd);

	/* One fixed policy for every run_ex() call on every handle this
	 * whole init()/shutdown() cycle — set unconditionally (even when
	 * both counts are 0) so a handle can never end up keeping some
	 * *other* run_ex() call's addr_pool by accident (WAMR's own
	 * WASIArguments lives on the module, reused across calls; skipping
	 * this call on a 0-count config would just leave whatever was set
	 * last time). See runtime.h's own doc comment on cfg->addr_pool for
	 * what "0 entries" actually means (deny all, not allow all). */
	wasm_runtime_set_wasi_addr_pool(slot->module, (const char **)g_pm_metal_runtime.addr_pool,
					 g_pm_metal_runtime.addr_pool_count);
	wasm_runtime_set_wasi_ns_lookup_pool(slot->module, (const char **)g_pm_metal_runtime.ns_lookup_pool,
					      g_pm_metal_runtime.ns_lookup_pool_count);

	char error_buf[PM_METAL_RUNTIME_ERROR_BUF_SIZE];
	wasm_module_inst_t inst = wasm_runtime_instantiate(
		slot->module, PM_METAL_RUNTIME_STACK_SIZE,
		PM_METAL_RUNTIME_HEAP_SIZE, error_buf, sizeof(error_buf));
	/* Published while still holding the lock — see
	 * pm_metal_runtime_exec_t's own doc comment (runtime.h) for why a
	 * concurrent terminate() reading exec_out->inst can never race
	 * this specific write. */
	if (exec_out && inst) {
		exec_out->inst = inst;
	}
	pm_metal_port_mutex_unlock(&g_pm_metal_runtime_lock);

	int exit_code;
	if (!inst) {
		fprintf(stderr, "pm_metal_runtime: instantiate failed: %s\n",
			error_buf);
		exit_code = -1;
	} else {
		if (wasm_application_execute_main(inst, argc, argv)) {
			exit_code = (int)wasm_runtime_get_wasi_exit_code(inst);
		} else {
			const char *exception = wasm_runtime_get_exception(inst);

			fprintf(stderr, "pm_metal_runtime: %s\n",
				exception ? exception : "run failed");
			exit_code = -1;
		}
	}

	pm_metal_port_mutex_lock(&g_pm_metal_runtime_lock);
	/* Cleared before deinstantiate() actually frees anything — same
	 * lock as the publish above, so terminate() can never observe a
	 * stale, about-to-be-freed inst. */
	if (exec_out) {
		exec_out->inst = NULL;
	}
	if (inst) {
		wasm_runtime_deinstantiate(inst);
	}
	slot->refcount--;
	pm_metal_port_mutex_unlock(&g_pm_metal_runtime_lock);

	if (owns_thread_env) {
		wasm_runtime_destroy_thread_env();
	}
	pm_metal_mount_proc_unbind_current();
	return exit_code;
}

int pm_metal_runtime_exec_is_live(const pm_metal_runtime_exec_t *exec)
{
	int live = 0;

	if (!exec) {
		return 0;
	}
	pm_metal_port_mutex_lock(&g_pm_metal_runtime_lock);
	live = exec->inst != NULL ? 1 : 0;
	pm_metal_port_mutex_unlock(&g_pm_metal_runtime_lock);
	return live;
}

void pm_metal_runtime_terminate(pm_metal_runtime_exec_t *exec)
{
	wasm_module_inst_t inst = NULL;

	if (!exec) {
		return;
	}

	/* Snapshot under the runtime lock, then terminate outside it.
	 * Holding g_pm_metal_runtime_lock across wasm_runtime_terminate()
	 * can deadlock the worker on SMP targets: terminate() may need
	 * cluster/wait locks while the worker is trying to re-take this
	 * same runtime lock for deinstantiate() after noticing the flag
	 * (or while already holding a WAMR lock that terminate also
	 * needs). Instantiating concurrent deinstantiate() is still safe
	 * — run_ex() clears exec->inst under the same lock before freeing
	 * the instance, and kill only runs while the worker is mid-flight. */
	pm_metal_port_mutex_lock(&g_pm_metal_runtime_lock);
	inst = (wasm_module_inst_t)exec->inst;
	pm_metal_port_mutex_unlock(&g_pm_metal_runtime_lock);

	if (inst) {
		wasm_runtime_terminate(inst);
	}
}

int pm_metal_runtime_hold(pm_metal_runtime_handle_t h)
{
	if (!g_pm_metal_runtime.initialized) {
		return -1;
	}

	pm_metal_port_mutex_lock(&g_pm_metal_runtime_lock);
	pm_metal_runtime_slot_t *slot = pm_metal_runtime_slot_for(h);
	if (!slot) {
		pm_metal_port_mutex_unlock(&g_pm_metal_runtime_lock);
		fprintf(stderr, "pm_metal_runtime: bad handle\n");
		return -1;
	}
	slot->refcount++;
	pm_metal_port_mutex_unlock(&g_pm_metal_runtime_lock);
	return 0;
}

void pm_metal_runtime_release(pm_metal_runtime_handle_t h)
{
	if (!g_pm_metal_runtime.initialized) {
		return;
	}

	pm_metal_port_mutex_lock(&g_pm_metal_runtime_lock);
	pm_metal_runtime_slot_t *slot = pm_metal_runtime_slot_for(h);
	if (slot) {
		slot->refcount--;
	}
	pm_metal_port_mutex_unlock(&g_pm_metal_runtime_lock);
}

int pm_metal_runtime_unload(pm_metal_runtime_handle_t h)
{
	if (!g_pm_metal_runtime.initialized) {
		return -1;
	}

	pm_metal_port_mutex_lock(&g_pm_metal_runtime_lock);
	pm_metal_runtime_slot_t *slot = pm_metal_runtime_slot_for(h);
	if (!slot) {
		pm_metal_port_mutex_unlock(&g_pm_metal_runtime_lock);
		fprintf(stderr, "pm_metal_runtime: bad handle\n");
		return -1;
	}
	if (slot->refcount > 0) {
		pm_metal_port_mutex_unlock(&g_pm_metal_runtime_lock);
		/* Deliberate rejection, not a fault — see docs/RUNTIME.md
		 * "Concurrency". Caller raced unload() against an in-flight
		 * run() on the same handle; retry once run() returns. */
		fprintf(stderr,
			"pm_metal_runtime: unload refused (by design) — handle busy, run in flight\n");
		return -1;
	}

	/* wasm_runtime_unload() stays inside the lock — see file header re:
	 * WAMR's shared pool allocator. bytecode->free() runs after unlock:
	 * it only touches our own arena, which has its own separate lock
	 * around the free-list (see memory/bytecode.c), unrelated to WAMR's
	 * pool. */
	wasm_module_t module = slot->module;
	uint8_t *buf = slot->buf;
	wasm_runtime_unload(module);
	slot->used = 0;
	slot->module = NULL;
	slot->buf = NULL;
	slot->buf_len = 0;
	pm_metal_port_mutex_unlock(&g_pm_metal_runtime_lock);

	pm_metal_memory_bytecode_ops()->free(buf);

	return 0;
}

static int g_pm_metal_runtime_allow_guest_mount;

void pm_metal_runtime_set_allow_guest_mount(int allow)
{
	g_pm_metal_runtime_allow_guest_mount = allow ? 1 : 0;
}

int pm_metal_runtime_allow_guest_mount(void)
{
	return g_pm_metal_runtime_allow_guest_mount;
}
