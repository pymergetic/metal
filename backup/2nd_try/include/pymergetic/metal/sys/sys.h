/*
 * Bootstrap exchange: lower encodes; upper binds in-process; wasm mods read /sys/pm.
 *
 * Lower implementations (symmetric filenames per host tree):
 *   host/linux/pymergetic/metal/sys/sys.c   — Linux firmware
 *   host/zephyr/pymergetic/metal/sys/sys.c  — Zephyr firmware
 *   src/pymergetic/metal/sys/sys.c          — upper bind + wasm mod getters
 */
#ifndef PYMERGETIC_METAL_SYS_H_
#define PYMERGETIC_METAL_SYS_H_

#include <pymergetic/metal/util/bigtag.h>
#include <pymergetic/metal/export.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Handoff mount — same path on host and guest when the host tree is preopened as /. */
#define PM_METAL_SYS_HANDOFF_VFS_ROOT "/sys/pm"
#define PM_METAL_SYS_BOOTSTRAP_PATH PM_METAL_SYS_HANDOFF_VFS_ROOT "/bootstrap"

#define PM_METAL_SYS_MAGIC PM_METAL_UTIL_EIGHTCC_LE('P', 'M', 'S', 'Y', 'B', 'O', 'O', 'T')
#define PM_METAL_SYS_VERSION PM_METAL_UTIL_VERSION_MAKE(1, 0, 0)

/* On-wire fields are LE — see pymergetic/metal/util/endian.h. */
typedef struct pm_metal_sys_bootstrap {
	pm_metal_util_bigtag_t tag;
	uint32_t size;
	uint32_t _reserved;
	uint64_t machine_ram;
	uint64_t arena_budget;
	uint64_t link_used;
} pm_metal_sys_bootstrap_t;

#define PM_METAL_SYS_BLOB_SIZE ((uint32_t)sizeof(pm_metal_sys_bootstrap_t))

/* Wasm mod getters — after pm_metal_sys_init().
 * Defined: src/pymergetic/metal/sys/sys.c
 */
PM_METAL_API(int, pm_metal_sys_ready, (void));
PM_METAL_API(uint64_t, pm_metal_sys_built, (void));
PM_METAL_API(uint64_t, pm_metal_sys_machine_ram, (void));
PM_METAL_API(uint64_t, pm_metal_sys_arena_budget, (void));
PM_METAL_API(uint64_t, pm_metal_sys_link_used, (void));

/* Wasm mod — hostinfo_load once, then cached getters.
 * Defined: src/pymergetic/metal/sys/sys.c
 */
PM_METAL_API(int, pm_metal_sys_init, (void));

/* Upper — in-process handoff from lower at boot.
 * Defined: src/pymergetic/metal/sys/sys.c
 */
PM_METAL_KERNEL_API(int, pm_metal_sys_bootstrap_bind,
		    (const pm_metal_sys_bootstrap_t *blob));

/* Lower — encode probe record.
 * Defined: host/linux/pymergetic/metal/sys/sys.c
 *          host/zephyr/pymergetic/metal/sys/sys.c
 */
PM_METAL_KERNEL_API(int, pm_metal_sys_bootstrap_encode, (pm_metal_sys_bootstrap_t *out));

/* Shared validate — upper bind and wasm mod load.
 * Defined: src/pymergetic/metal/sys/sys.c
 */
PM_METAL_API(int, pm_metal_sys_bootstrap_validate, (const pm_metal_sys_bootstrap_t *blob));

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_SYS_H_ */
