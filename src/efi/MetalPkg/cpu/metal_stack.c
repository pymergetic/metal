/** @file
  Per-CPU looper stacks — mapped from dual-span low brk.
**/
#include "metal_stack.h"

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>

#include "mem/metal_mem.h"

STATIC VOID     **mStackBase;
STATIC VOID     **mStackTop;
STATIC unsigned   mStackN;
STATIC INT32      mReady;

typedef struct {
  BASE_LIBRARY_JUMP_BUFFER  JumpBuffer;
  void                      (*Fn)(
    unsigned  cpu
    );
  unsigned                  Cpu;
} metal_stack_ctx_t;

STATIC
VOID
EFIAPI
MetalStackEntry (
  IN VOID  *Context1,
  IN VOID  *Context2
  )
{
  metal_stack_ctx_t  *Ctx;

  (VOID)Context2;
  Ctx = (metal_stack_ctx_t *)Context1;
  ASSERT (Ctx != NULL);
  ASSERT (Ctx->Fn != NULL);

  Ctx->Fn (Ctx->Cpu);
  LongJump (&Ctx->JumpBuffer, 1);
}

int
metal_stack_init (
  unsigned  n_cpus
  )
{
  unsigned  i;
  UINTN     bytes;

  if (n_cpus == 0) {
    return -1;
  }

  if (mReady) {
    return (mStackN == n_cpus) ? 0 : -1;
  }

  bytes = (UINTN)n_cpus * sizeof (VOID *);
  mStackBase = (VOID **)metal_alloc (bytes, METAL_MEM_HEAP, METAL_ID_NONE);
  mStackTop  = (VOID **)metal_alloc (bytes, METAL_MEM_HEAP, METAL_ID_NONE);
  if (mStackBase == NULL || mStackTop == NULL) {
    return -1;
  }

  ZeroMem (mStackBase, bytes);
  ZeroMem (mStackTop, bytes);

  for (i = 0; i < n_cpus; i++) {
    UINT8  *base;
    UINTN   top;

    base = (UINT8 *)metal_alloc (
                      METAL_STACK_BYTES,
                      METAL_MEM_MAP,
                      METAL_ID_STACK (i)
                      );
    if (base == NULL) {
      return -1;
    }

    top  = (UINTN)base + METAL_STACK_BYTES;
    top &= ~(UINTN)15;

    mStackBase[i] = base;
    mStackTop[i]  = (VOID *)top;
  }

  mStackN = n_cpus;
  mReady  = 1;
  return 0;
}

void
metal_stack_call (
  unsigned  cpu,
  void      (*fn)(unsigned cpu)
  )
{
  metal_stack_ctx_t  Ctx;

  ASSERT (mReady);
  ASSERT (fn != NULL);
  ASSERT (cpu < mStackN);
  ASSERT (mStackTop[cpu] != NULL);

  ZeroMem (&Ctx, sizeof (Ctx));
  Ctx.Fn  = fn;
  Ctx.Cpu = cpu;

  if (SetJump (&Ctx.JumpBuffer) == 0) {
    SwitchStack (
      MetalStackEntry,
      &Ctx,
      NULL,
      mStackTop[cpu]
      );
  }
}

size_t
metal_stack_bytes (
  VOID
  )
{
  return METAL_STACK_BYTES;
}

void *
metal_stack_base (
  unsigned  cpu
  )
{
  if (!mReady || cpu >= mStackN) {
    return NULL;
  }

  return mStackBase[cpu];
}

unsigned
metal_stack_n_cpus (
  VOID
  )
{
  return mStackN;
}
