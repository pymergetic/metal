/** @file
  MP-safe time via TSC (Boot Services Stall/GetTime are not AP-safe).
**/
#include <time/time.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/CpuLib.h>
#include <Library/UefiBootServicesTableLib.h>

STATIC UINT64  mTscPerUs;

/**
  Calibrate once on the BSP before APs run. Safe to call again (no-op).
*/
void
pm_metal_time_init (
  VOID
  )
{
  UINT64  a;
  UINT64  b;

  if (mTscPerUs != 0 || gBS == NULL) {
    return;
  }

  a = AsmReadTsc ();
  (VOID)gBS->Stall (1000); /* 1 ms */
  b = AsmReadTsc ();
  if (b > a) {
    mTscPerUs = (b - a) / 1000u;
  }

  if (mTscPerUs == 0) {
    mTscPerUs = 2000; /* ~2 GHz fallback */
  }
}

void
pm_metal_time_usleep (
  uint32_t  us
  )
{
  UINT64  start;
  UINT64  target;

  if (us == 0) {
    return;
  }

  if (mTscPerUs == 0) {
    pm_metal_time_init ();
  }

  start  = AsmReadTsc ();
  target = start + (UINT64)us * mTscPerUs;
  while (AsmReadTsc () < target) {
    CpuPause ();
  }
}

void
pm_metal_time_msleep (
  uint32_t  ms
  )
{
  while (ms > 0) {
    uint32_t  chunk;

    chunk = (ms > 1000u) ? 1000u : ms;
    pm_metal_time_usleep (chunk * 1000u);
    ms -= chunk;
  }
}

void
pm_metal_time_sleep (
  uint32_t  sec
  )
{
  while (sec > 0) {
    uint32_t  chunk;

    chunk = (sec > 10u) ? 10u : sec;
    pm_metal_time_msleep (chunk * 1000u);
    sec -= chunk;
  }
}

uint64_t
pm_metal_time_mono_us (
  VOID
  )
{
  if (mTscPerUs == 0) {
    pm_metal_time_init ();
  }

  if (mTscPerUs == 0) {
    return 0;
  }

  return AsmReadTsc () / mTscPerUs;
}
