/*
 * Host virtio-blk (raw sectors).
 */
#ifndef PYMERGETIC_METAL_BLK_H_
#define PYMERGETIC_METAL_BLK_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

int pm_metal_blk_virtio_probe(void);
int pm_metal_blk_ready(void);
uint64_t pm_metal_blk_capacity_sectors(void);
/** Read/write 512-byte sectors. Returns 0 on success. */
int pm_metal_blk_read(uint64_t lba, void *buf, uint32_t nsec);
int pm_metal_blk_write(uint64_t lba, const void *buf, uint32_t nsec);
void pm_metal_blk_poll(void);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BLK_H_ */
