/** @file
  Dual-span + TLSF heap + id directory (docs/COOP_MEMORY.md).
**/
#include "metal_mem.h"
#include "metal_arena.h"

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/SynchronizationLib.h>

#include "tlsf.h"

#define METAL_HEAP_SEED_BYTES  (16u * 1024u * 1024u)
#define METAL_HEAP_GROW_BYTES  (4u * 1024u * 1024u)

typedef struct metal_obj {
  metal_id_t         id;
  VOID              *ptr;
  struct metal_obj  *prev;
  struct metal_obj  *next;
} metal_obj_t;

STATIC tlsf_t       mTlsf;
STATIC SPIN_LOCK    mHeapLock;
STATIC unsigned     mNCpus;
STATIC unsigned     mCurrentCpu;
STATIC metal_obj_t *mObjHead;
STATIC SPIN_LOCK    mObjLock;
STATIC INT32        mReady;

STATIC CONST UINTN mSharedClassSizes[METAL_SHARED_CLASS_COUNT] = {
  64, 256, 1024, 4096
};

void metal_free (void *ptr);

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
    grow = METAL_HEAP_GROW_BYTES;
    if (grow < size + 64 * 1024) {
      grow = size + 64 * 1024;
    }

    chunk = metal_arena_heap_grow (grow);
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
  metal_obj_t  *node
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
  metal_obj_t  *node
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
metal_obj_t *
MetalObjFindIdLocked (
  metal_id_t  id
  )
{
  metal_obj_t  *n;

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
metal_obj_t *
MetalObjFindPtrLocked (
  VOID  *ptr
  )
{
  metal_obj_t  *n;

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
  metal_id_t  id,
  VOID       *ptr
  )
{
  metal_obj_t  *node;

  if (id == METAL_ID_NONE || ptr == NULL) {
    return 0;
  }

  node = (metal_obj_t *)MetalHeapMalloc (sizeof (metal_obj_t));
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
  metal_obj_t  *node;

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
metal_mem_init (
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

  if (metal_arena_init (arena, bytes) != 0) {
    return -1;
  }

  InitializeSpinLock (&mHeapLock);
  InitializeSpinLock (&mObjLock);
  mObjHead    = NULL;
  mNCpus      = n_cpus;
  mCurrentCpu = 0;
  mTlsf       = NULL;

  need       = tlsf_size () + tlsf_pool_overhead () + 256 * 1024;
  seed_bytes = METAL_HEAP_SEED_BYTES;
  if (seed_bytes < need) {
    seed_bytes = need;
  }

  if (seed_bytes > metal_arena_hole () / 2) {
    seed_bytes = (metal_arena_hole () / 2) & ~(size_t)(EFI_PAGE_SIZE - 1);
  }

  if (seed_bytes < need) {
    return -1;
  }

  seed = metal_arena_heap_grow (seed_bytes);
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
metal_mem_set_cpu (
  unsigned  cpu_id
  )
{
  if (cpu_id < mNCpus) {
    mCurrentCpu = cpu_id;
  }
}

void *
metal_map (
  size_t  bytes
  )
{
  if (!mReady) {
    return NULL;
  }

  return metal_arena_map (bytes);
}

int
metal_unmap (
  void   *ptr,
  size_t  bytes
  )
{
  if (!mReady) {
    return -1;
  }

  return metal_arena_unmap (ptr, bytes);
}

void *
metal_lookup (
  metal_id_t  id
  )
{
  metal_obj_t  *node;
  VOID         *ptr;

  if (!mReady || id == METAL_ID_NONE) {
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
metal_alloc (
  size_t             size,
  metal_mem_flags_t  where,
  metal_id_t         id
  )
{
  VOID  *ptr;

  if (!mReady || size == 0) {
    return NULL;
  }

  if (id != METAL_ID_NONE && metal_lookup (id) != NULL) {
    return NULL;
  }

  if ((where & METAL_MEM_MAP) != 0) {
    ptr = metal_map (size);
  } else {
    (VOID)where; /* HEAP / SHARED / CPU(k) → unified TLSF */
    ptr = MetalHeapMalloc (size);
  }

  if (ptr == NULL) {
    return NULL;
  }

  if (id != METAL_ID_NONE && MetalObjPublish (id, ptr) != 0) {
    if ((where & METAL_MEM_MAP) != 0) {
      metal_unmap (ptr, size);
    } else {
      MetalHeapFree (ptr);
    }

    return NULL;
  }

  return ptr;
}

void
metal_free (
  void  *ptr
  )
{
  if (!mReady || ptr == NULL) {
    return;
  }

  /* Heap / TLSF only. Page maps use metal_unmap (LIFO). */
  MetalObjUnpublishPtr (ptr);
  MetalHeapFree (ptr);
}

void *
metal_realloc (
  void   *ptr,
  size_t  size
  )
{
  VOID  *n;

  if (!mReady) {
    return NULL;
  }

  if (ptr == NULL) {
    return metal_alloc (size, METAL_MEM_HEAP, METAL_ID_NONE);
  }

  if (size == 0) {
    metal_free (ptr);
    return NULL;
  }

  AcquireSpinLock (&mHeapLock);
  n = tlsf_realloc (mTlsf, ptr, size);
  ReleaseSpinLock (&mHeapLock);
  if (n != NULL) {
    return n;
  }

  n = metal_alloc (size, METAL_MEM_HEAP, METAL_ID_NONE);
  if (n != NULL) {
    CopyMem (n, ptr, size);
    metal_free (ptr);
  }

  return n;
}

void *
metal_shared_alloc_class (
  metal_shared_class_t  cls
  )
{
  if (cls >= METAL_SHARED_CLASS_COUNT) {
    return NULL;
  }

  return metal_alloc (mSharedClassSizes[cls], METAL_MEM_SHARED, METAL_ID_NONE);
}

void
metal_shared_free (
  void  *ptr
  )
{
  metal_free (ptr);
}

size_t
metal_mem_arena_bytes (
  VOID
  )
{
  return metal_arena_bytes ();
}

size_t
metal_mem_map_bytes (
  VOID
  )
{
  return metal_arena_map_used ();
}

size_t
metal_mem_heap_bytes (
  VOID
  )
{
  return metal_arena_heap_used ();
}

size_t
metal_mem_hole_bytes (
  VOID
  )
{
  return metal_arena_hole ();
}

size_t
metal_mem_local_bytes (
  VOID
  )
{
  return metal_arena_heap_used ();
}

size_t
metal_mem_shared_bytes (
  VOID
  )
{
  return metal_arena_map_used ();
}

size_t
metal_mem_os_bytes (
  VOID
  )
{
  return metal_arena_hole ();
}

unsigned
metal_mem_n_cpus (
  VOID
  )
{
  return mNCpus;
}
