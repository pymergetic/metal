/** @file
  Minimal spinlock, built on the shared CAS32 port primitive (see
  slot_table.h) — the one EDK2 (InterlockedCompareExchange32) touchpoint
  both already have a body for on efi and bios. Replaces every ad-hoc
  Library/SynchronizationLib AcquireSpinLock/ReleaseSpinLock use in
  Metal-side code; no new port files needed.

  Not reentrant, not fair — same tradeoffs the SPIN_LOCK it replaces had.
**/
#ifndef PM_METAL_RUNTIME_SLOT_SPIN_H
#define PM_METAL_RUNTIME_SLOT_SPIN_H

#include <stdint.h>

#include "slot_table.h"

typedef volatile uint32_t pm_metal_spin_t;

static inline void pm_metal_spin_init(pm_metal_spin_t *lock)
{
  *lock = 0;
}

static inline void pm_metal_spin_lock(pm_metal_spin_t *lock)
{
  while (pm_metal_slot_port_cas32(lock, 0, 1) != 0) {
    /* spin */
  }
}

static inline void pm_metal_spin_unlock(pm_metal_spin_t *lock)
{
  *lock = 0;
}

/* Full memory barrier (EDK2 MemoryFence) — plain compiler intrinsic, same
 * reasoning as runtime/time/cpu.h: not an EDK2 API, so no port-split. */
static inline void pm_metal_mem_fence(void)
{
  __sync_synchronize();
}

#endif /* PM_METAL_RUNTIME_SLOT_SPIN_H */
