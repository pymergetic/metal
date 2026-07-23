/** @file
  Random fill + wall-clock — shared; port supplies strong entropy.
**/
#include <pymergetic/metal/dev/random/random.h>
#include <runtime/time/time.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>

#include "wasm_export.h"

/* Port: bios|efi dev/random/random_port.c */
uint32_t pm_metal_random_fill_port(void *dest, uint32_t len);
uint64_t pm_metal_random_realtime_ms_port(void);

STATIC wasm_module_inst_t  mRandInst;
STATIC UINT64              mWeak;
STATIC INT32               mWeakSeeded;

STATIC INT32               mWallOffsetValid;
STATIC INT64               mWallOffsetMs;

/* Default: Europe/Berlin summer (+02:00). No DST engine. */
STATIC INT32               mTzMin = 120;
STATIC CHAR8               mTzName[32] = "Europe/Berlin";

typedef struct {
  CONST CHAR8  *name;
  INT32         minutes;
} metal_tz_ent_t;

STATIC CONST metal_tz_ent_t  mTzTable[] = {
  { "UTC", 0 },
  { "GMT", 0 },
  { "Europe/Berlin", 120 },
  { "Europe/Paris", 120 },
  { "Europe/London", 60 },
  { "America/New_York", -240 },
  { "America/Los_Angeles", -420 },
  { "Asia/Tokyo", 540 },
};

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

void
pm_metal_realtime_set_unix_ms (
  uint64_t  unix_ms
  )
{
  UINT64  mono_ms;

  mono_ms         = pm_metal_time_mono_us () / 1000u;
  mWallOffsetMs   = (INT64)unix_ms - (INT64)mono_ms;
  mWallOffsetValid = 1;
}

uint64_t
pm_metal_realtime_ms (
  VOID
  )
{
  UINT64  ms;

  if (mWallOffsetValid) {
    return (uint64_t)((INT64)(pm_metal_time_mono_us () / 1000u) + mWallOffsetMs);
  }

  ms = pm_metal_random_realtime_ms_port ();
  if (ms != 0) {
    return ms;
  }

  return pm_metal_time_mono_us () / 1000u;
}

void
pm_metal_tz_set_minutes (
  int32_t  east_of_utc
  )
{
  mTzMin = east_of_utc;
  {
    INT32  abs_m;

    abs_m = (east_of_utc < 0) ? -east_of_utc : east_of_utc;
    AsciiSPrint (
      mTzName,
      sizeof (mTzName),
      "%c%02d%02d",
      (east_of_utc < 0) ? '-' : '+',
      abs_m / 60,
      abs_m % 60
      );
  }
}

int32_t
pm_metal_tz_minutes (
  VOID
  )
{
  return mTzMin;
}

CONST CHAR8 *
pm_metal_tz_name (
  VOID
  )
{
  return mTzName;
}

STATIC
INT32
TzParseOffset (
  CONST CHAR8  *spec,
  INT32        *out_min
  )
{
  CONST CHAR8  *p;
  INT32         sign;
  INT32         hh;
  INT32         mm;

  if (spec == NULL || out_min == NULL || spec[0] == '\0') {
    return -1;
  }

  p    = spec;
  sign = 1;
  if (*p == '+') {
    p++;
  } else if (*p == '-') {
    sign = -1;
    p++;
  }

  if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') {
    return -1;
  }

  hh = (p[0] - '0') * 10 + (p[1] - '0');
  p += 2;
  if (*p == ':') {
    p++;
  }

  if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9' || p[2] != '\0') {
    return -1;
  }

  mm = (p[0] - '0') * 10 + (p[1] - '0');
  if (hh > 14 || mm > 59) {
    return -1;
  }

  *out_min = sign * (hh * 60 + mm);
  return 0;
}

int
pm_metal_tz_set (
  CONST CHAR8  *spec
  )
{
  UINT32  i;
  INT32   mins;

  if (spec == NULL || spec[0] == '\0') {
    return -1;
  }

  if (TzParseOffset (spec, &mins) == 0) {
    mTzMin = mins;
    AsciiStrCpyS (mTzName, sizeof (mTzName), spec);
    return 0;
  }

  for (i = 0; i < sizeof (mTzTable) / sizeof (mTzTable[0]); i++) {
    if (AsciiStrCmp (spec, mTzTable[i].name) == 0) {
      mTzMin = mTzTable[i].minutes;
      AsciiStrCpyS (mTzName, sizeof (mTzName), mTzTable[i].name);
      return 0;
    }
  }

  return -1;
}

uint64_t
pm_metal_tz_local_ms (
  VOID
  )
{
  return pm_metal_realtime_ms () + (uint64_t)((INT64)mTzMin * 60ll * 1000ll);
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
