/** @file
  Random fill + wall-clock get. (impl: efi)
**/
#include <pymergetic/metal/random/random.h>
#include <time/time.h>

#include <Uefi.h>
#include <Protocol/Rng.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include "wasm_export.h"

STATIC wasm_module_inst_t     mRandInst;
STATIC EFI_RNG_PROTOCOL      *mRng;
STATIC INT32                  mRngProbed;
STATIC UINT64                 mWeak;

void
pm_metal_random_bind_inst (
  VOID  *module_inst
  )
{
  mRandInst = (wasm_module_inst_t)module_inst;
}

STATIC
VOID
MetalRandomProbe (
  VOID
  )
{
  EFI_STATUS  Status;

  if (mRngProbed || gBS == NULL) {
    return;
  }

  mRngProbed = 1;
  Status     = gBS->LocateProtocol (
                      &gEfiRngProtocolGuid,
                      NULL,
                      (VOID **)&mRng
                      );
  if (EFI_ERROR (Status)) {
    mRng = NULL;
  }

  mWeak = pm_metal_time_mono_us ();
}

uint32_t
pm_metal_random (
  VOID      *dest,
  uint32_t   len
  )
{
  UINT8   *p;
  UINT32   i;

  if (dest == NULL || len == 0) {
    return 0;
  }

  MetalRandomProbe ();
  if (mRng != NULL) {
    EFI_STATUS  Status;

    Status = mRng->GetRNG (mRng, NULL, (UINTN)len, (UINT8 *)dest);
    if (!EFI_ERROR (Status)) {
      return len;
    }
  }

  /* Weak fallback: mix mono_us. Documented insecure. */
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
  EFI_TIME  T;
  UINT64    days;
  UINT64    sec;

  if (gRT == NULL) {
    return 0;
  }

  if (EFI_ERROR (gRT->GetTime (&T, NULL))) {
    return 0;
  }

  /* Crude civil→epoch (good enough for wall get; not leap-second exact). */
  days = (UINT64)(T.Year - 1970u) * 365u
         + (UINT64)((T.Year - 1969u) / 4u)
         - (UINT64)((T.Year - 1901u) / 100u)
         + (UINT64)((T.Year - 1601u) / 400u);
  {
    STATIC CONST UINT16  mdays[] = {
      0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    UINTN  m;

    m = (T.Month >= 1 && T.Month <= 12) ? (UINTN)(T.Month - 1) : 0;
    days += mdays[m];
    if (T.Month > 2
        && ((T.Year % 4u) == 0)
        && (((T.Year % 100u) != 0) || ((T.Year % 400u) == 0)))
    {
      days += 1;
    }
  }

  days += (UINT64)(T.Day > 0 ? T.Day - 1 : 0);
  sec   = days * 86400ULL
          + (UINT64)T.Hour * 3600ULL
          + (UINT64)T.Minute * 60ULL
          + (UINT64)T.Second;
  return sec * 1000ULL + (UINT64)(T.Nanosecond / 1000000u);
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
