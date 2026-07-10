/*
 * Engine probe contract — no OS headers in this file.
 */
#ifndef PYMERGETIC_METAL_PORT_PLAT_H_
#define PYMERGETIC_METAL_PORT_PLAT_H_

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

pm_metal_port_target_id_t pm_metal_port_target_id(void);

/* Installed machine RAM (probe) — not link-window DT cap on dynamic x86. */
uint64_t pm_metal_port_machine_ram(void);

/* Bytes from RAM base through kernel/host image tail (link used). */
uint64_t pm_metal_port_link_used(void);

/* Orchestrator arena budget after link reservation. */
uint64_t pm_metal_port_arena_budget(void);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_PORT_PLAT_H_ */
