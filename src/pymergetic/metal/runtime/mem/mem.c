/** @file
  Dual-span + TLSF heap + id directory (docs/COOP_MEMORY.md). (impl: efi|bios)
**/
#include <pymergetic/metal/runtime/mem/mem.h>
#include <pymergetic/metal/runtime/mem/limit.h>
#include <runtime/mem/arena.h>
#include <runtime/stack/stack.h>
#include <runtime/slot/spin.h>

#include <string.h>

#include "tlsf.h"

#define PM_METAL_HEAP_SEED_BYTES (128u * 1024u * 1024u)
#define PM_METAL_HEAP_GROW_BYTES (16u * 1024u * 1024u)
#define PM_METAL_HEAP_POOLS_MAX  32u

typedef struct pm_metal_mem_obj {
  pm_metal_mem_id_t        id;
  void                    *ptr;
  struct pm_metal_mem_obj *prev;
  struct pm_metal_mem_obj *next;
} pm_metal_mem_obj_t;

typedef struct {
  size_t used;
  size_t free;
} pm_metal_heap_walk_t;

static tlsf_t              mTlsf;
static pool_t              mPools[PM_METAL_HEAP_POOLS_MAX];
static unsigned            mPoolCount;
static pm_metal_spin_t     mHeapLock;
static unsigned            mNCpus;
static unsigned            mCurrentCpu;
static pm_metal_mem_obj_t *mObjHead;
static pm_metal_spin_t     mObjLock;
static int32_t             mReady;
static size_t              mPhysBytes;

static void MetalPoolNote(pool_t pool)
{
  if (pool == NULL || mPoolCount >= PM_METAL_HEAP_POOLS_MAX) {
    return;
  }

  mPools[mPoolCount++] = pool;
}

static void MetalHeapWalk(void *ptr, size_t size, int used, void *user)
{
  pm_metal_heap_walk_t *st;

  (void)ptr;
  st = (pm_metal_heap_walk_t *)user;
  if (used) {
    st->used += size;
  } else {
    st->free += size;
  }
}

static const uintptr_t mSharedClassSizes[PM_METAL_MEM_SHARED_CLASS_COUNT] = { 64, 256, 1024, 4096 };

void pm_metal_mem_free(void *ptr);

static void *MetalHeapMalloc(size_t size)
{
  void  *p;
  void  *chunk;
  size_t grow;

  if (mTlsf == NULL || size == 0) {
    return NULL;
  }

  pm_metal_spin_lock(&mHeapLock);
  p = tlsf_malloc(mTlsf, size);
  if (p == NULL) {
    /*
     * TLSF is segregated-fit: a request of `size` searches the free-list
     * bucket for a *rounded-up* threshold (next second-level-index slot,
     * ~size/32 above `size`), never the bucket `size` itself would land
     * in on insert -- that bucket may hold blocks smaller than `size`.
     * A pool grown to `size` plus a small flat pad (previously 64 KiB)
     * produces a single free block whose insert bucket is *below* that
     * search threshold once size is above a few MiB (bucket width scales
     * with size, e.g. 512 KiB at the 16-32 MiB class) -- tlsf_malloc then
     * finds nothing and fails even though the block is big enough.
     * size/8 comfortably clears the largest possible bucket width
     * (size/32) at any scale, so the grown block always lands at or above
     * the search bucket.
     */
    grow = PM_METAL_HEAP_GROW_BYTES;
    if (grow < size + size / 8 + 64 * 1024) {
      grow = size + size / 8 + 64 * 1024;
    }

    chunk = pm_metal_arena_heap_grow(grow);
    if (chunk != NULL) {
      pool_t pool;

      pool = tlsf_add_pool(mTlsf, chunk, grow);
      if (pool != NULL) {
        MetalPoolNote(pool);
        p = tlsf_malloc(mTlsf, size);
      }
    }
  }

  pm_metal_spin_unlock(&mHeapLock);
  return p;
}

