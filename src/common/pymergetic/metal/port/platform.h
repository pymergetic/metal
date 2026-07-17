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
	PM_METAL_PORT_TARGET_NUTTX,
} pm_metal_port_target_id_t;

/* impl: common — src/common/pymergetic/metal/port/platform.c */
pm_metal_port_target_id_t pm_metal_port_target_id(void);

/* Stable lowercase name for id ("linux", "zephyr", …). "unknown" if
 * out of range. impl: common — src/common/pymergetic/metal/port/platform.c */
const char *pm_metal_port_target_name(pm_metal_port_target_id_t id);

/* impl: bind — src/linux/pymergetic/metal/port/platform.c
 *              src/zephyr/pymergetic/metal/port/platform.c
 *              src/nuttx/pymergetic/metal/port/platform.c
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
 *              src/nuttx/pymergetic/metal/port/platform.c
 *
 * Quiet "does a regular file exist at host_path" check — no read, no
 * error logging on a miss (unlike read_file() above, which is meant to
 * be called for a path the caller already expects to exist and logs
 * accordingly) — for a caller that expects misses to be common/expected
 * and needs that path to stay silent. Returns 1/0 (exists-and-is-a-
 * regular-file / does not), never -1 — a missing file is not an error
 * here, just a "no". */
int pm_metal_port_file_exists(const char *host_path);

/* impl: bind — src/linux/pymergetic/metal/port/platform.c
 *              src/zephyr/pymergetic/metal/port/platform.c
 *              src/nuttx/pymergetic/metal/port/platform.c
 *
 * Write len bytes at data to host_path (create/truncate). Parent
 * directories must already exist (callers that need mkdir -p use
 * mkdir() below first). Returns 0/-1. */
int pm_metal_port_write_file(const char *host_path, const uint8_t *data, uint32_t len);

/* impl: bind — src/linux/pymergetic/metal/port/platform.c
 *              src/zephyr/pymergetic/metal/port/platform.c
 *              src/nuttx/pymergetic/metal/port/platform.c
 *
 * Create directory at host_path, including any missing parents
 * (mkdir -p). Existing directory is success (0), not an error.
 * Returns 0/-1. */
int pm_metal_port_mkdir(const char *host_path);

/* impl: bind — src/linux/pymergetic/metal/port/platform.c
 *              src/zephyr/pymergetic/metal/port/platform.c
 *              src/nuttx/pymergetic/metal/port/platform.c
 *
 * Monotonic milliseconds since an arbitrary epoch (boot / process
 * start). Used for Metal /proc/uptime — not wall-clock time. */
uint64_t pm_metal_port_monotonic_ms(void);

#endif /* PYMERGETIC_METAL_PORT_PLATFORM_H_ */
