/*
 * C into-Py bridge for frozen pymergetic.metal.arch.x86.
 */
#include "pymergetic/metal/arch/x86/__init__.h"

#define SEAT_MOD "pymergetic.metal.arch.x86"
#define SEAT_PREFIX pm_metal_arch_x86
#define SEAT_HAS_FIRMWARE 1
#include "../seat_bridge.inc.c"
