#ifndef PYMERGETIC_METAL_PORT_FIRMWARE_UPY_H
#define PYMERGETIC_METAL_PORT_FIRMWARE_UPY_H

#include "pymergetic/util/mem.h"

void pm_metal_firmware_bind_arena(pm_util_mem_arena_t *arena);
int pm_metal_firmware_upy(void);

#endif
