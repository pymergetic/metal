/** @file
  EFI body for the one EDK2 primitive runtime/slot/slot_table.h needs.
**/

#include <stdint.h>

#include <Uefi.h>
#include <Library/SynchronizationLib.h>

uint32_t
pm_metal_slot_port_cas32 (
  volatile uint32_t  *v,
  uint32_t            cmp,
  uint32_t            x
  )
{
  return InterlockedCompareExchange32 ((volatile UINT32 *)v, cmp, x);
}

uint64_t
pm_metal_slot_port_cas64 (
  volatile uint64_t  *v,
  uint64_t            cmp,
  uint64_t            x
  )
{
  return InterlockedCompareExchange64 ((volatile UINT64 *)v, cmp, x);
}
