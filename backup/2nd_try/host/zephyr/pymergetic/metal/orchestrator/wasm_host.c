/*
 * SPDX-License-Identifier: Apache-2.0
 * WAMR lower bind — load a wasm mod from the host VFS.
 */
#include <pymergetic/metal/orchestrator/wasm_host.h>
#include <pymergetic/metal/port/plat.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "wasm_export.h"

#include <zephyr/fs/fs.h>
#include <zephyr/sys/printk.h>

#define PM_WAMR_APP_STACK_SIZE 32768
#define PM_WAMR_APP_HEAP_SIZE 32768
#define PM_WASM_LOAD_BUF_BYTES 262144

static uint8_t g_wasm_load_buf[PM_WASM_LOAD_BUF_BYTES];

static int pm_metal_wasm_host_read_file(const char *path, uint8_t **out_buf, uint32_t *out_len)
{
	struct fs_file_t file;
	struct fs_dirent entry;
	uint8_t *buf;
	ssize_t nread;
	int rc;

	if (path == NULL || out_buf == NULL || out_len == NULL) {
		return -1;
	}

	rc = fs_stat(path, &entry);
	if (rc != 0 || entry.size <= 0) {
		return -1;
	}

	buf = (uint8_t *)malloc((size_t)entry.size);
	if (buf == NULL) {
		return -1;
	}

	fs_file_t_init(&file);
	rc = fs_open(&file, path, FS_O_READ);
	if (rc != 0) {
		free(buf);
		return -1;
	}

	nread = fs_read(&file, buf, entry.size);
	rc = fs_close(&file);
	if (nread != entry.size || rc != 0) {
		free(buf);
		return -1;
	}

	*out_buf = buf;
	*out_len = (uint32_t)entry.size;
	return 0;
}

static int pm_metal_wasm_host_execute(const uint8_t *wasm_buf, uint32_t wasm_len, int free_buf)
{
	wasm_module_t wasm_module = NULL;
	wasm_module_inst_t wasm_module_inst = NULL;
	RuntimeInitArgs init_args;
	char error_buf[128];
	const char *exception;
	const char *dir_list[1];
	uint8_t *wamr_pool = NULL;
	size_t wamr_pool_size = 0;
	int rc = 1;

	if (wasm_buf == NULL || wasm_len == 0U) {
		return -1;
	}

	if (pm_metal_port_wamr_pool_establish(&wamr_pool, &wamr_pool_size) != 0) {
		return -1;
	}

	memset(&init_args, 0, sizeof(init_args));
	init_args.mem_alloc_type = Alloc_With_Pool;
	init_args.mem_alloc_option.pool.heap_buf = wamr_pool;
	init_args.mem_alloc_option.pool.heap_size = wamr_pool_size;

	if (!wasm_runtime_full_init(&init_args)) {
		printk("wasm_runtime_full_init failed\n");
		return -1;
	}

	if (!(wasm_module = wasm_runtime_load(wasm_buf, wasm_len, error_buf, sizeof(error_buf)))) {
		printk("wasm_runtime_load: %s\n", error_buf);
		goto done;
	}

#if WASM_ENABLE_LIBC_WASI != 0
	dir_list[0] = "/";
	wasm_runtime_set_wasi_args(wasm_module, dir_list, 1, NULL, 0, NULL, 0, NULL, 0);
#endif

	if (!(wasm_module_inst = wasm_runtime_instantiate(
		      wasm_module, PM_WAMR_APP_STACK_SIZE, PM_WAMR_APP_HEAP_SIZE, error_buf,
		      sizeof(error_buf)))) {
		printk("wasm_runtime_instantiate: %s\n", error_buf);
		goto unload;
	}

	if (wasm_runtime_lookup_function(wasm_module_inst, "_start")
	    || wasm_runtime_lookup_function(wasm_module_inst, "__main_argc_argv")
	    || wasm_runtime_lookup_function(wasm_module_inst, "main")) {
		wasm_application_execute_main(wasm_module_inst, 0, NULL);
	} else {
		printk("wasm mod entry not found\n");
		goto deinstantiate;
	}

	if ((exception = wasm_runtime_get_exception(wasm_module_inst)) != NULL) {
		printk("wasm exception: %s\n", exception);
		goto deinstantiate;
	}

	rc = wasm_runtime_get_wasi_exit_code(wasm_module_inst);
	printk("wasm mod exit code: %d\n", rc);

deinstantiate:
	wasm_runtime_deinstantiate(wasm_module_inst);
unload:
	wasm_runtime_unload(wasm_module);
done:
	wasm_runtime_destroy();
	if (free_buf) {
		free((void *)wasm_buf);
	}
	return rc;
}

int pm_metal_orchestrator_wasm_host_run(const char *host_vfs_root, const char *wasm_path)
{
	uint8_t *wasm_buf = NULL;
	uint32_t wasm_len = 0;
	int rc;

	(void)host_vfs_root;

	if (wasm_path == NULL || wasm_path[0] == '\0') {
		return -1;
	}

	if (pm_metal_wasm_host_read_file(wasm_path, &wasm_buf, &wasm_len) != 0) {
		printk("wasm_host: read %s failed\n", wasm_path);
		return -1;
	}

	rc = pm_metal_wasm_host_execute(wasm_buf, wasm_len, 1);
	return rc;
}

int pm_metal_orchestrator_wasm_host_run_bytes(const uint8_t *wasm_buf, uint32_t wasm_len)
{
	if (wasm_buf == NULL || wasm_len == 0U || wasm_len > PM_WASM_LOAD_BUF_BYTES) {
		return -1;
	}

	memcpy(g_wasm_load_buf, wasm_buf, wasm_len);
	return pm_metal_wasm_host_execute(g_wasm_load_buf, wasm_len, 0);
}
