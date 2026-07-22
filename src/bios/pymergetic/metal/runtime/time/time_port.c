/** @file
  BIOS TSC calibrate via PIT channel 2.
**/

#include <stdint.h>
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/CpuLib.h>
#include <Library/IoLib.h>

STATIC VOID
PitDelayMs (
  UINT32  ms
  )
{
  /*
   * Channel 2, mode 0; gate via port 0x61.
   * Bounded waits — some laptops never raise OUT2.
   */
  UINT32  total;

  total = ms;
  while (total--) {
    UINT32  count;
    UINT32  spins;

    count = 1193; /* ~1ms at 1.193182 MHz */
    spins = 0;
    IoWrite8 (0x61, (UINT8)((IoRead8 (0x61) & ~0x02u) | 0x01u));
    IoWrite8 (0x43, 0xB0);
    IoWrite8 (0x42, (UINT8)(count & 0xff));
    IoWrite8 (0x42, (UINT8)(count >> 8));
    while ((IoRead8 (0x61) & 0x20u) == 0) {
      if (++spins > 2000000u) {
        return;
      }

      CpuPause ();
    }
  }
}

uint64_t
pm_metal_time_tsc_per_us_port (
  VOID
  )
{
  UINT64  a;
  UINT64  b;

  a = AsmReadTsc ();
  PitDelayMs (1);
  b = AsmReadTsc ();
  if (b > a) {
    return (b - a) / 1000u;
  }

  return 0;
}
