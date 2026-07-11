/*
 * Lower bind — load wasm mod instances (WAMR / wasmtime).
 */
#ifndef PYMERGETIC_METAL_ORCHESTRATOR_WASM_HOST_H_
#define PYMERGETIC_METAL_ORCHESTRATOR_WASM_HOST_H_

#include <pymergetic/metal/export.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* host_vfs_root: host tree WASI-preopened as guest / (pass "/" on target).
 * Bootstrap handoff: PM_METAL_SYS_HANDOFF_VFS_ROOT (/sys/pm) on both sides.
 * Defined: host/zephyr/pymergetic/metal/orchestrator/wasm_host.c
 */
PM_METAL_KERNEL_API(int, pm_metal_orchestrator_wasm_host_run, (const char *host_vfs_root,
							      const char *wasm_path));
PM_METAL_KERNEL_API(int, pm_metal_orchestrator_wasm_host_run_bytes,
		    (const uint8_t *wasm_buf, uint32_t wasm_len));

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_ORCHESTRATOR_WASM_HOST_H_ */
