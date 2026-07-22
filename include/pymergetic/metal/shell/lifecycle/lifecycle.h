/*
 * Metal surface/app lifecycle — focus + visibility (guest/host dual ABI).
 * See docs/IO.md. Blur always unlocks pointer.
 *
 * impl: common — src/pymergetic/metal/shell/lifecycle/lifecycle.c
 */
#ifndef PYMERGETIC_METAL_SHELL_LIFECYCLE_LIFECYCLE_H_
#define PYMERGETIC_METAL_SHELL_LIFECYCLE_LIFECYCLE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_LIFECYCLE_WASI_MODULE "pymergetic.metal.lifecycle"

#define PM_METAL_LIFE_FOCUSED   1u
#define PM_METAL_LIFE_VISIBLE   2u
#define PM_METAL_LIFE_CLOSING   4u

typedef struct {
	uint32_t surface; /* gfx surface handle; 0 = none / DEFAULT clear */
	uint32_t flags;
} pm_metal_lifecycle_event_t;

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_LIFECYCLE_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_LIFECYCLE_WASI_MODULE, name)

/** Pop lifecycle event into guest struct at dest; 1=ok, 0=empty. */
extern int32_t pm_metal_lifecycle_poll(uint32_t dest)
	PM_METAL_LIFECYCLE_IMPORT(pm_metal_lifecycle_poll);
/** 1 if surface currently focused. */
extern int32_t pm_metal_lifecycle_focused(uint32_t surface)
	PM_METAL_LIFECYCLE_IMPORT(pm_metal_lifecycle_focused);
#else
int32_t pm_metal_lifecycle_poll(pm_metal_lifecycle_event_t *out);
int32_t pm_metal_lifecycle_focused(uint32_t surface);

/** Host: set focus/visibility; blur unlocks pointer. */
void pm_metal_lifecycle_set(uint32_t surface, uint32_t flags);
void pm_metal_lifecycle_blur(void);

int pm_metal_lifecycle_native_register(void);
void pm_metal_lifecycle_bind_inst(void *module_inst);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_SHELL_LIFECYCLE_LIFECYCLE_H_ */
