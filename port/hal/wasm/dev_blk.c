/*
 * Browser seat-local blk HAL — no virtio/IDE. Fail-closed until a real
 * browser block backend exists. Not a port dump.
 */
#include <stddef.h>
#include <stdint.h>

#include <pymergetic/metal/dev/blk/__init__.h>

int32_t pm_metal_dev_blk_detect(void)
{
    return 0;
}

int32_t pm_metal_dev_blk_open(void)
{
    return -1;
}

uint64_t pm_metal_dev_blk_capacity_sectors(void)
{
    return 0;
}

int32_t pm_metal_dev_blk_read(uint64_t lba, void *buf, uint32_t nsec)
{
    (void)lba;
    (void)buf;
    (void)nsec;
    return -1;
}

uint32_t pm_metal_dev_blk_read_async(uint64_t lba, void *buf, uint32_t nsec)
{
    (void)lba;
    (void)buf;
    (void)nsec;
    return 0;
}

uint32_t pm_metal_dev_blk_result(uint32_t h)
{
    (void)h;
    return 0;
}
