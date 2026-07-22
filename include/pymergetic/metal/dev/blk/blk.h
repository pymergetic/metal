/*
 * Block devices — guest/host dual ABI (raw 512-byte sectors).
 *
 * Sync: count / at / ready / capacity_sectors.
 * Async: read_async / write_async → await → blk_result (sectors xfer'd, or 0).
 *
 * Host also keeps sync read/write with native pointers (detectors, boot).
 *
 * impl: common — src/pymergetic/metal/dev/blk/blk.c
 * impl: backends — src/pymergetic/metal/dev/blk/{virtio_blk,ide_ata}.c
 */
#ifndef PYMERGETIC_METAL_DEV_BLK_BLK_H_
#define PYMERGETIC_METAL_DEV_BLK_BLK_H_

#include <stdint.h>

#include "pymergetic/metal/runtime/async/async.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t pm_metal_blk_h;

#define PM_METAL_BLK_INVALID  ((pm_metal_blk_h)0xffffffffu)

/** Guest linear offset for read_async dest / write_async src. */
#if defined(__wasm__)
#define PM_METAL_BLK_IO_PTR(p) ((uint32_t)(uintptr_t)(p))
#endif

#define PM_METAL_BLK_WASI_MODULE "pymergetic.metal.blk"

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_BLK_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_BLK_WASI_MODULE, name)

extern uint32_t pm_metal_blk_count(void)
	PM_METAL_BLK_IMPORT(pm_metal_blk_count);
extern pm_metal_blk_h pm_metal_blk_at(uint32_t index)
	PM_METAL_BLK_IMPORT(pm_metal_blk_at);
extern int32_t pm_metal_blk_ready(pm_metal_blk_h h)
	PM_METAL_BLK_IMPORT(pm_metal_blk_ready);
extern uint64_t pm_metal_blk_capacity_sectors(pm_metal_blk_h h)
	PM_METAL_BLK_IMPORT(pm_metal_blk_capacity_sectors);
/**
 * Awaitable sector read into guest linear buffer at dest (nsec * 512 bytes).
 * Await, then pm_metal_blk_result(self) → sectors copied (0 on failure).
 */
extern pm_metal_async_handle_t pm_metal_blk_read_async(pm_metal_blk_h h,
						       uint64_t lba,
						       uint32_t dest,
						       uint32_t nsec)
	PM_METAL_BLK_IMPORT(pm_metal_blk_read_async);
/**
 * Awaitable sector write from guest linear buffer at src.
 * Await, then pm_metal_blk_result(self) → sectors written (0 on failure).
 */
extern pm_metal_async_handle_t pm_metal_blk_write_async(pm_metal_blk_h h,
							uint64_t lba,
							uint32_t src,
							uint32_t nsec)
	PM_METAL_BLK_IMPORT(pm_metal_blk_write_async);
/** After await resumes on self: sectors transferred, or 0. */
extern uint32_t pm_metal_blk_result(pm_metal_async_handle_t self_h)
	PM_METAL_BLK_IMPORT(pm_metal_blk_result);
#else
uint32_t pm_metal_blk_count(void);
pm_metal_blk_h pm_metal_blk_at(uint32_t index);
int pm_metal_blk_ready(pm_metal_blk_h h);
uint64_t pm_metal_blk_capacity_sectors(pm_metal_blk_h h);

pm_metal_async_handle_t pm_metal_blk_read_async(pm_metal_blk_h h, uint64_t lba,
						uint32_t dest, uint32_t nsec);
pm_metal_async_handle_t pm_metal_blk_write_async(pm_metal_blk_h h, uint64_t lba,
						 uint32_t src, uint32_t nsec);
uint32_t pm_metal_blk_result(pm_metal_async_handle_t self_h);

/** Host sync I/O — native pointers. Prefer *_async from guests. impl: common — blk.c */
int pm_metal_blk_read(pm_metal_blk_h h, uint64_t lba, void *buf, uint32_t nsec);
int pm_metal_blk_write(pm_metal_blk_h h, uint64_t lba, const void *buf,
		       uint32_t nsec);
void pm_metal_blk_poll(void);

/** Independent detectors — each adds 0..N DT nodes and binds. impl: common — virtio_blk.c */
int pm_metal_blk_virtio_detect(void);
/** Re-bind virtq rings after ExitBootServices. impl: common — virtio_blk.c */
int pm_metal_blk_virtio_resume(void);
/** impl: common — ide_ata.c */
int pm_metal_blk_ide_detect(void);

void pm_metal_blk_bind_inst(void *module_inst);
int pm_metal_blk_native_register(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_BLK_BLK_H_ */
