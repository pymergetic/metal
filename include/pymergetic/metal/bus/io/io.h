/*
 * Metal IO device/capability table (host DT) — see docs/IO.md.
 * Append-only inventory of present devices; multiple nodes per class allowed.
 *
 * impl: common — src/pymergetic/metal/bus/io/io.c
 */
#ifndef PYMERGETIC_METAL_BUS_IO_IO_H_
#define PYMERGETIC_METAL_BUS_IO_IO_H_

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
	PM_METAL_IO_BLK,
	PM_METAL_IO_CLASS_COUNT
} pm_metal_io_class_t;

typedef enum {
	PM_METAL_IO_BUS_PLATFORM = 0,
	PM_METAL_IO_BUS_PCI,
	PM_METAL_IO_BUS_ISA
} pm_metal_io_bus_t;

typedef struct {
	pm_metal_io_class_t class;
	const char         *compat; /* "tsc", "virtio-blk", "ide-ata", … */
	uint32_t            unit;   /* assigned on add; index within class */
	uint32_t            caps;
	uint32_t            bus;    /* pm_metal_io_bus_t */
	uint32_t            loc[4]; /* PCI bus/dev/func/bar or ISA ports */
} pm_metal_io_node_t;

#if !defined(__wasm__)

typedef int (*pm_metal_io_dt_iter_fn)(const pm_metal_io_node_t *node, void *ctx);

/**
 * Append a present device. Sets node->unit. Idempotent for same
 * compat+loc. Returns DT id (>=0), or -1 on error/full.
 */
int pm_metal_io_dt_add(const pm_metal_io_node_t *node);
const pm_metal_io_node_t *pm_metal_io_dt_get(uint32_t id);
uint32_t pm_metal_io_dt_count(void);
uint32_t pm_metal_io_dt_count_class(pm_metal_io_class_t class);
/** Nth node of class (0-based), or NULL. */
const pm_metal_io_node_t *pm_metal_io_dt_by_class(pm_metal_io_class_t class,
						  uint32_t index);
/** First node of class, or NULL. */
const pm_metal_io_node_t *pm_metal_io_dt_lookup(pm_metal_io_class_t class);
void pm_metal_io_dt_foreach(pm_metal_io_dt_iter_fn fn, void *ctx);
void pm_metal_io_dt_reset(void);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BUS_IO_IO_H_ */
