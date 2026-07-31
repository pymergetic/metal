/** @file
  Generic "claim a free slot" ticket, shared by every fixed-size slot
  table in Metal (async handles in async.c, process ids in process.c,
  stream fds in stream.c, ...).

  Each table's slot struct starts with a `volatile uint32_t` tag: 0
  when free, non-zero while owned. pm_metal_slot_try_claim() CASes it
  from 0 to a caller-chosen non-zero value - that CAS is the only thing
  that decides ownership of index i, so two CPUs scanning at once can
  never both win the same free slot. Once claimed, the rest of the
  struct is exclusively the caller's until pm_metal_slot_release() (or
  a release path that already holds the only reference just zeroing
  the whole struct, tag included). pm_metal_slot_claimed_zero() zeroes
  a freshly claimed slot without racing the claim: it clears every byte
  after the tag, leaving the tag itself alone.

  Not a guarantee against every hazard (e.g. reading a slot while
  another CPU concurrently releases that same slot is still the owning
  subsystem's job) - just the "two CPUs claim the same free index"
  race every hand-rolled `for (i) if (!used) used = 1;` loop has once
  callers run on more than one CPU.

  Internal implementation header; nothing outside this runtime's own
  .c files should include it.
**/
#ifndef PM_METAL_RUNTIME_SLOT_SLOT_TABLE_H
#define PM_METAL_RUNTIME_SLOT_SLOT_TABLE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* impl: efi|bios — src/{efi,bios}/pymergetic/metal/runtime/slot/slot_table_port.c
 * the two EDK2 (InterlockedCompareExchange32/64) touchpoints this header
 * needs; bodies live per-target, never here. */
uint32_t pm_metal_slot_port_cas32(volatile uint32_t *v, uint32_t cmp, uint32_t x);
uint64_t pm_metal_slot_port_cas64(volatile uint64_t *v, uint64_t cmp, uint64_t x);

static inline bool pm_metal_slot_try_claim(volatile uint32_t *tag, uint32_t want_tag)
{
  return pm_metal_slot_port_cas32(tag, 0, want_tag) == 0;
}

static inline void pm_metal_slot_release(volatile uint32_t *tag)
{
  *tag = 0;
}

static inline void pm_metal_slot_claimed_zero(volatile uint32_t *tag, size_t slot_size)
{
  memset((uint8_t *)tag + sizeof(*tag), 0, slot_size - sizeof(*tag));
}

/* Atomic post-increment (EDK2 InterlockedIncrement), built on the same
 * cas32 port primitive — no separate EDK2 touchpoint needed. */
static inline uint32_t pm_metal_atomic_inc32(volatile uint32_t *v)
{
  uint32_t old;

  for (;;) {
    old = *v;
    if (pm_metal_slot_port_cas32(v, old, old + 1) == old) {
      return old + 1;
    }
  }
}

#endif /* PM_METAL_RUNTIME_SLOT_SLOT_TABLE_H */
