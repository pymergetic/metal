/** @file
  Metal IO device/capability table. (impl: efi)
**/
#include <pymergetic/metal/io/io.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>

STATIC pm_metal_io_node_t  mNodes[PM_METAL_IO_CLASS_COUNT];
STATIC UINT8               mHave[PM_METAL_IO_CLASS_COUNT];

int
pm_metal_io_dt_register (
  CONST pm_metal_io_node_t  *node
  )
{
  if (node == NULL || node->backend == NULL) {
    return -1;
  }

  if ((UINT32)node->class >= (UINT32)PM_METAL_IO_CLASS_COUNT) {
    return -1;
  }

  mNodes[node->class] = *node;
  mHave[node->class]  = 1;
  return 0;
}

CONST pm_metal_io_node_t *
pm_metal_io_dt_lookup (
  pm_metal_io_class_t  class
  )
{
  if ((UINT32)class >= (UINT32)PM_METAL_IO_CLASS_COUNT) {
    return NULL;
  }

  if (!mHave[class]) {
    return NULL;
  }

  return &mNodes[class];
}

void
pm_metal_io_dt_reset (
  VOID
  )
{
  ZeroMem (mNodes, sizeof (mNodes));
  ZeroMem (mHave, sizeof (mHave));
}
