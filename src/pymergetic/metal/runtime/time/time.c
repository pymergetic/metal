/** @file
  Shared TSC time — port supplies calibrate delay (PIT / Stall).
**/
#include <runtime/time/time.h>
#include <runtime/time/cpu.h>

/* Port: bios|efi runtime/time/time_port.c */
uint64_t pm_metal_time_tsc_per_us_port(void);

static uint64_t mTscPerUs;

void pm_metal_time_init(void)
{
  if (mTscPerUs != 0) {
    return;
  }

  mTscPerUs = pm_metal_time_tsc_per_us_port();
  if (mTscPerUs == 0) {
    mTscPerUs = 2000; /* ~2 GHz fallback */
  }
}

void pm_metal_time_usleep(uint32_t us)
{
  uint64_t start;
  uint64_t target;

  if (us == 0) {
    return;
  }

  if (mTscPerUs == 0) {
    pm_metal_time_init();
  }

  start  = pm_metal_cpu_rdtsc();
  target = start + (uint64_t)us * mTscPerUs;
  while (pm_metal_cpu_rdtsc() < target) {
    pm_metal_cpu_pause();
  }
}

void pm_metal_time_msleep(uint32_t ms)
{
  while (ms > 0) {
    uint32_t chunk;

    chunk = (ms > 1000u) ? 1000u : ms;
    pm_metal_time_usleep(chunk * 1000u);
    ms -= chunk;
  }
}

void pm_metal_time_sleep(uint32_t sec)
{
  while (sec > 0) {
    uint32_t chunk;

    chunk = (sec > 10u) ? 10u : sec;
    pm_metal_time_msleep(chunk * 1000u);
    sec -= chunk;
  }
}

uint64_t pm_metal_time_mono_us(void)
{
  if (mTscPerUs == 0) {
    pm_metal_time_init();
  }

  if (mTscPerUs == 0) {
    return 0;
  }

  return pm_metal_cpu_rdtsc() / mTscPerUs;
}

void pm_metal_time_recalibrate(void)
{
  /*
   * TSC ticks/us does not change across ExitBootServices. Re-running the
   * EFI port calibrator post-EBS only had a bogus CpuPause loop that
   * inflated pm_metal_time_msleep() under emulation.
   */
}
