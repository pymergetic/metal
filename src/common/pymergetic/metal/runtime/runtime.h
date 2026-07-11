/*
 * Runtime contract — long-lived dynamic loader.
 * See docs/RUNTIME.md.
 */
#ifndef PYMERGETIC_METAL_RUNTIME_RUNTIME_H_
#define PYMERGETIC_METAL_RUNTIME_RUNTIME_H_

#include <stdint.h>

typedef struct pm_metal_runtime_config {
	uint64_t memory_bytes;
	const char *vfs_root;
} pm_metal_runtime_config_t;

typedef struct pm_metal_runtime_handle {
	uint32_t id;
} pm_metal_runtime_handle_t;

/* impl: common — lifecycle */
int pm_metal_runtime_init(const pm_metal_runtime_config_t *cfg);
int pm_metal_runtime_shutdown(void);

/* impl: common — dynamic loader */
int pm_metal_runtime_load_file(const char *path, pm_metal_runtime_handle_t *out);
int pm_metal_runtime_load_bytes(const uint8_t *wasm, uint32_t len,
				pm_metal_runtime_handle_t *out);
int pm_metal_runtime_run(pm_metal_runtime_handle_t h, int argc, char **argv);
int pm_metal_runtime_unload(pm_metal_runtime_handle_t h);

#endif /* PYMERGETIC_METAL_RUNTIME_RUNTIME_H_ */