static void MetalHeapFree(void *ptr)
{
  if (mTlsf == NULL || ptr == NULL) {
    return;
  }

  pm_metal_spin_lock(&mHeapLock);
  tlsf_free(mTlsf, ptr);
  pm_metal_spin_unlock(&mHeapLock);
}

static void MetalObjInsert(pm_metal_mem_obj_t *node)
{
  if (mObjHead == NULL) {
    node->next = node;
    node->prev = node;
    mObjHead   = node;
    return;
  }

  node->next           = mObjHead;
  node->prev           = mObjHead->prev;
  mObjHead->prev->next = node;
  mObjHead->prev       = node;
}

static void MetalObjUnlink(pm_metal_mem_obj_t *node)
{
  if (node->next == node) {
    mObjHead = NULL;
  } else {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    if (mObjHead == node) {
      mObjHead = node->next;
    }
  }

  node->next = NULL;
  node->prev = NULL;
}

static pm_metal_mem_obj_t *MetalObjFindIdLocked(pm_metal_mem_id_t id)
{
  pm_metal_mem_obj_t *n;

  if (mObjHead == NULL) {
    return NULL;
  }

  n = mObjHead;
  do {
    if (n->id == id) {
      return n;
    }

    n = n->next;
  } while (n != mObjHead);

  return NULL;
}

static pm_metal_mem_obj_t *MetalObjFindPtrLocked(void *ptr)
{
  pm_metal_mem_obj_t *n;

  if (mObjHead == NULL) {
    return NULL;
  }

  n = mObjHead;
  do {
    if (n->ptr == ptr) {
      return n;
    }

    n = n->next;
  } while (n != mObjHead);

  return NULL;
}

static intptr_t MetalObjPublish(pm_metal_mem_id_t id, void *ptr)
{
  pm_metal_mem_obj_t *node;

  if (id == PM_METAL_MEM_ID_NONE || ptr == NULL) {
    return 0;
  }

  node = (pm_metal_mem_obj_t *)MetalHeapMalloc(sizeof(pm_metal_mem_obj_t));
  if (node == NULL) {
    return -1;
  }

  node->id  = id;
  node->ptr = ptr;

  pm_metal_spin_lock(&mObjLock);
  if (MetalObjFindIdLocked(id) != NULL) {
    pm_metal_spin_unlock(&mObjLock);
    MetalHeapFree(node);
    return -1;
  }

  MetalObjInsert(node);
  pm_metal_spin_unlock(&mObjLock);
  return 0;
}

static void MetalObjUnpublishPtr(void *ptr)
{
  pm_metal_mem_obj_t *node;

  if (ptr == NULL) {
    return;
  }

  pm_metal_spin_lock(&mObjLock);
  node = MetalObjFindPtrLocked(ptr);
  if (node != NULL) {
    MetalObjUnlink(node);
  }

  pm_metal_spin_unlock(&mObjLock);

  if (node != NULL) {
    MetalHeapFree(node);
  }
}

int pm_metal_mem_init(void *arena, size_t bytes, unsigned n_cpus)
{
  void  *seed;
  size_t seed_bytes;
  size_t need;

  if (mReady || arena == NULL || bytes == 0 || n_cpus == 0) {
    return -1;
  }

  if (pm_metal_arena_init(arena, bytes) != 0) {
    return -1;
  }

  pm_metal_spin_init(&mHeapLock);
  pm_metal_spin_init(&mObjLock);
  mObjHead    = NULL;
  mNCpus      = n_cpus;
  mCurrentCpu = 0;
  mTlsf       = NULL;

  need       = tlsf_size() + tlsf_pool_overhead() + 256 * 1024;
  seed_bytes = PM_METAL_HEAP_SEED_BYTES;
  if (seed_bytes < need) {
    seed_bytes = need;
  }

  if (seed_bytes > pm_metal_arena_hole() / 2) {
    seed_bytes = (pm_metal_arena_hole() / 2) & ~(size_t)(PM_METAL_MEM_PAGE_SIZE - 1);
  }

  if (seed_bytes < need) {
    return -1;
  }

  seed = pm_metal_arena_heap_grow(seed_bytes);
  if (seed == NULL) {
    return -1;
  }

  mPoolCount = 0;
  mTlsf      = tlsf_create_with_pool(seed, seed_bytes);
  if (mTlsf == NULL) {
    return -1;
  }

  MetalPoolNote(tlsf_get_pool(mTlsf));
  mReady = 1;
  return 0;
}

