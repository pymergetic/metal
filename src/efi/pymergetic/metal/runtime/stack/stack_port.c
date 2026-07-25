/** @file
  EFI body for the one EDK2 primitive stack.c needs: switching onto a new
  stack and calling back, via SetJump/SwitchStack/LongJump (EDK2 BaseLib).
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>

typedef struct {
  BASE_LIBRARY_JUMP_BUFFER JumpBuffer;
  void (*Fn)(unsigned cpu);
  unsigned Cpu;
} pm_metal_stack_ctx_t;

static VOID EFIAPI MetalStackEntry(IN VOID *Context1, IN VOID *Context2)
{
  pm_metal_stack_ctx_t *Ctx;

  (VOID)Context2;
  Ctx = (pm_metal_stack_ctx_t *)Context1;
  ASSERT(Ctx != NULL);
  ASSERT(Ctx->Fn != NULL);

  Ctx->Fn(Ctx->Cpu);
  LongJump(&Ctx->JumpBuffer, 1);
}

void pm_metal_stack_port_switch(void (*fn)(unsigned cpu), unsigned cpu, void *new_stack_top)
{
  pm_metal_stack_ctx_t Ctx;

  ZeroMem(&Ctx, sizeof(Ctx));
  Ctx.Fn  = fn;
  Ctx.Cpu = cpu;

  if (SetJump(&Ctx.JumpBuffer) == 0) {
    SwitchStack(MetalStackEntry, &Ctx, NULL, new_stack_top);
  }
}
