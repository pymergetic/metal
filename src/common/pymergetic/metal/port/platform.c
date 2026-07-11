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
