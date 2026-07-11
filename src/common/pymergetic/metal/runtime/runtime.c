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
 * against vfs_root — the exact same tree WASI preopens as "/" for the guest.
 * There is no separate "host path" for mod bytecode: the loader and the
 * guest read from the same storage, so this keeps working unchanged when
 * vfs_root is a mounted ramdisk/littlefs on zephyr instead of a host dir.
 * Path resolution (string concat) stays here — OS-independent — but the
 * actual bytes-from-storage read is a port call (pm_metal_port_read_file):
 * bare libc stdio is not guaranteed to reach mounted storage on embedded
 * targets, so this file must never call fopen()/fread() etc. directly.
 */
#include "pymergetic/metal/runtime/runtime.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "pymergetic/metal/memory/memory.h"
#include "pymergetic/metal/port/platform.h"
#include "wasm_export.h"

#define PM_METAL_RUNTIME_MAX_HANDLES 8
#define PM_METAL_RUNTIME_MAX_PATH 256 /* resolved vfs path buffer — not PATH_MAX (POSIX-only, and too large for small stacks) */
#define PM_METAL_RUNTIME_STACK_SIZE (64 * 1024)
#define PM_METAL_RUNTIME_HEAP_SIZE 0 /* wasi-libc manages its own heap in linear memory */
#define PM_METAL_RUNTIME_ERROR_BUF_SIZE 128

typedef struct pm_metal_runtime_slot {
	int used;
	wasm_module_t module;
	uint8_t *buf;
	uint32_t buf_len;
} pm_metal_runtime_slot_t;

static struct {
	int initialized;
	char *vfs_root;
	char *map_dir_entry; /* "/::<vfs_root>", built once at init */
	pm_metal_runtime_slot_t slots[PM_METAL_RUNTIME_MAX_HANDLES];
} g_pm_metal_runtime;

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
	char error_buf[PM_METAL_RUNTIME_ERROR_BUF_SIZE];
	pm_metal_runtime_slot_t *slot = NULL;
	uint32_t i;

	for (i = 0; i < PM_METAL_RUNTIME_MAX_HANDLES; i++) {
		if (!g_pm_metal_runtime.slots[i].used) {
			slot = &g_pm_metal_runtime.slots[i];
			break;
		}
	}
	if (!slot) {
		fprintf(stderr, "pm_metal_runtime: handle table full\n");
		return -1;
	}

	wasm_module_t module =
		wasm_runtime_load(buf, len, error_buf, sizeof(error_buf));
	if (!module) {
		fprintf(stderr, "pm_metal_runtime: load failed: %s\n", error_buf);
		return -1;
	}

	slot->used = 1;
	slot->module = module;
	slot->buf = buf;
	slot->buf_len = len;

	out->id = (uint32_t)(i + 1);
	return 0;
}

/* load_file() paths are guest-style, resolved against vfs_root exactly like
 * WASI resolves a guest open() — never an arbitrary host path. Same rule as
 * the map_dir_list preopen, so the loader keeps working unchanged when
 * vfs_root is a mounted ramdisk/littlefs on zephyr instead of a host dir. */
