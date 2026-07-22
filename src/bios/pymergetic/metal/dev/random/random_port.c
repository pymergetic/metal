/** @file
  BIOS random port — no EFI RNG / RTC; shared uses weak + mono.
**/

#include <pymergetic/metal/dev/random/random.h>
#include <Uefi.h>

uint32_t
pm_metal_random_fill_port (
  VOID      *dest,
  uint32_t   len
  )
{
  (VOID)dest;
  (VOID)len;
  return 0;
}

uint64_t
pm_metal_random_realtime_ms_port (
  VOID
  )
{
  return 0;
}
