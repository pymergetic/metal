/** @file
  Metal memory front end — dual-span arena + TLSF malloc + optional id publish.
  See docs/COOP_MEMORY.md
**/
#ifndef PM_METAL_MEM_H_
#define PM_METAL_MEM_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* where: HEAP (malloc/TLSF) is zero; MAP is page carve from low brk. */
#define PM_METAL_MEM_LOCAL   0u          /* heap / TLSF (compat name) */
#define PM_METAL_MEM_HEAP    PM_METAL_MEM_LOCAL
#define PM_METAL_MEM_SHARED  (1u << 0)   /* heap + publish-friendly (same pool) */
#define PM_METAL_MEM_MAP     (1u << 2)   /* page map from dual-span low side */
#define PM_METAL_MEM_CPU_MASK  0x0000ff00u
#define PM_METAL_MEM_CPU_SHIFT 8
#define PM_METAL_MEM_CPU(k)  ((((unsigned)(k) << PM_METAL_MEM_CPU_SHIFT) & PM_METAL_MEM_CPU_MASK) | (1u << 1))

#define PM_METAL_MEM_ID_NONE  0u

typedef uint32_t pm_metal_mem_flags_t;
typedef uint32_t pm_metal_mem_id_t;

typedef enum {
  PM_METAL_MEM_SHARED_CLASS_64 = 0,
  PM_METAL_MEM_SHARED_CLASS_256,
  PM_METAL_MEM_SHARED_CLASS_1K,
  PM_METAL_MEM_SHARED_CLASS_4K,
  PM_METAL_MEM_SHARED_CLASS_COUNT
} pm_metal_mem_shared_class_t;

/**
  Init dual-span arena on claimed RAM, seed TLSF from the high end.
  n_cpus recorded for runners (no per-CPU heap split).
*/
/* impl: efi */
int pm_metal_mem_init (
  void     *arena,
  size_t    bytes,
  unsigned  n_cpus
  );

/* impl: efi */
void pm_metal_mem_set_cpu (unsigned cpu_id);

/** Current CPU id last passed to pm_metal_mem_set_cpu (0 before enter). */
/* impl: efi */
unsigned pm_metal_mem_cpu (void);

/**
  Allocate.
  HEAP/LOCAL/SHARED → TLSF (grows high brk as needed)
  MAP → page map (low brk)
  id → publish for pm_metal_mem_lookup (PM_METAL_MEM_ID_NONE = anonymous)
*/
/* impl: efi */
void *pm_metal_mem_alloc (
  size_t                size,
  pm_metal_mem_flags_t  where,
  pm_metal_mem_id_t     id
  );

/* impl: efi */
void *pm_metal_mem_lookup (pm_metal_mem_id_t id);
/* impl: efi */
void *pm_metal_mem_realloc (void *ptr, size_t size);
/* impl: efi */
void  pm_metal_mem_free (void *ptr);

/** Compat: class-sized heap alloc. */
/* impl: efi */
void *pm_metal_mem_shared_alloc_class (pm_metal_mem_shared_class_t cls);
/* impl: efi */
void  pm_metal_mem_shared_free (void *ptr);

/** Page map / unmap (LIFO unmap only). */
/* impl: efi */
void *pm_metal_mem_map (size_t bytes);
/* impl: efi */
int   pm_metal_mem_unmap (void *ptr, size_t bytes);

/* impl: efi */
size_t   pm_metal_mem_arena_bytes (void);
/* impl: efi */
size_t   pm_metal_mem_map_bytes (void);
/* impl: efi */
size_t   pm_metal_mem_heap_bytes (void);
/* impl: efi */
size_t   pm_metal_mem_hole_bytes (void);
/* impl: efi */
size_t   pm_metal_mem_local_bytes (void);   /* alias: heap used */
/* impl: efi */
size_t   pm_metal_mem_shared_bytes (void);  /* alias: map used */
/* impl: efi */
size_t   pm_metal_mem_os_bytes (void);      /* alias: hole */
/* impl: efi */
unsigned pm_metal_mem_n_cpus (void);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_MEM_H_ */
