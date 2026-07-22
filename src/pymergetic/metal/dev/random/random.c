/** @file
  Random fill + wall-clock — shared; port supplies strong entropy.
**/
#include <pymergetic/metal/dev/random/random.h>
#include <runtime/time/time.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>

#include "wasm_export.h"

/* Port: bios|efi dev/random/random_port.c */
uint32_t pm_metal_random_fill_port(void *dest, uint32_t len);
uint64_t pm_metal_random_realtime_ms_port(void);

STATIC wasm_module_inst_t  mRandInst;
STATIC UINT64              mWeak;
STATIC INT32               mWeakSeeded;

void
pm_metal_random_bind_inst (
  VOID  *module_inst
  )
{
  mRandInst = (wasm_module_inst_t)module_inst;
}

uint32_t
pm_metal_random (
  VOID      *dest,
  uint32_t   len
  )
{
  UINT8   *p;
  UINT32   i;
  UINT32   n;

  if (dest == NULL || len == 0) {
    return 0;
  }

  n = pm_metal_random_fill_port (dest, len);
  if (n == len) {
    return len;
  }

  /* Weak fallback: mix mono_us. Documented insecure. */
  if (!mWeakSeeded) {
    mWeak       = pm_metal_time_mono_us ();
    mWeakSeeded = 1;
  }

  p = (UINT8 *)dest;
  for (i = 0; i < len; i++) {
    mWeak = mWeak * 6364136223846793005ULL + 1ULL;
    p[i]  = (UINT8)(mWeak >> 33);
  }

  return len;
}

uint64_t
pm_metal_realtime_ms (
  VOID
  )
{
  UINT64  ms;

  ms = pm_metal_random_realtime_ms_port ();
  if (ms != 0) {
    return ms;
  }

  return pm_metal_time_mono_us () / 1000u;
}

STATIC UINT32
pm_metal_random_native (
  wasm_exec_env_t  exec_env,
  UINT32           dest,
  UINT32           len
  )
{
  VOID  *native;

  (VOID)exec_env;
  if (mRandInst == NULL || len == 0) {
    return 0;
  }

  if (!wasm_runtime_validate_app_addr (mRandInst, dest, len)) {
    return 0;
  }

  native = wasm_runtime_addr_app_to_native (mRandInst, dest);
  if (native == NULL) {
    return 0;
  }

  return pm_metal_random (native, len);
}

STATIC UINT64
pm_metal_realtime_ms_native (
  wasm_exec_env_t  exec_env
  )
{
  (VOID)exec_env;
  return pm_metal_realtime_ms ();
}

STATIC NativeSymbol g_pm_metal_random_native_symbols[] = {
  { "pm_metal_random", (VOID *)pm_metal_random_native, "(ii)i", NULL },
  { "pm_metal_realtime_ms", (VOID *)pm_metal_realtime_ms_native, "()I", NULL },
};

int
pm_metal_random_native_register (
  VOID
  )
{
  if (!wasm_runtime_register_natives (
         PM_METAL_RANDOM_WASI_MODULE,
         g_pm_metal_random_native_symbols,
         sizeof (g_pm_metal_random_native_symbols)
           / sizeof (g_pm_metal_random_native_symbols[0])
         ))
  {
    return -1;
  }

  return 0;
}