void pm_metal_mem_set_cpu(unsigned cpu_id)
{
  /*
   * Fallback only — real identity is the Metal stack under SP
   * (see pm_metal_mem_cpu). A global here races with AP run_loops.
   */
  if (cpu_id < mNCpus) {
    mCurrentCpu = cpu_id;
  }
}

unsigned pm_metal_mem_cpu(void)
{
  unsigned  i;
  unsigned  n;
  uintptr_t sp;
  uint8_t  *base;

  /*
   * Derive runner id from the stack we are on. APs and the BSP each
   * SwitchStack into a private MAP stack; a single mCurrentCpu global
   * was overwritten by every AP run_loop and made session affinity
   * randomly pin wasm/AOT onto an AP (doom AOT #GP under -smp>1).
   */
  sp = (uintptr_t)(uintptr_t)__builtin_frame_address(0);
  n  = pm_metal_stack_n_cpus();
  for (i = 0; i < n; i++) {
    base = (uint8_t *)pm_metal_stack_base(i);
    if (base == NULL) {
      continue;
    }

    if (sp >= (uintptr_t)(uintptr_t)base &&
        sp < (uintptr_t)(uintptr_t)base + (uintptr_t)PM_METAL_STACK_BYTES) {
      return i;
    }
  }

  return mCurrentCpu;
}

void *pm_metal_mem_map(size_t bytes)
{
  if (!mReady) {
    return NULL;
  }

  return pm_metal_arena_map(bytes);
}

int pm_metal_mem_unmap(void *ptr, size_t bytes)
{
  if (!mReady) {
    return -1;
  }

  return pm_metal_arena_unmap(ptr, bytes);
}

void *pm_metal_mem_lookup(pm_metal_mem_id_t id)
{
  pm_metal_mem_obj_t *node;
  void               *ptr;

  if (!mReady || id == PM_METAL_MEM_ID_NONE) {
    return NULL;
  }

  ptr = NULL;
  pm_metal_spin_lock(&mObjLock);
  node = MetalObjFindIdLocked(id);
  if (node != NULL) {
    ptr = node->ptr;
  }

  pm_metal_spin_unlock(&mObjLock);
  return ptr;
}

void *pm_metal_mem_alloc(size_t size, pm_metal_mem_flags_t where, pm_metal_mem_id_t id)
{
  void *ptr;

  if (!mReady || size == 0) {
    return NULL;
  }

  if (id != PM_METAL_MEM_ID_NONE && pm_metal_mem_lookup(id) != NULL) {
    return NULL;
  }

  if ((where & PM_METAL_MEM_MAP) != 0) {
    ptr = pm_metal_mem_map(size);
  } else {
    (void)where; /* HEAP / SHARED / CPU(k) → unified TLSF */
    ptr = MetalHeapMalloc(size);
  }

  if (ptr == NULL) {
    return NULL;
  }

  if (id != PM_METAL_MEM_ID_NONE && MetalObjPublish(id, ptr) != 0) {
    if ((where & PM_METAL_MEM_MAP) != 0) {
      pm_metal_mem_unmap(ptr, size);
    } else {
      MetalHeapFree(ptr);
    }

    return NULL;
  }

  return ptr;
}

void pm_metal_mem_free(void *ptr)
{
  if (!mReady || ptr == NULL) {
    return;
  }

  /* Heap / TLSF only. Page maps use pm_metal_mem_unmap (LIFO). */
  MetalObjUnpublishPtr(ptr);
  MetalHeapFree(ptr);
}

