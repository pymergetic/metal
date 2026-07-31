#ifndef PYMERGETIC_METAL_DEV_BLK_H_
#define PYMERGETIC_METAL_DEV_BLK_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_DEV_BLK_INVALID      0u
#define PM_METAL_DEV_BLK_SECTOR_BYTES 512u

/** Probe IDE primary -> DT BLK. Virtio-blk enumerated via PCI class. */
int32_t pm_metal_dev_blk_detect(void);

int32_t  pm_metal_dev_blk_open(void);
uint64_t pm_metal_dev_blk_capacity_sectors(void);
int32_t  pm_metal_dev_blk_read(uint64_t lba, void *buf, uint32_t nsec);

/** Native-pointer awaitable; caller keeps buf alive until completion. */
uint32_t pm_metal_dev_blk_read_async(uint64_t lba, void *buf, uint32_t nsec);
uint32_t pm_metal_dev_blk_result(uint32_t h);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_BLK_H_ */
