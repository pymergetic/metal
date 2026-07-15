/*
 * Port — common implementations.
 */
#include "pymergetic/metal/port/platform.h"

#ifndef PM_METAL_PORT_TARGET
#define PM_METAL_PORT_TARGET PM_METAL_PORT_TARGET_UNKNOWN
#endif

pm_metal_port_target_id_t pm_metal_port_target_id(void)
{
	return (pm_metal_port_target_id_t)PM_METAL_PORT_TARGET;
}

const char *pm_metal_port_target_name(pm_metal_port_target_id_t id)
{
	switch (id) {
	case PM_METAL_PORT_TARGET_LINUX:
		return "linux";
	case PM_METAL_PORT_TARGET_ZEPHYR:
		return "zephyr";
	case PM_METAL_PORT_TARGET_RUMP:
		return "rump";
	case PM_METAL_PORT_TARGET_UNIKRAFT:
		return "unikraft";
	case PM_METAL_PORT_TARGET_NUTTX:
		return "nuttx";
	case PM_METAL_PORT_TARGET_UNKNOWN:
	default:
		return "unknown";
	}
}
