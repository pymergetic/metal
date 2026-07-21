/*
 * Metal IO device/capability table (host DT) — see docs/IO.md.
 */
#ifndef PYMERGETIC_METAL_IO_H_
#define PYMERGETIC_METAL_IO_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	PM_METAL_IO_TIME = 0,
	PM_METAL_IO_GFX,
	PM_METAL_IO_AUDIO,
	PM_METAL_IO_INPUT,
	PM_METAL_IO_FS,
	PM_METAL_IO_STREAM,
	PM_METAL_IO_NET,
	PM_METAL_IO_RANDOM,
	PM_METAL_IO_CLASS_COUNT
} pm_metal_io_class_t;

typedef struct {
	pm_metal_io_class_t class;
	const char         *backend; /* "software", "null", "efi", "esp", … */
	uint32_t            caps;
} pm_metal_io_node_t;

#if !defined(__wasm__)
/** Register or replace the node for node->class. Returns 0 ok. */
int pm_metal_io_dt_register(const pm_metal_io_node_t *node);
/** Lookup; returns NULL if none registered. */
const pm_metal_io_node_t *pm_metal_io_dt_lookup(pm_metal_io_class_t class);
/** Clear table (tests / fini). */
void pm_metal_io_dt_reset(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_IO_H_ */
