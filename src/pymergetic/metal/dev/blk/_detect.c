#include <stdint.h>

#include <pymergetic/metal/boot/platform/io.h>
#include <pymergetic/metal/dev/blk/__init__.h>
#include <pymergetic/metal/dt/__init__.h>

#define IDE_PRIMARY 0x1F0u
#define IDE_STATUS  0x1F7u

static const uint8_t k_compat_ide[] = "ide";

static int32_t already_ide(void)
{
  uint32_t n;
  uint32_t i;

  n = pm_metal_dt_count();
  for (i = 0; i < n; i++) {
    const DtNode *node = pm_metal_dt_get(i);
    if (node == NULL || node->class != PM_METAL_DT_CLASS_BLK) {
      continue;
    }
    if (node->bus == PM_METAL_DT_BUS_ISA && node->loc[0] == IDE_PRIMARY) {
      return 1;
    }
    if ((node->caps & (uint32_t)PM_METAL_DT_CAP_BOUND) != 0u && node->loc[0] == IDE_PRIMARY) {
      return 1;
    }
  }
  return 0;
}

int32_t pm_metal_dev_blk_detect(void)
{
  const pm_metal_boot_io_ops_t *ops;
  DtNode node;
  uint8_t st;

  if (already_ide()) {
    return 0;
  }
  ops = pm_metal_boot_io_ops();
  if (ops == NULL || ops->inb == NULL) {
    return 0;
  }
  st = ops->inb((uint16_t)IDE_STATUS);
  if (st == 0xFFu) {
    return 0;
  }
  node.class  = PM_METAL_DT_CLASS_BLK;
  node.compat = k_compat_ide;
  node.unit   = 0;
  node.caps   = 0;
  node.bus    = PM_METAL_DT_BUS_ISA;
  node.loc[0] = IDE_PRIMARY;
  node.loc[1] = 0;
  node.loc[2] = 0;
  node.loc[3] = 0;
  (void)pm_metal_dt_add(&node);
  return 0;
}
