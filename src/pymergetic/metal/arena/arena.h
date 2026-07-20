/** @file
  Dual-span arena: pages grow up (mmap-like), heap grows down (TLSF pools).
**/
#ifndef PM_METAL_ARENA_H_
#define PM_METAL_ARENA_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* impl: efi */
int pm_metal_arena_init (
  void   *base,
  size_t  bytes
  );

/** Grow map_brk upward (page-aligned). NULL on OOM. */
/* impl: efi */
void *pm_metal_arena_map (
  size_t  bytes
  );

/**
  Shrink map only if ptr is the last map at the frontier (LIFO).
  Returns 0 on success, -1 if not LIFO / unknown.
*/
/* impl: efi */
int pm_metal_arena_unmap (
  void   *ptr,
  size_t  bytes
  );

/** Grow heap_brk downward (page-aligned). For TLSF pools. NULL on OOM. */
/* impl: efi */
void *pm_metal_arena_heap_grow (
  size_t  bytes
  );

/* impl: efi */
size_t pm_metal_arena_bytes (void);
/* impl: efi */
size_t pm_metal_arena_map_used (void);
/* impl: efi */
size_t pm_metal_arena_heap_used (void);
/* impl: efi */
size_t pm_metal_arena_hole (void);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_ARENA_H_ */
