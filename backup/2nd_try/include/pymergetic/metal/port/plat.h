/*
 * Engine probe contract — no OS headers in this file.
 * Lower-only: firmware port bind. Mods use pm_metal_sys_* / hostinfo, not plat directly.
 *
 * Port (per host tree):
 *   host/linux/pymergetic/metal/port/plat.c  — Linux firmware
 *   host/zephyr/pymergetic/metal/port/plat.c — Zephyr firmware
 */
#ifndef PYMERGETIC_METAL_PORT_PLAT_H_
#define PYMERGETIC_METAL_PORT_PLAT_H_

#include <pymergetic/metal/export.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum pm_metal_port_target_id {
	PM_METAL_PORT_TARGET_LINUX = 1,
	PM_METAL_PORT_TARGET_ZEPHYR = 2,
	PM_METAL_PORT_TARGET_RUMP = 3,
	PM_METAL_PORT_TARGET_UNIKRAFT = 4,
} pm_metal_port_target_id_t;

/* Port: host/linux/pymergetic/metal/port/plat.c
 *       host/zephyr/pymergetic/metal/port/plat.c
 */
PM_METAL_KERNEL_API(pm_metal_port_target_id_t, pm_metal_port_target_id, (void));

/* Port: host/linux/pymergetic/metal/port/plat.c
 *       host/zephyr/pymergetic/metal/port/plat.c
 */
PM_METAL_KERNEL_API(uint64_t, pm_metal_port_machine_ram, (void));

/* Port: host/linux/pymergetic/metal/port/plat.c
 *       host/zephyr/pymergetic/metal/port/plat.c
 */
PM_METAL_KERNEL_API(uint64_t, pm_metal_port_link_used, (void));

/* Port: host/linux/pymergetic/metal/port/plat.c
 *       host/zephyr/pymergetic/metal/port/plat.c
 */
PM_METAL_KERNEL_API(uint64_t, pm_metal_port_arena_budget, (void));

/*
 * Establish WAMR runtime pool sized from arena_budget (idempotent).
 * Port: host/linux/pymergetic/metal/port/plat.c
 *       host/zephyr/pymergetic/metal/port/plat.c
 */
PM_METAL_KERNEL_API(int, pm_metal_port_wamr_pool_establish,
		    (uint8_t **out_buf, size_t *out_size));

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_PORT_PLAT_H_ */
