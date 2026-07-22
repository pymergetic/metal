/*
 * Host-only pluggable block backend ops (per bound device).
 *
 * impl: common — src/pymergetic/metal/dev/blk/blk.c
 * impl: backends — src/pymergetic/metal/dev/blk/{virtio_blk,ide_ata}.c
 */
#ifndef PYMERGETIC_METAL_DEV_BLK_BLK_OPS_H_
#define PYMERGETIC_METAL_DEV_BLK_BLK_OPS_H_

#include <stdint.h>

#include "pymergetic/metal/dev/blk/blk.h"

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

typedef struct pm_metal_blk_ops {
	const char *compat;
	uint32_t    dt_id;
	int (*ready)(void *ctx);
	uint64_t (*capacity)(void *ctx);
	int (*read)(void *ctx, uint64_t lba, void *buf, uint32_t nsec);
	int (*write)(void *ctx, uint64_t lba, const void *buf, uint32_t nsec);
	void (*poll)(void *ctx);
	void *ctx;
} pm_metal_blk_ops_t;

/** Bind a detected device. Returns handle, or PM_METAL_BLK_INVALID. */
pm_metal_blk_h pm_metal_blk_bind(const pm_metal_blk_ops_t *ops);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_BLK_BLK_OPS_H_ */
