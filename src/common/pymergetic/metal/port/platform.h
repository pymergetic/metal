/*
 * Port contract — OS floor API for the runtime binary. Memory (ram probe,
 * WAMR kheap pool, bytecode arena) lives in pymergetic/metal/memory/ —
 * this file is everything else. See docs/SOURCETREE.md.
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
 *              src/zephyr/pymergetic/metal/port/platform.c
 *
 * Read an entire file at host_path into a buffer obtained from
 * pm_metal_memory_bytecode_ops()->alloc() — the caller frees it with
 * ->free(), never plain free(). host_path is already vfs_root-resolved by
 * runtime.c — this never sees a guest-style path.
 * Deliberately not a stdio passthrough: linux uses fopen/fread; zephyr
 * (later) backs this with fs_open()/fs_read() (CONFIG_FILE_SYSTEM) since
 * bare libc stdio is not guaranteed to reach mounted storage on embedded
 * targets. Returns 0 and fills *out_buf and *out_len on success (nothing
 * allocated on failure), -1 on failure. */
int pm_metal_port_read_file(const char *host_path, uint8_t **out_buf, uint32_t *out_len);

#endif /* PYMERGETIC_METAL_PORT_PLATFORM_H_ */
