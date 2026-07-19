/** @file
  Per-job stackless coroutines — private memory grant + spawn onto runloops.
**/
#ifndef METAL_CORO_H_
#define METAL_CORO_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  METAL_CORO_READY = 0,
  METAL_CORO_WAIT,
  METAL_CORO_DONE,
  METAL_CORO_ERROR
} metal_coro_status_t;

/**
  One step of a stackless coro.
  state/in/out live inside the job's private region.
*/
typedef metal_coro_status_t (*metal_coro_fn)(
  void  *state,
  void  *in,
  void  *out
  );

typedef struct metal_coro metal_coro_t;

/**
  Create a job: map a private region, place state/in/out inside it.
  region_bytes is the grant size (page-rounded). Returns NULL on OOM.
*/
metal_coro_t *metal_coro_create (
  metal_coro_fn  fn,
  size_t         state_bytes,
  size_t         in_bytes,
  size_t         out_bytes,
  size_t         region_bytes
  );

/** Bump-allocate inside the coro’s private region. */
void *metal_coro_alloc (
  metal_coro_t  *coro,
  size_t         bytes
  );

/** Run one step (or until DONE/WAIT/ERROR). */
metal_coro_status_t metal_coro_resume (
  metal_coro_t  *coro
  );

/** Post CORO message to cpu’s inbox (looper or another coro may call). */
int metal_coro_spawn (
  metal_coro_t  *coro,
  unsigned       cpu
  );

metal_coro_status_t metal_coro_status (metal_coro_t *coro);
void               *metal_coro_in (metal_coro_t *coro);
void               *metal_coro_out (metal_coro_t *coro);
void               *metal_coro_state (metal_coro_t *coro);

#ifdef __cplusplus
}
#endif

#endif /* METAL_CORO_H_ */
