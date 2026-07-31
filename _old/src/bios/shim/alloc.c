/* Freestanding bump allocator for Metal BIOS shim. */
#include "PmBiosUefi.h"
#include "Library/MemoryAllocationLib.h"
#include "Library/BaseMemoryLib.h"

static UINT8 *mHeap;
static UINTN mHeapSize;
static UINTN mHeapUsed;

void
pm_bios_shim_set_heap(void *base, UINTN bytes)
{
  mHeap = (UINT8 *)base;
  mHeapSize = bytes;
  mHeapUsed = 0;
}

static VOID *
Bump(UINTN Size, UINTN Align)
{
  UINTN off;
  VOID *p;

  if (mHeap == NULL || Size == 0)
    return NULL;
  off = (mHeapUsed + (Align - 1)) & ~(Align - 1);
  if (off + Size > mHeapSize)
    return NULL;
  p = mHeap + off;
  mHeapUsed = off + Size;
  return p;
}

VOID *
AllocatePool(UINTN AllocationSize)
{
  return Bump(AllocationSize, 8);
}

VOID *
AllocateZeroPool(UINTN AllocationSize)
{
  VOID *p = AllocatePool(AllocationSize);
  if (p != NULL)
    ZeroMem(p, AllocationSize);
  return p;
}

VOID
FreePool(VOID *Buffer)
{
  (VOID)Buffer;
}

VOID *
AllocatePages(UINTN Pages)
{
  return Bump(Pages * EFI_PAGE_SIZE, EFI_PAGE_SIZE);
}

VOID
FreePages(VOID *Buffer, UINTN Pages)
{
  (VOID)Buffer;
  (VOID)Pages;
}
