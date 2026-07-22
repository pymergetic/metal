/** @file
  Shared TSC time — port supplies calibrate delay (PIT / Stall).
**/
#include <runtime/time/time.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/CpuLib.h>

/* Port: bios|efi runtime/time/time_port.c */
uint64_t pm_metal_time_tsc_per_us_port(void);

STATIC UINT64  mTscPerUs;

void
pm_metal_time_init (
  VOID
  )
{
  if (mTscPerUs != 0) {
    return;
  }

  mTscPerUs = pm_metal_time_tsc_per_us_port ();
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

void
pm_metal_time_recalibrate (
  VOID
  )
{
  /*
   * TSC ticks/us does not change across ExitBootServices. Re-running the
   * EFI port calibrator post-EBS only had a bogus CpuPause loop that
   * inflated pm_metal_time_msleep() under emulation.
   */
}