static int pm_metal_runtime_resolve_vfs_path(const char *guest_path, char *out, size_t out_len)
{
	int n = snprintf(out, out_len, "%s%s%s", g_pm_metal_runtime.vfs_root,
			  guest_path[0] == '/' ? "" : "/", guest_path);

	return (n > 0 && (size_t)n < out_len) ? 0 : -1;
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
	    || cfg->bytecode_bytes > UINT32_MAX) {
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

	/* strdup() is a POSIX/BSD extension, not ISO C — not guaranteed on
	 * every target's libc, so copy by hand instead. */
	size_t vfs_root_len = strlen(cfg->vfs_root) + 1;
	char *vfs_root = malloc(vfs_root_len);
	if (!vfs_root) {
		bytecode->release();
		kheap->release();
		return -1;
	}
	memcpy(vfs_root, cfg->vfs_root, vfs_root_len);

	size_t map_dir_len = strlen("/::") + strlen(vfs_root) + 1;
	char *map_dir_entry = malloc(map_dir_len);
	if (!map_dir_entry) {
		free(vfs_root);
		bytecode->release();
		kheap->release();
		return -1;
	}
	snprintf(map_dir_entry, map_dir_len, "/::%s", vfs_root);

	RuntimeInitArgs init_args;
	memset(&init_args, 0, sizeof(init_args));
	init_args.mem_alloc_type = Alloc_With_Pool;
	init_args.mem_alloc_option.pool.heap_buf = pool;
	init_args.mem_alloc_option.pool.heap_size = (uint32_t)pool_bytes;

	if (!wasm_runtime_full_init(&init_args)) {
		fprintf(stderr, "pm_metal_runtime: wasm_runtime_full_init failed\n");
		free(map_dir_entry);
		free(vfs_root);
		bytecode->release();
		kheap->release();
		return -1;
	}

	memset(g_pm_metal_runtime.slots, 0, sizeof(g_pm_metal_runtime.slots));
	g_pm_metal_runtime.vfs_root = vfs_root;
	g_pm_metal_runtime.map_dir_entry = map_dir_entry;
	g_pm_metal_runtime.initialized = 1;

	return 0;
}

int pm_metal_runtime_shutdown(void)
{
	if (!g_pm_metal_runtime.initialized) {
		return -1;
	}

	uint32_t i;

	for (i = 0; i < PM_METAL_RUNTIME_MAX_HANDLES; i++) {
		if (g_pm_metal_runtime.slots[i].used) {
			pm_metal_runtime_unload_slot(&g_pm_metal_runtime.slots[i]);
		}
	}

	wasm_runtime_destroy();

	free(g_pm_metal_runtime.map_dir_entry);
	free(g_pm_metal_runtime.vfs_root);
	pm_metal_memory_bytecode_ops()->release();
	pm_metal_memory_kheap_ops()->release();

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
		fprintf(stderr, "pm_metal_runtime: read failed: %s (vfs_root=%s)\n",
			path, g_pm_metal_runtime.vfs_root);
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

int pm_metal_runtime_run(pm_metal_runtime_handle_t h, int argc, char **argv)
{
	if (!g_pm_metal_runtime.initialized) {
		return -1;
	}

	pm_metal_runtime_slot_t *slot = pm_metal_runtime_slot_for(h);
	if (!slot) {
		fprintf(stderr, "pm_metal_runtime: bad handle\n");
		return -1;
	}

	const char *map_dir_list[1];
	map_dir_list[0] = g_pm_metal_runtime.map_dir_entry;

	wasm_runtime_set_wasi_args(slot->module, NULL, 0, map_dir_list, 1, NULL, 0,
				    argv, argc);

	char error_buf[PM_METAL_RUNTIME_ERROR_BUF_SIZE];
	wasm_module_inst_t inst = wasm_runtime_instantiate(
		slot->module, PM_METAL_RUNTIME_STACK_SIZE,
		PM_METAL_RUNTIME_HEAP_SIZE, error_buf, sizeof(error_buf));
	if (!inst) {
		fprintf(stderr, "pm_metal_runtime: instantiate failed: %s\n",
			error_buf);
		return -1;
	}

	int exit_code;
	if (wasm_application_execute_main(inst, argc, argv)) {
		exit_code = (int)wasm_runtime_get_wasi_exit_code(inst);
	} else {
		const char *exception = wasm_runtime_get_exception(inst);
		fprintf(stderr, "pm_metal_runtime: %s\n",
			exception ? exception : "run failed");
		exit_code = -1;
	}

	wasm_runtime_deinstantiate(inst);

	return exit_code;
}

int pm_metal_runtime_unload(pm_metal_runtime_handle_t h)
{
	if (!g_pm_metal_runtime.initialized) {
		return -1;
	}

	pm_metal_runtime_slot_t *slot = pm_metal_runtime_slot_for(h);
	if (!slot) {
		fprintf(stderr, "pm_metal_runtime: bad handle\n");
		return -1;
	}

	pm_metal_runtime_unload_slot(slot);

	return 0;
}
