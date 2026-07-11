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

/* impl: common */
pm_metal_port_target_id_t pm_metal_port_target_id(void);

/* impl: bind */
uint64_t pm_metal_port_machine_ram(void);

#endif /* PYMERGETIC_METAL_PORT_PLATFORM_H_ */
