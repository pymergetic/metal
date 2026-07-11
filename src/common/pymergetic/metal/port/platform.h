/*
 * Port contract — OS floor API for the runtime binary.
 * See docs/SOURCETREE.md.
 */
#ifndef PYMERGETIC_METAL_PORT_PLATFORM_H_
#define PYMERGETIC_METAL_PORT_PLATFORM_H_

#include <stdint.h>

typedef enum pm_metal_port_target_id {
	PM_METAL_PORT_TARGET_UNKNOWN = 0,
	PM_METAL_PORT_TARGET_LINUX,
	PM_METAL_PORT_TARGET_ZEPHYR,
	PM_METAL_PORT_TARGET_RUMP,
	PM_METAL_PORT_TARGET_UNIKRAFT,
} pm_metal_port_target_id_t;

/* impl: common — src/common/pymergetic/metal/port/platform.c */
pm_metal_port_target_id_t pm_metal_port_target_id(void);

/* impl: bind — src/linux/pymergetic/metal/port/platform.c
 *              src/zephyr/pymergetic/metal/port/platform.c */
uint64_t pm_metal_port_machine_ram(void);

/* impl: bind — src/linux/pymergetic/metal/port/platform.c
 *              src/zephyr/pymergetic/metal/port/platform.c
 *
 * Establish the WAMR memory pool for this target, from a requested size.
 * One pool per process (runtime.c enforces one init()/shutdown() pair).
 * linux: malloc(requested_bytes) verbatim — memory_bytes is the sole
 * source, no probe. zephyr (later): probe pm_metal_port_machine_ram(),
 * subtract kernel static + heap, establish from the remainder —
 * requested_bytes may be capped against that. See docs/RUNTIME.md.
 * Returns the pool pointer (also written to *out_bytes), NULL on failure. */
void *pm_metal_port_wamr_pool_establish(uint64_t requested_bytes, uint64_t *out_bytes);

/* impl: bind — src/linux/pymergetic/metal/port/platform.c
 *              src/zephyr/pymergetic/metal/port/platform.c
 *
 * Release the pool obtained from wamr_pool_establish(). */
void pm_metal_port_wamr_pool_release(void);

/* impl: bind — src/linux/pymergetic/metal/port/platform.c
 *              src/zephyr/pymergetic/metal/port/platform.c
 *
 * Bytes of the pool currently established, 0 if none. */
uint64_t pm_metal_port_wamr_pool_bytes(void);

/* impl: bind — src/linux/pymergetic/metal/port/platform.c
 *              src/zephyr/pymergetic/metal/port/platform.c
 *
 * Read an entire file at host_path into a malloc()'d buffer the caller
 * frees with free(). host_path is already vfs_root-resolved by runtime.c —
 * this never sees a guest-style path. Deliberately not a stdio passthrough:
 * linux uses fopen/fread; zephyr (later) backs this with fs_open()/fs_read()
 * (CONFIG_FILE_SYSTEM) since bare libc stdio is not guaranteed to reach
 * mounted storage on embedded targets. Returns 0 and fills *out_buf and
 * *out_len on success (nothing allocated on failure), -1 on failure. */
int pm_metal_port_read_file(const char *host_path, uint8_t **out_buf, uint32_t *out_len);

#endif /* PYMERGETIC_METAL_PORT_PLATFORM_H_ */
