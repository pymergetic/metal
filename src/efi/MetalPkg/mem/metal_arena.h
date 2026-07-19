/** @file
  Dual-span arena: pages grow up (mmap-like), heap grows down (TLSF pools).
**/
#ifndef METAL_ARENA_H_
#define METAL_ARENA_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int metal_arena_init (
  void   *base,
  size_t  bytes
  );

/** Grow map_brk upward (page-aligned). NULL on OOM. */
void *metal_arena_map (
  size_t  bytes
  );

/**
  Shrink map only if ptr is the last map at the frontier (LIFO).
  Returns 0 on success, -1 if not LIFO / unknown.
*/
int metal_arena_unmap (
  void   *ptr,
  size_t  bytes
  );

/** Grow heap_brk downward (page-aligned). For TLSF pools. NULL on OOM. */
void *metal_arena_heap_grow (
  size_t  bytes
  );

size_t metal_arena_bytes (void);
size_t metal_arena_map_used (void);
size_t metal_arena_heap_used (void);
size_t metal_arena_hole (void);

#ifdef __cplusplus
}
#endif

#endif /* METAL_ARENA_H_ */
