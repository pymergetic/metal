/** @file
  Homogeneous per-CPU stacks in LOCAL (docs/COOP_MEMORY.md).
**/
#ifndef METAL_STACK_H_
#define METAL_STACK_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Published id for CPU k stack base (debug / lookup). */
#define METAL_ID_STACK(k)  (0x4d544300u + (uint32_t)(k))

/* Same size on every CPU. */
#define METAL_STACK_BYTES  (64u * 1024u)

/**
  Carve one LOCAL/CPU(k) stack per CPU. Call after metal_mem_init.
  Returns 0 on success.
*/
int metal_stack_init (unsigned n_cpus);

/**
  Run fn(cpu) on that CPU's Metal stack, then return on the caller's
  (EFI) stack. Uses SetJump / SwitchStack / LongJump.
*/
void metal_stack_call (
  unsigned  cpu,
  void      (*fn)(unsigned cpu)
  );

/** Diagnostics. */
size_t   metal_stack_bytes (void);
void    *metal_stack_base (unsigned cpu);
unsigned metal_stack_n_cpus (void);

#ifdef __cplusplus
}
#endif

#endif /* METAL_STACK_H_ */
