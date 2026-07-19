/** @file
  Per-job coroutines with private map grant (bump alloc inside).
**/
#include "metal_coro.h"
#include "metal_run.h"

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>

#include "mem/metal_mem.h"

struct metal_coro {
  metal_coro_fn       fn;
  void               *region;
  size_t              region_bytes;
  size_t              bump;
  void               *state;
  void               *in;
  void               *out;
  metal_coro_status_t status;
};

STATIC
size_t
MetalAlign8 (
  size_t  n
  )
{
  return (n + 7u) & ~(size_t)7u;
}

metal_coro_t *
metal_coro_create (
  metal_coro_fn  fn,
  size_t         state_bytes,
  size_t         in_bytes,
  size_t         out_bytes,
  size_t         region_bytes
  )
{
  metal_coro_t  *c;
  size_t         need;
  UINT8         *cursor;

  if (fn == NULL) {
    return NULL;
  }

  need = MetalAlign8 (sizeof (metal_coro_t))
         + MetalAlign8 (state_bytes)
         + MetalAlign8 (in_bytes)
         + MetalAlign8 (out_bytes)
         + 64;
  if (region_bytes < need) {
    region_bytes = need;
  }

  /* Private grant from the map (low) side — not the TLSF heap. */
  cursor = (UINT8 *)metal_map (region_bytes);
  if (cursor == NULL) {
    return NULL;
  }

  c = (metal_coro_t *)(VOID *)cursor;
  ZeroMem (c, sizeof (*c));
  c->fn           = fn;
  c->region       = cursor;
  c->region_bytes = region_bytes;
  c->bump         = MetalAlign8 (sizeof (metal_coro_t));
  c->status       = METAL_CORO_READY;

  c->state = (state_bytes != 0) ? metal_coro_alloc (c, state_bytes) : NULL;
  c->in    = (in_bytes != 0) ? metal_coro_alloc (c, in_bytes) : NULL;
  c->out   = (out_bytes != 0) ? metal_coro_alloc (c, out_bytes) : NULL;

  if ((state_bytes != 0 && c->state == NULL)
      || (in_bytes != 0 && c->in == NULL)
      || (out_bytes != 0 && c->out == NULL))
  {
    return NULL;
  }

  return c;
}

void *
metal_coro_alloc (
  metal_coro_t  *coro,
  size_t         bytes
  )
{
  size_t  n;
  size_t  next;
  VOID   *p;

  if (coro == NULL || bytes == 0) {
    return NULL;
  }

  n    = MetalAlign8 (bytes);
  next = coro->bump + n;
  if (next > coro->region_bytes) {
    return NULL;
  }

  p          = (UINT8 *)coro->region + coro->bump;
  coro->bump = next;
  ZeroMem (p, n);
  return p;
}

metal_coro_status_t
metal_coro_resume (
  metal_coro_t  *coro
  )
{
  metal_coro_status_t  st;

  if (coro == NULL || coro->fn == NULL) {
    return METAL_CORO_ERROR;
  }

  if (coro->status == METAL_CORO_DONE || coro->status == METAL_CORO_ERROR) {
    return coro->status;
  }

  st = coro->fn (coro->state, coro->in, coro->out);
  coro->status = st;
  return st;
}

int
metal_coro_spawn (
  metal_coro_t  *coro,
  unsigned       cpu
  )
{
  if (coro == NULL) {
    return -1;
  }

  coro->status = METAL_CORO_READY;
  return metal_run_post_ex (cpu, METAL_MSG_CORO, 0, (uint64_t)(UINTN)(VOID *)coro);
}

metal_coro_status_t
metal_coro_status (
  metal_coro_t  *coro
  )
{
  return (coro != NULL) ? coro->status : METAL_CORO_ERROR;
}

void *
metal_coro_in (
  metal_coro_t  *coro
  )
{
  return (coro != NULL) ? coro->in : NULL;
}

void *
metal_coro_out (
  metal_coro_t  *coro
  )
{
  return (coro != NULL) ? coro->out : NULL;
}

void *
metal_coro_state (
  metal_coro_t  *coro
  )
{
  return (coro != NULL) ? coro->state : NULL;
}
