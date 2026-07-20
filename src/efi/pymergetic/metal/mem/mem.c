/** @file
  Dual-span + TLSF heap + id directory (docs/COOP_MEMORY.md). (impl: efi)
**/
#include <mem/mem.h>
#include <arena/arena.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/SynchronizationLib.h>

#include "tlsf.h"

#define PM_METAL_HEAP_SEED_BYTES  (128u * 1024u * 1024u)
#define PM_METAL_HEAP_GROW_BYTES  (16u * 1024u * 1024u)

typedef struct pm_metal_mem_obj {
  pm_metal_mem_id_t         id;
  VOID                     *ptr;
  struct pm_metal_mem_obj  *prev;
  struct pm_metal_mem_obj  *next;
} pm_metal_mem_obj_t;

STATIC tlsf_t               mTlsf;
STATIC SPIN_LOCK            mHeapLock;
STATIC unsigned             mNCpus;
STATIC unsigned             mCurrentCpu;
STATIC pm_metal_mem_obj_t  *mObjHead;
STATIC SPIN_LOCK            mObjLock;
STATIC INT32                mReady;

STATIC CONST UINTN mSharedClassSizes[PM_METAL_MEM_SHARED_CLASS_COUNT] = {
  64, 256, 1024, 4096
};

void pm_metal_mem_free (void *ptr);

STATIC
VOID *
MetalHeapMalloc (
  size_t  size
  )
{
  VOID   *p;
  VOID   *chunk;
  size_t  grow;

  if (mTlsf == NULL || size == 0) {
    return NULL;
  }

  AcquireSpinLock (&mHeapLock);
  p = tlsf_malloc (mTlsf, size);
  if (p == NULL) {
    grow = PM_METAL_HEAP_GROW_BYTES;
    if (grow < size + 64 * 1024) {
      grow = size + 64 * 1024;
    }

    chunk = pm_metal_arena_heap_grow (grow);
    if (chunk != NULL && tlsf_add_pool (mTlsf, chunk, grow) != NULL) {
      p = tlsf_malloc (mTlsf, size);
    }
  }

  ReleaseSpinLock (&mHeapLock);
  return p;
}

STATIC
VOID
MetalHeapFree (
  VOID  *ptr
  )
{
  if (mTlsf == NULL || ptr == NULL) {
    return;
  }

  AcquireSpinLock (&mHeapLock);
  tlsf_free (mTlsf, ptr);
  ReleaseSpinLock (&mHeapLock);
}

STATIC
VOID
MetalObjInsert (
  pm_metal_mem_obj_t  *node
  )
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

STATIC
VOID
MetalObjUnlink (
  pm_metal_mem_obj_t  *node
  )
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

