/** @file
  EFI TSC calibrate via Boot Services Stall (BSP only, multi-sample).
**/

#include <stdint.h>
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/CpuLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define TSC_CAL_SAMPLES 8u

STATIC UINT64  mTscPerUsCached;
STATIC INT32   mForceRemeasure;

void
pm_metal_time_tsc_port_invalidate (
  VOID
  )
{
  mForceRemeasure = 1;
}

uint64_t
pm_metal_time_tsc_per_us_port (
  VOID
  )
{
  UINT64  sum;
  UINT32  i;
  UINT32  ok;

  if (mTscPerUsCached != 0 && mForceRemeasure == 0) {
    return mTscPerUsCached;
  }

  if (gBS == NULL) {
    /*
     * Post-EBS: Stall is gone but TSC frequency is unchanged — keep the
     * last multi-sample (or ~2 GHz fallback). Never busy-loop calibrate.
     */
    mForceRemeasure = 0;
    if (mTscPerUsCached != 0) {
      return mTscPerUsCached;
    }

    return 2000;
  }

  sum = 0;
  ok  = 0;
  for (i = 0; i < TSC_CAL_SAMPLES; i++) {
    UINT64  a;
    UINT64  b;

    a = AsmReadTsc ();
    (VOID)gBS->Stall (1000); /* 1 ms */
    b = AsmReadTsc ();
    if (b > a) {
      sum += (b - a) / 1000u;
      ok++;
    }
  }

  mForceRemeasure = 0;
  if (ok == 0) {
    if (mTscPerUsCached != 0) {
      return mTscPerUsCached;
    }

    return 2000;
  }

  mTscPerUsCached = sum / ok;
  return mTscPerUsCached;
}
