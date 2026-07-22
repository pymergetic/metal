/*
 * Metal UI — shared types / WASI module (guest/host dual ABI).
 *
 * Opaque handles only; widget trees stay host-private (priv.h).
 */
#ifndef PYMERGETIC_METAL_SHELL_UI_TYPES_H_
#define PYMERGETIC_METAL_SHELL_UI_TYPES_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t pm_metal_ui_handle_t;

#define PM_METAL_UI_HANDLE_INVALID 0u

#define PM_METAL_UI_WASI_MODULE "pymergetic.metal.ui"

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_UI_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_UI_WASI_MODULE, name)
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_SHELL_UI_TYPES_H_ */
