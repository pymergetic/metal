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

/* impl: bind — src/linux/pymergetic/metal/port/platform.c
 *              src/zephyr/pymergetic/metal/port/platform.c
 *
 * Quiet "does a regular file exist at host_path" check — no read, no
 * error logging on a miss (unlike read_file() above, which is meant to
 * be called for a path the caller already expects to exist and logs
 * accordingly). Exists specifically for shell/shell.c's wasm-override
 * probe (see there): trying every bare command name against
 * "/bin/<name>.wasm" is expected to usually miss, so that path must stay
 * silent — this is what makes it safe to call in that hot path without
 * spamming every ordinary native command with a bogus "read failed".
 * Returns 1/0 (exists-and-is-a-regular-file / does not), never -1 — a
 * missing file is not an error here, just a "no". */
int pm_metal_port_file_exists(const char *host_path);

#endif /* PYMERGETIC_METAL_PORT_PLATFORM_H_ */