void *pm_metal_mem_realloc(void *ptr, size_t size)
{
  void  *n;
  size_t old_size;
  size_t copy;

  if (!mReady) {
    return NULL;
  }

  if (ptr == NULL) {
    return pm_metal_mem_alloc(size, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  }

  if (size == 0) {
    pm_metal_mem_free(ptr);
    return NULL;
  }

  /*
   * Prefer tlsf_realloc (in-pool resize/move; already MIN(old, new)).
   * If pools are full: grow via alloc, copy MIN(old, new), free old
   * (METAL-001 — never copy new_size past the old block).
   */
  pm_metal_spin_lock(&mHeapLock);
  n        = tlsf_realloc(mTlsf, ptr, size);
  old_size = (n == NULL) ? tlsf_block_size(ptr) : 0;
  pm_metal_spin_unlock(&mHeapLock);
  if (n != NULL) {
    return n;
  }

  n = pm_metal_mem_alloc(size, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (n != NULL) {
    copy = (old_size < size) ? old_size : size;
    memcpy(n, ptr, copy);
    pm_metal_mem_free(ptr);
  }

  return n;
}

void *pm_metal_mem_shared_alloc_class(pm_metal_mem_shared_class_t cls)
{
  if (cls >= PM_METAL_MEM_SHARED_CLASS_COUNT) {
    return NULL;
  }

  return pm_metal_mem_alloc(mSharedClassSizes[cls], PM_METAL_MEM_SHARED, PM_METAL_MEM_ID_NONE);
}

void pm_metal_mem_shared_free(void *ptr)
{
  pm_metal_mem_free(ptr);
}

size_t pm_metal_mem_arena_bytes(void)
{
  return pm_metal_arena_bytes();
}

size_t pm_metal_mem_map_bytes(void)
{
  return pm_metal_arena_map_used();
}

size_t pm_metal_mem_heap_bytes(void)
{
  return pm_metal_arena_heap_used();
}

size_t pm_metal_mem_hole_bytes(void)
{
  return pm_metal_arena_hole();
}

void pm_metal_mem_heap_pool_bytes(size_t *used_out, size_t *free_out)
{
  pm_metal_heap_walk_t st;
  unsigned             i;

  st.used = 0;
  st.free = 0;
  if (mReady && mTlsf != NULL) {
    pm_metal_spin_lock(&mHeapLock);
    for (i = 0; i < mPoolCount; i++) {
      tlsf_walk_pool(mPools[i], MetalHeapWalk, &st);
    }

    pm_metal_spin_unlock(&mHeapLock);
  }

  if (used_out != NULL) {
    *used_out = st.used;
  }

  if (free_out != NULL) {
    *free_out = st.free;
  }
}

size_t pm_metal_mem_local_bytes(void)
{
  return pm_metal_arena_heap_used();
}

size_t pm_metal_mem_shared_bytes(void)
{
  return pm_metal_arena_map_used();
}

size_t pm_metal_mem_os_bytes(void)
{
  return pm_metal_arena_hole();
}

unsigned pm_metal_mem_n_cpus(void)
{
  return mNCpus;
}

void pm_metal_mem_set_phys_bytes(size_t phys_bytes)
{
  mPhysBytes = phys_bytes;
}

size_t pm_metal_mem_phys_bytes(void)
{
  return mPhysBytes;
}

PM_METAL_MEM_LIMIT(g_pm_metal_lim_runtime_mem_HEAP_SEED_BYTES,
                   "runtime.mem",
                   "PM_METAL_HEAP_SEED_BYTES",
                   PM_METAL_HEAP_SEED_BYTES,
                   "bytes",
                   "TLSF host heap seed carve");

PM_METAL_MEM_LIMIT(g_pm_metal_lim_runtime_mem_HEAP_GROW_BYTES,
                   "runtime.mem",
                   "PM_METAL_HEAP_GROW_BYTES",
                   PM_METAL_HEAP_GROW_BYTES,
                   "bytes",
                   "TLSF host heap grow quantum");
