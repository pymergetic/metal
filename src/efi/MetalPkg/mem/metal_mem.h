/** @file
  Metal memory front end — dual-span arena + TLSF malloc + optional id publish.
  See docs/COOP_MEMORY.md
**/
#ifndef METAL_MEM_H_
#define METAL_MEM_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* where: HEAP (malloc/TLSF) is zero; MAP is page carve from low brk. */
#define METAL_MEM_LOCAL   0u          /* heap / TLSF (compat name) */
#define METAL_MEM_HEAP    METAL_MEM_LOCAL
#define METAL_MEM_SHARED  (1u << 0)   /* heap + publish-friendly (same pool) */
#define METAL_MEM_MAP     (1u << 2)   /* page map from dual-span low side */
#define METAL_MEM_CPU_MASK  0x0000ff00u
#define METAL_MEM_CPU_SHIFT 8
#define METAL_MEM_CPU(k)  ((((unsigned)(k) << METAL_MEM_CPU_SHIFT) & METAL_MEM_CPU_MASK) | (1u << 1))

#define METAL_ID_NONE  0u

typedef uint32_t metal_mem_flags_t;
typedef uint32_t metal_id_t;

typedef enum {
  METAL_SHARED_CLASS_64 = 0,
  METAL_SHARED_CLASS_256,
  METAL_SHARED_CLASS_1K,
  METAL_SHARED_CLASS_4K,
  METAL_SHARED_CLASS_COUNT
} metal_shared_class_t;

/**
  Init dual-span arena on claimed RAM, seed TLSF from the high end.
  n_cpus recorded for runners (no per-CPU heap split).
*/
int metal_mem_init (
  void     *arena,
  size_t    bytes,
  unsigned  n_cpus
  );

void metal_mem_set_cpu (unsigned cpu_id);

/**
  Allocate.
  HEAP/LOCAL/SHARED → TLSF (grows high brk as needed)
  MAP → page map (low brk)
  id → publish for metal_lookup (METAL_ID_NONE = anonymous)
*/
void *metal_alloc (
  size_t             size,
  metal_mem_flags_t  where,
  metal_id_t         id
  );

void *metal_lookup (metal_id_t id);
void *metal_realloc (void *ptr, size_t size);
void  metal_free (void *ptr);

/** Compat: class-sized heap alloc. */
void *metal_shared_alloc_class (metal_shared_class_t cls);
void  metal_shared_free (void *ptr);

/** Page map / unmap (LIFO unmap only). */
void *metal_map (size_t bytes);
int   metal_unmap (void *ptr, size_t bytes);

size_t   metal_mem_arena_bytes (void);
size_t   metal_mem_map_bytes (void);
size_t   metal_mem_heap_bytes (void);
size_t   metal_mem_hole_bytes (void);
size_t   metal_mem_local_bytes (void);   /* alias: heap used */
size_t   metal_mem_shared_bytes (void);  /* alias: 0 — no separate SHARED slab */
size_t   metal_mem_os_bytes (void);      /* alias: hole */
unsigned metal_mem_n_cpus (void);

#ifdef __cplusplus
}
#endif

#endif /* METAL_MEM_H_ */
