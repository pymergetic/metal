/** @file
  Dual-span arena — map_brk ↑ , heap_brk ↓ , hole in between. (impl: efi|bios)
**/
#include <runtime/mem/arena.h>
#include <runtime/slot/spin.h>
#include <pymergetic/metal/runtime/mem/mem.h>

#include <string.h>

static uint8_t        *mBase;
static uint8_t        *mEnd;
static uint8_t        *mMapBrk;  /* next free byte for map (grows up) */
static uint8_t        *mHeapBrk; /* first byte of heap region (grows down) */
static pm_metal_spin_t mLock;
static int32_t         mReady;

static size_t MetalAlignUp(size_t n, size_t a)
{
  return (n + (a - 1)) & ~(a - 1);
}

int pm_metal_arena_init(void *base, size_t bytes)
{
  if (mReady || base == NULL || bytes < (PM_METAL_MEM_PAGE_SIZE * 8)) {
    return -1;
  }

  bytes    = bytes & ~(size_t)(PM_METAL_MEM_PAGE_SIZE - 1);
  mBase    = (uint8_t *)base;
  mEnd     = mBase + bytes;
  mMapBrk  = mBase;
  mHeapBrk = mEnd;
  pm_metal_spin_init(&mLock);
  mReady = 1;
  return 0;
}

void *pm_metal_arena_map(size_t bytes)
{
  uint8_t *p;
  size_t   n;

  if (!mReady || bytes == 0) {
    return NULL;
  }

  n = MetalAlignUp(bytes, PM_METAL_MEM_PAGE_SIZE);
  pm_metal_spin_lock(&mLock);
  if ((size_t)(mHeapBrk - mMapBrk) < n) {
    pm_metal_spin_unlock(&mLock);
    return NULL;
  }

  p = mMapBrk;
  mMapBrk += n;
  pm_metal_spin_unlock(&mLock);
  memset(p, 0, n);
  return p;
}

int pm_metal_arena_unmap(void *ptr, size_t bytes)
{
  uint8_t *p;
  size_t   n;

  if (!mReady || ptr == NULL || bytes == 0) {
    return -1;
  }

  n = MetalAlignUp(bytes, PM_METAL_MEM_PAGE_SIZE);
  p = (uint8_t *)ptr;
  pm_metal_spin_lock(&mLock);
  if (p + n != mMapBrk || p < mBase) {
    pm_metal_spin_unlock(&mLock);
    return -1; /* only LIFO unmap */
  }

  mMapBrk = p;
  pm_metal_spin_unlock(&mLock);
  return 0;
}

void *pm_metal_arena_heap_grow(size_t bytes)
{
  uint8_t *p;
  size_t   n;

  if (!mReady || bytes == 0) {
    return NULL;
  }

  n = MetalAlignUp(bytes, PM_METAL_MEM_PAGE_SIZE);
  pm_metal_spin_lock(&mLock);
  if ((size_t)(mHeapBrk - mMapBrk) < n) {
    pm_metal_spin_unlock(&mLock);
    return NULL;
  }

  mHeapBrk -= n;
  p = mHeapBrk;
  pm_metal_spin_unlock(&mLock);
  memset(p, 0, n);
  return p;
}

size_t pm_metal_arena_bytes(void)
{
  if (!mReady) {
    return 0;
  }

  return (size_t)(mEnd - mBase);
}

size_t pm_metal_arena_map_used(void)
{
  if (!mReady) {
    return 0;
  }

  return (size_t)(mMapBrk - mBase);
}

size_t pm_metal_arena_heap_used(void)
{
  if (!mReady) {
    return 0;
  }

  return (size_t)(mEnd - mHeapBrk);
}

size_t pm_metal_arena_hole(void)
{
  if (!mReady) {
    return 0;
  }

  return (size_t)(mHeapBrk - mMapBrk);
}
