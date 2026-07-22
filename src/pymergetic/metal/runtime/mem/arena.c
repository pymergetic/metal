/** @file
  Dual-span arena — map_brk ↑ , heap_brk ↓ , hole in between. (impl: efi|bios)
**/
#include <runtime/mem/arena.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/SynchronizationLib.h>

STATIC UINT8     *mBase;
STATIC UINT8     *mEnd;
STATIC UINT8     *mMapBrk;   /* next free byte for map (grows up) */
STATIC UINT8     *mHeapBrk;  /* first byte of heap region (grows down) */
STATIC SPIN_LOCK  mLock;
STATIC INT32      mReady;

STATIC
size_t
MetalAlignUp (
  size_t  n,
  size_t  a
  )
{
  return (n + (a - 1)) & ~(a - 1);
}

int
pm_metal_arena_init (
  void   *base,
  size_t  bytes
  )
{
  if (mReady || base == NULL || bytes < (EFI_PAGE_SIZE * 8)) {
    return -1;
  }

  bytes = bytes & ~(size_t)(EFI_PAGE_SIZE - 1);
  mBase    = (UINT8 *)base;
  mEnd     = mBase + bytes;
  mMapBrk  = mBase;
  mHeapBrk = mEnd;
  InitializeSpinLock (&mLock);
  mReady   = 1;
  return 0;
}

void *
pm_metal_arena_map (
  size_t  bytes
  )
{
  UINT8  *p;
  size_t  n;

  if (!mReady || bytes == 0) {
    return NULL;
  }

  n = MetalAlignUp (bytes, EFI_PAGE_SIZE);
  AcquireSpinLock (&mLock);
  if ((size_t)(mHeapBrk - mMapBrk) < n) {
    ReleaseSpinLock (&mLock);
    return NULL;
  }

  p         = mMapBrk;
  mMapBrk  += n;
  ReleaseSpinLock (&mLock);
  ZeroMem (p, n);
  return p;
}

int
pm_metal_arena_unmap (
  void   *ptr,
  size_t  bytes
  )
{
  UINT8  *p;
  size_t  n;

  if (!mReady || ptr == NULL || bytes == 0) {
    return -1;
  }

  n = MetalAlignUp (bytes, EFI_PAGE_SIZE);
  p = (UINT8 *)ptr;
  AcquireSpinLock (&mLock);
  if (p + n != mMapBrk || p < mBase) {
    ReleaseSpinLock (&mLock);
    return -1; /* only LIFO unmap */
  }

  mMapBrk = p;
  ReleaseSpinLock (&mLock);
  return 0;
}

void *
pm_metal_arena_heap_grow (
  size_t  bytes
  )
{
  UINT8  *p;
  size_t  n;

  if (!mReady || bytes == 0) {
    return NULL;
  }

  n = MetalAlignUp (bytes, EFI_PAGE_SIZE);
  AcquireSpinLock (&mLock);
  if ((size_t)(mHeapBrk - mMapBrk) < n) {
    ReleaseSpinLock (&mLock);
    return NULL;
  }

  mHeapBrk -= n;
  p         = mHeapBrk;
  ReleaseSpinLock (&mLock);
  ZeroMem (p, n);
  return p;
}

size_t
pm_metal_arena_bytes (
  VOID
  )
{
  if (!mReady) {
    return 0;
  }

  return (size_t)(mEnd - mBase);
}

size_t
pm_metal_arena_map_used (
  VOID
  )
{
  if (!mReady) {
    return 0;
  }

  return (size_t)(mMapBrk - mBase);
}

size_t
pm_metal_arena_heap_used (
  VOID
  )
{
  if (!mReady) {
    return 0;
  }

  return (size_t)(mEnd - mHeapBrk);
}

size_t
pm_metal_arena_hole (
  VOID
  )
{
  if (!mReady) {
    return 0;
  }

  return (size_t)(mHeapBrk - mMapBrk);
}
