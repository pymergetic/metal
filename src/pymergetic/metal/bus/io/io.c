/** @file
  Metal IO device/capability table — multi-device inventory. (impl: efi|bios)
**/
#include <pymergetic/metal/bus/io/io.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

#define PM_METAL_IO_DT_MAX  32u

STATIC pm_metal_io_node_t  mNodes[PM_METAL_IO_DT_MAX];
STATIC UINT32              mCount;
STATIC UINT32              mClassCount[PM_METAL_IO_CLASS_COUNT];

STATIC
INT32
LocEqual (
  CONST UINT32  *a,
  CONST UINT32  *b
  )
{
  return (a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3])
         ? 1
         : 0;
}

int
pm_metal_io_dt_add (
  CONST pm_metal_io_node_t  *node
  )
{
  UINT32  i;

  if (node == NULL || node->compat == NULL) {
    return -1;
  }

  if ((UINT32)node->class >= (UINT32)PM_METAL_IO_CLASS_COUNT) {
    return -1;
  }

  for (i = 0; i < mCount; i++) {
    if (mNodes[i].class == node->class
        && AsciiStrCmp (mNodes[i].compat, node->compat) == 0
        && LocEqual (mNodes[i].loc, node->loc))
    {
      return (INT32)i;
    }
  }

  if (mCount >= PM_METAL_IO_DT_MAX) {
    return -1;
  }

  mNodes[mCount]       = *node;
  mNodes[mCount].unit  = mClassCount[node->class];
  mClassCount[node->class]++;
  mCount++;
  return (INT32)(mCount - 1);
}

CONST pm_metal_io_node_t *
pm_metal_io_dt_get (
  UINT32  id
  )
{
  if (id >= mCount) {
    return NULL;
  }

  return &mNodes[id];
}

UINT32
pm_metal_io_dt_count (
  VOID
  )
{
  return mCount;
}

UINT32
pm_metal_io_dt_count_class (
  pm_metal_io_class_t  class
  )
{
  if ((UINT32)class >= (UINT32)PM_METAL_IO_CLASS_COUNT) {
    return 0;
  }

  return mClassCount[class];
}

CONST pm_metal_io_node_t *
pm_metal_io_dt_by_class (
  pm_metal_io_class_t  class,
  UINT32               index
  )
{
  UINT32  i;
  UINT32  seen;

  if ((UINT32)class >= (UINT32)PM_METAL_IO_CLASS_COUNT) {
    return NULL;
  }

  seen = 0;
  for (i = 0; i < mCount; i++) {
    if (mNodes[i].class != class) {
      continue;
    }

    if (seen == index) {
      return &mNodes[i];
    }

    seen++;
  }

  return NULL;
}

CONST pm_metal_io_node_t *
pm_metal_io_dt_lookup (
  pm_metal_io_class_t  class
  )
{
  return pm_metal_io_dt_by_class (class, 0);
}

void
pm_metal_io_dt_foreach (
  pm_metal_io_dt_iter_fn  fn,
  VOID                   *ctx
  )
{
  UINT32  i;

  if (fn == NULL) {
    return;
  }

  for (i = 0; i < mCount; i++) {
    if (fn (&mNodes[i], ctx) != 0) {
      return;
    }
  }
}

void
pm_metal_io_dt_reset (
  VOID
  )
{
  ZeroMem (mNodes, sizeof (mNodes));
  ZeroMem (mClassCount, sizeof (mClassCount));
  mCount = 0;
}
