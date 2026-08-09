/*
 * C into-Py bridge for frozen pymergetic.metal.arch.wasm.
 */
#include "pymergetic/metal/arch/wasm/__init__.h"

#define SEAT_MOD "pymergetic.metal.arch.wasm"
#define SEAT_PREFIX pm_metal_arch_wasm
#define SEAT_HAS_FIRMWARE 1
#include "../seat_bridge.inc.c"
