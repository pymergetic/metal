/** @file
  Per-CPU looper stacks — mapped from dual-span low brk. (impl: efi|bios)
**/
#include <runtime/stack/stack.h>
#include <pymergetic/metal/runtime/mem/mem.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void   **mStackBase;
static void   **mStackTop;
static unsigned mStackN;
static int32_t  mReady;

/* efi: SetJump/SwitchStack/LongJump (EDK2 BaseLib); bios: shim equivalent.
 * impl: src/{efi,bios}/pymergetic/metal/runtime/stack/stack_port.c */
void pm_metal_stack_port_switch(void (*fn)(unsigned cpu), unsigned cpu, void *new_stack_top);

int pm_metal_stack_init(unsigned n_cpus)
{
  unsigned  i;
  uintptr_t bytes;

  if (n_cpus == 0) {
    return -1;
  }

  if (mReady) {
    return (mStackN == n_cpus) ? 0 : -1;
  }

  bytes      = (uintptr_t)n_cpus * sizeof(void *);
  mStackBase = (void **)pm_metal_mem_alloc(bytes, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  mStackTop  = (void **)pm_metal_mem_alloc(bytes, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (mStackBase == NULL || mStackTop == NULL) {
    return -1;
  }

  memset(mStackBase, 0, bytes);
  memset(mStackTop, 0, bytes);

  for (i = 0; i < n_cpus; i++) {
    uint8_t  *base;
    uintptr_t top;

    base = (uint8_t *)pm_metal_mem_alloc(
      PM_METAL_STACK_BYTES, PM_METAL_MEM_MAP, PM_METAL_MEM_ID_STACK(i));
    if (base == NULL) {
      return -1;
    }

    top = (uintptr_t)base + PM_METAL_STACK_BYTES;
    top &= ~(uintptr_t)15;

    mStackBase[i] = base;
    mStackTop[i]  = (void *)top;
  }

  mStackN = n_cpus;
  mReady  = 1;
  return 0;
}

void pm_metal_stack_call(unsigned cpu, void (*fn)(unsigned cpu))
{
  assert(mReady);
  assert(fn != NULL);
  assert(cpu < mStackN);
  assert(mStackTop[cpu] != NULL);

  pm_metal_stack_port_switch(fn, cpu, mStackTop[cpu]);
}

size_t pm_metal_stack_bytes(void)
{
  return PM_METAL_STACK_BYTES;
}

void *pm_metal_stack_base(unsigned cpu)
{
  if (!mReady || cpu >= mStackN) {
    return NULL;
  }

  return mStackBase[cpu];
}

unsigned pm_metal_stack_n_cpus(void)
{
  return mStackN;
}
