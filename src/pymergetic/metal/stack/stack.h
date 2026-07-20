/** @file
  Homogeneous per-CPU stacks (docs/COOP_MEMORY.md).
**/
#ifndef PM_METAL_STACK_H_
#define PM_METAL_STACK_H_

#include <pymergetic/metal/util/fourcc.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Published id for CPU k stack base (debug / lookup). */
#define PM_METAL_MEM_ID_STACK(k) \
  (PM_METAL_UTIL_FOURCC ('s', 't', 'k', 0) + (uint32_t)(k))

/* Same size on every CPU. */
#define PM_METAL_STACK_BYTES  (64u * 1024u)

/**
  Carve one MAP stack per CPU. Call after pm_metal_mem_init.
  Returns 0 on success.
*/
/* impl: efi */
int pm_metal_stack_init (unsigned n_cpus);

/**
  Run fn(cpu) on that CPU's Metal stack, then return on the caller's
  (EFI) stack. Uses SetJump / SwitchStack / LongJump.
*/
/* impl: efi */
void pm_metal_stack_call (
  unsigned  cpu,
  void      (*fn)(unsigned cpu)
  );

/** Diagnostics. */
/* impl: efi */
size_t   pm_metal_stack_bytes (void);
/* impl: efi */
void    *pm_metal_stack_base (unsigned cpu);
/* impl: efi */
unsigned pm_metal_stack_n_cpus (void);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_STACK_H_ */
