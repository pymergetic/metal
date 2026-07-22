/** @file
  EFI TSC calibrate via Boot Services Stall (BSP only).
**/

#include <stdint.h>
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/CpuLib.h>
#include <Library/UefiBootServicesTableLib.h>

STATIC UINT64  mTscPerUsCached;

uint64_t
pm_metal_time_tsc_per_us_port (
  VOID
  )
{
  UINT64  a;
  UINT64  b;

  if (mTscPerUsCached != 0) {
    return mTscPerUsCached;
  }

  if (gBS != NULL) {
    a = AsmReadTsc ();
    (VOID)gBS->Stall (1000); /* 1 ms */
    b = AsmReadTsc ();
    if (b > a) {
      mTscPerUsCached = (b - a) / 1000u;
      return mTscPerUsCached;
    }
  }

  /*
   * Post-EBS: Stall is gone but TSC frequency is unchanged — do not
   * re-measure with a blind busy loop (QEMU TCG makes that lie badly).
   */
  return 2000; /* ~2 GHz fallback */
}
