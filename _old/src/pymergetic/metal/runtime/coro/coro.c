/** @file
  Stackless coro core — alloc, close, await, resume.
**/
#include <runtime/coro/coro.h>
#include <pymergetic/metal/runtime/mem/mem.h>

#include <stddef.h>
#include <string.h>

pm_metal_coro_t *pm_metal_coro(pm_metal_coro_fn fn, size_t bytes)
{
  pm_metal_coro_t *c;

  if (fn == NULL || bytes < sizeof(pm_metal_coro_t)) {
    return NULL;
  }

  c = (pm_metal_coro_t *)pm_metal_mem_alloc(bytes, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (c == NULL) {
    return NULL;
  }

  memset(c, 0, bytes);
  c->fn     = fn;
  c->status = PM_METAL_PENDING;
  c->bytes  = bytes;
  return c;
}

void pm_metal_coro_close(pm_metal_coro_t *c)
{
  if (c == NULL) {
    return;
  }

  if (c->release != NULL) {
    c->release(c);
    c->release = NULL;
  }

  pm_metal_mem_free(c);
}

pm_metal_status_t pm_metal_await(pm_metal_coro_t *self, pm_metal_coro_t *aw)
{
  if (self == NULL || aw == NULL) {
    return PM_METAL_ERROR;
  }

  aw->owner      = self->owner;
  aw->waiter     = self;
  self->awaiting = aw;
  self->status   = PM_METAL_WAITING;
  return PM_METAL_WAITING;
}

pm_metal_status_t pm_metal_coro_resume(pm_metal_coro_t *c)
{
  pm_metal_coro_t  *leaf;
  pm_metal_status_t st;

  if (c == NULL || c->fn == NULL) {
    return PM_METAL_ERROR;
  }

  leaf = c;
  while (leaf->awaiting != NULL) {
    leaf = leaf->awaiting;
  }

  if (leaf->status == PM_METAL_DONE || leaf->status == PM_METAL_ERROR ||
      leaf->status == PM_METAL_CANCELLED) {
    return leaf->status;
  }

  st           = leaf->fn(leaf);
  leaf->status = st;
  return st;
}
