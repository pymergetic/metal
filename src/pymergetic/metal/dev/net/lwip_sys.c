/** @file
  lwIP sys_now for Metal timers (NO_SYS). (impl: efi|bios)
**/
#include "lwipopts.h" /* IWYU pragma: keep */
#include <lwip/sys.h>
#include <runtime/time/time.h>

u32_t
sys_now (
  void
  )
{
  return (u32_t)(pm_metal_time_mono_us () / 1000ull);
}