STATIC
pm_metal_mem_obj_t *
MetalObjFindIdLocked (
  pm_metal_mem_id_t  id
  )
{
  pm_metal_mem_obj_t  *n;

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

STATIC
pm_metal_mem_obj_t *
MetalObjFindPtrLocked (
  VOID  *ptr
  )
{
  pm_metal_mem_obj_t  *n;

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

STATIC
INTN
MetalObjPublish (
  pm_metal_mem_id_t  id,
  VOID              *ptr
  )
{
  pm_metal_mem_obj_t  *node;

  if (id == PM_METAL_MEM_ID_NONE || ptr == NULL) {
    return 0;
  }

  node = (pm_metal_mem_obj_t *)MetalHeapMalloc (sizeof (pm_metal_mem_obj_t));
  if (node == NULL) {
    return -1;
  }

  node->id  = id;
  node->ptr = ptr;

  AcquireSpinLock (&mObjLock);
  if (MetalObjFindIdLocked (id) != NULL) {
    ReleaseSpinLock (&mObjLock);
    MetalHeapFree (node);
    return -1;
  }

  MetalObjInsert (node);
  ReleaseSpinLock (&mObjLock);
  return 0;
}

STATIC
VOID
MetalObjUnpublishPtr (
  VOID  *ptr
  )
{
  pm_metal_mem_obj_t  *node;

  if (ptr == NULL) {
    return;
  }

  AcquireSpinLock (&mObjLock);
  node = MetalObjFindPtrLocked (ptr);
  if (node != NULL) {
    MetalObjUnlink (node);
  }

  ReleaseSpinLock (&mObjLock);

  if (node != NULL) {
    MetalHeapFree (node);
  }
}

int
pm_metal_mem_init (
  void     *arena,
  size_t    bytes,
  unsigned  n_cpus
  )
{
  VOID   *seed;
  size_t  seed_bytes;
  size_t  need;

  if (mReady || arena == NULL || bytes == 0 || n_cpus == 0) {
    return -1;
  }

  if (pm_metal_arena_init (arena, bytes) != 0) {
    return -1;
  }

  InitializeSpinLock (&mHeapLock);
  InitializeSpinLock (&mObjLock);
  mObjHead    = NULL;
  mNCpus      = n_cpus;
  mCurrentCpu = 0;
  mTlsf       = NULL;

  need       = tlsf_size () + tlsf_pool_overhead () + 256 * 1024;
  seed_bytes = PM_METAL_HEAP_SEED_BYTES;
  if (seed_bytes < need) {
    seed_bytes = need;
  }

  if (seed_bytes > pm_metal_arena_hole () / 2) {
    seed_bytes = (pm_metal_arena_hole () / 2) & ~(size_t)(EFI_PAGE_SIZE - 1);
  }

  if (seed_bytes < need) {
    return -1;
  }

  seed = pm_metal_arena_heap_grow (seed_bytes);
  if (seed == NULL) {
    return -1;
  }

  mTlsf = tlsf_create_with_pool (seed, seed_bytes);
  if (mTlsf == NULL) {
    return -1;
  }

  mReady = 1;
  return 0;
}

void
pm_metal_mem_set_cpu (
  unsigned  cpu_id
  )
{
  if (cpu_id < mNCpus) {
    mCurrentCpu = cpu_id;
  }
}

unsigned
pm_metal_mem_cpu (
  VOID
  )
{
  return mCurrentCpu;
}

void *
pm_metal_mem_map (
  size_t  bytes
  )
{
  if (!mReady) {
    return NULL;
  }

  return pm_metal_arena_map (bytes);
}

int
pm_metal_mem_unmap (
  void   *ptr,
  size_t  bytes
  )
{
  if (!mReady) {
    return -1;
  }

  return pm_metal_arena_unmap (ptr, bytes);
}

void *
pm_metal_mem_lookup (
  pm_metal_mem_id_t  id
  )
{
  pm_metal_mem_obj_t  *node;
  VOID                *ptr;

  if (!mReady || id == PM_METAL_MEM_ID_NONE) {
    return NULL;
  }

  ptr = NULL;
  AcquireSpinLock (&mObjLock);
  node = MetalObjFindIdLocked (id);
  if (node != NULL) {
    ptr = node->ptr;
  }

  ReleaseSpinLock (&mObjLock);
  return ptr;
}

void *
pm_metal_mem_alloc (
  size_t                size,
  pm_metal_mem_flags_t  where,
  pm_metal_mem_id_t     id
  )
{
  VOID  *ptr;

  if (!mReady || size == 0) {
    return NULL;
  }

  if (id != PM_METAL_MEM_ID_NONE && pm_metal_mem_lookup (id) != NULL) {
    return NULL;
  }

  if ((where & PM_METAL_MEM_MAP) != 0) {
    ptr = pm_metal_mem_map (size);
  } else {
    (VOID)where; /* HEAP / SHARED / CPU(k) → unified TLSF */
    ptr = MetalHeapMalloc (size);
  }

  if (ptr == NULL) {
    return NULL;
  }

  if (id != PM_METAL_MEM_ID_NONE && MetalObjPublish (id, ptr) != 0) {
    if ((where & PM_METAL_MEM_MAP) != 0) {
      pm_metal_mem_unmap (ptr, size);
    } else {
      MetalHeapFree (ptr);
    }

    return NULL;
  }

  return ptr;
}

void
pm_metal_mem_free (
  void  *ptr
  )
{
  if (!mReady || ptr == NULL) {
    return;
  }

  /* Heap / TLSF only. Page maps use pm_metal_mem_unmap (LIFO). */
  MetalObjUnpublishPtr (ptr);
  MetalHeapFree (ptr);
}

void *
pm_metal_mem_realloc (
  void   *ptr,
  size_t  size
  )
{
  VOID  *n;

  if (!mReady) {
    return NULL;
  }

  if (ptr == NULL) {
    return pm_metal_mem_alloc (size, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  }

  if (size == 0) {
    pm_metal_mem_free (ptr);
    return NULL;
  }

  AcquireSpinLock (&mHeapLock);
  n = tlsf_realloc (mTlsf, ptr, size);
  ReleaseSpinLock (&mHeapLock);
  if (n != NULL) {
    return n;
  }

  n = pm_metal_mem_alloc (size, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (n != NULL) {
    CopyMem (n, ptr, size);
    pm_metal_mem_free (ptr);
  }

  return n;
}

void *
pm_metal_mem_shared_alloc_class (
  pm_metal_mem_shared_class_t  cls
  )
{
  if (cls >= PM_METAL_MEM_SHARED_CLASS_COUNT) {
    return NULL;
  }

  return pm_metal_mem_alloc (
           mSharedClassSizes[cls],
           PM_METAL_MEM_SHARED,
           PM_METAL_MEM_ID_NONE
           );
}

void
pm_metal_mem_shared_free (
  void  *ptr
  )
{
  pm_metal_mem_free (ptr);
}

size_t
pm_metal_mem_arena_bytes (
  VOID
  )
{
  return pm_metal_arena_bytes ();
}

size_t
pm_metal_mem_map_bytes (
  VOID
  )
{
  return pm_metal_arena_map_used ();
}

size_t
pm_metal_mem_heap_bytes (
  VOID
  )
{
  return pm_metal_arena_heap_used ();
}

size_t
pm_metal_mem_hole_bytes (
  VOID
  )
{
  return pm_metal_arena_hole ();
}

size_t
pm_metal_mem_local_bytes (
  VOID
  )
{
  return pm_metal_arena_heap_used ();
}

size_t
pm_metal_mem_shared_bytes (
  VOID
  )
{
  return pm_metal_arena_map_used ();
}

size_t
pm_metal_mem_os_bytes (
  VOID
  )
{
  return pm_metal_arena_hole ();
}

unsigned
pm_metal_mem_n_cpus (
  VOID
  )
{
  return mNCpus;
}
