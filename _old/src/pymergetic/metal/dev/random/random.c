/** @file
  Random fill + wall-clock — shared; port supplies strong entropy.
**/
#include <pymergetic/metal/dev/random/random.h>
#include <runtime/time/time.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "wasm_export.h"

/* Port: bios|efi dev/random/random_port.c */
uint32_t pm_metal_random_fill_port(void *dest, uint32_t len);
uint64_t pm_metal_random_realtime_ms_port(void);

static uint64_t mWeak;
static int32_t  mWeakSeeded;

static int32_t mWallOffsetValid;
static int64_t mWallOffsetMs;

/* Default: Europe/Berlin summer (+02:00). No DST engine. */
static int32_t mTzMin      = 120;
static char    mTzName[32] = "Europe/Berlin";

typedef struct {
  const char *name;
  int32_t     minutes;
} metal_tz_ent_t;

static const metal_tz_ent_t mTzTable[] = {
  { "UTC", 0 },
  { "GMT", 0 },
  { "Europe/Berlin", 120 },
  { "Europe/Paris", 120 },
  { "Europe/London", 60 },
  { "America/New_York", -240 },
  { "America/Los_Angeles", -420 },
  { "Asia/Tokyo", 540 },
};

uint32_t pm_metal_random(void *dest, uint32_t len)
{
  uint8_t *p;
  uint32_t i;
  uint32_t n;

  if (dest == NULL || len == 0) {
    return 0;
  }

  n = pm_metal_random_fill_port(dest, len);
  if (n == len) {
    return len;
  }

  /* Weak fallback: mix mono_us. Documented insecure. */
  if (!mWeakSeeded) {
    mWeak       = pm_metal_time_mono_us();
    mWeakSeeded = 1;
  }

  p = (uint8_t *)dest;
  for (i = 0; i < len; i++) {
    mWeak = mWeak * 6364136223846793005ULL + 1ULL;
    p[i]  = (uint8_t)(mWeak >> 33);
  }

  return len;
}

void pm_metal_realtime_set_unix_ms(uint64_t unix_ms)
{
  uint64_t mono_ms;

  mono_ms          = pm_metal_time_mono_us() / 1000u;
  mWallOffsetMs    = (int64_t)unix_ms - (int64_t)mono_ms;
  mWallOffsetValid = 1;
}

uint64_t pm_metal_realtime_ms(void)
{
  uint64_t ms;

  if (mWallOffsetValid) {
    return (uint64_t)((int64_t)(pm_metal_time_mono_us() / 1000u) + mWallOffsetMs);
  }

  ms = pm_metal_random_realtime_ms_port();
  if (ms != 0) {
    return ms;
  }

  return pm_metal_time_mono_us() / 1000u;
}

void pm_metal_tz_set_minutes(int32_t east_of_utc)
{
  mTzMin = east_of_utc;
  {
    int32_t abs_m;

    abs_m = (east_of_utc < 0) ? -east_of_utc : east_of_utc;
    snprintf(mTzName,
             sizeof(mTzName),
             "%c%02d%02d",
             (east_of_utc < 0) ? '-' : '+',
             abs_m / 60,
             abs_m % 60);
  }
}

int32_t pm_metal_tz_minutes(void)
{
  return mTzMin;
}

const char *pm_metal_tz_name(void)
{
  return mTzName;
}

static int32_t TzParseOffset(const char *spec, int32_t *out_min)
{
  const char *p;
  int32_t     sign;
  int32_t     hh;
  int32_t     mm;

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

int pm_metal_tz_set(const char *spec)
{
  uint32_t i;
  int32_t  mins;

  if (spec == NULL || spec[0] == '\0') {
    return -1;
  }

  if (TzParseOffset(spec, &mins) == 0) {
    mTzMin = mins;
    snprintf(mTzName, sizeof(mTzName), "%s", spec);
    return 0;
  }

  for (i = 0; i < sizeof(mTzTable) / sizeof(mTzTable[0]); i++) {
    if (strcmp(spec, mTzTable[i].name) == 0) {
      mTzMin = mTzTable[i].minutes;
      snprintf(mTzName, sizeof(mTzName), "%s", mTzTable[i].name);
      return 0;
    }
  }

  return -1;
}

uint64_t pm_metal_tz_local_ms(void)
{
  return pm_metal_realtime_ms() + (uint64_t)((int64_t)mTzMin * 60ll * 1000ll);
}

static uint32_t pm_metal_random_native(wasm_exec_env_t exec_env, uint32_t dest, uint32_t len)
{
  wasm_module_inst_t inst;
  void              *native;

  if (len == 0) {
    return 0;
  }

  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL) {
    return 0;
  }

  if (!wasm_runtime_validate_app_addr(inst, dest, len)) {
    return 0;
  }

  native = wasm_runtime_addr_app_to_native(inst, dest);
  if (native == NULL) {
    return 0;
  }

  return pm_metal_random(native, len);
}

static uint64_t pm_metal_realtime_ms_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_realtime_ms();
}

static NativeSymbol g_pm_metal_random_native_symbols[] = {
  { "pm_metal_random", (void *)pm_metal_random_native, "(ii)i", NULL },
  { "pm_metal_realtime_ms", (void *)pm_metal_realtime_ms_native, "()I", NULL },
};

int pm_metal_random_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_RANDOM_WASI_MODULE,
                                     g_pm_metal_random_native_symbols,
                                     sizeof(g_pm_metal_random_native_symbols) /
                                       sizeof(g_pm_metal_random_native_symbols[0]))) {
    return -1;
  }

  return 0;
}
