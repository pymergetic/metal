/** @file
  Surface focus / visibility events. (impl: efi|bios)
**/
#include <pymergetic/metal/shell/lifecycle/lifecycle.h>
#include <pymergetic/metal/dev/input/input.h>
#include <pymergetic/metal/dev/gfx/gfx.h>
#include <pymergetic/metal/dev/audio/audio_ops.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "wasm_export.h"

#define PM_METAL_LIFE_Q 16

static pm_metal_lifecycle_event_t mQ[PM_METAL_LIFE_Q];
static uint32_t                   mHead;
static uint32_t                   mTail;
static uint32_t                   mFocusSurf;
static uint32_t                   mFocusFlags;
static wasm_module_inst_t         mLifeInst;

void pm_metal_lifecycle_bind_inst(void *module_inst)
{
  mLifeInst = (wasm_module_inst_t)module_inst;
}

static void MetalLifeEnqueue(uint32_t surface, uint32_t flags)
{
  uint32_t next;

  next = (mHead + 1u) % PM_METAL_LIFE_Q;
  if (next == mTail) {
    return;
  }

  mQ[mHead].surface = surface;
  mQ[mHead].flags   = flags;
  mHead             = next;
}

void pm_metal_lifecycle_set(uint32_t surface, uint32_t flags)
{
  uint32_t surf;

  surf = surface == 0 ? PM_METAL_GFX_SURFACE_DEFAULT : surface;
  if (mFocusSurf == surf && mFocusFlags == flags) {
    return;
  }

  mFocusSurf  = surf;
  mFocusFlags = flags;
  MetalLifeEnqueue(surf, flags);

  if ((flags & PM_METAL_LIFE_FOCUSED) == 0) {
    pm_metal_input_pointer_unlock();
    pm_metal_audio_mute(1);
  } else {
    pm_metal_audio_mute(0);
  }
}

void pm_metal_lifecycle_blur(void)
{
  if (mFocusSurf == 0 && mFocusFlags == 0) {
    pm_metal_input_pointer_unlock();
    pm_metal_audio_mute(1);
    return;
  }

  MetalLifeEnqueue(mFocusSurf, PM_METAL_LIFE_CLOSING);
  mFocusSurf  = 0;
  mFocusFlags = 0;
  pm_metal_input_pointer_unlock();
  pm_metal_audio_mute(1);
}

int32_t pm_metal_lifecycle_poll(pm_metal_lifecycle_event_t *out)
{
  if (out == NULL || mHead == mTail) {
    return 0;
  }

  *out  = mQ[mTail];
  mTail = (mTail + 1u) % PM_METAL_LIFE_Q;
  return 1;
}

int32_t pm_metal_lifecycle_focused(uint32_t surface)
{
  uint32_t surf;

  surf = surface == 0 ? PM_METAL_GFX_SURFACE_DEFAULT : surface;
  return (mFocusSurf == surf && (mFocusFlags & PM_METAL_LIFE_FOCUSED) != 0) ? 1 : 0;
}

static int32_t pm_metal_lifecycle_poll_native(wasm_exec_env_t exec_env, uint32_t dest)
{
  pm_metal_lifecycle_event_t ev;
  void                      *native;

  (void)exec_env;
  if (mLifeInst == NULL || !wasm_runtime_validate_app_addr(mLifeInst, dest, sizeof(ev))) {
    return 0;
  }

  if (pm_metal_lifecycle_poll(&ev) == 0) {
    return 0;
  }

  native = wasm_runtime_addr_app_to_native(mLifeInst, dest);
  if (native == NULL) {
    return 0;
  }

  memcpy(native, &ev, sizeof(ev));
  return 1;
}

static int32_t pm_metal_lifecycle_focused_native(wasm_exec_env_t exec_env, uint32_t surface)
{
  (void)exec_env;
  return pm_metal_lifecycle_focused(surface);
}

static NativeSymbol g_pm_metal_lifecycle_native_symbols[] = {
  { "pm_metal_lifecycle_poll", (void *)pm_metal_lifecycle_poll_native, "(i)i", NULL },
  { "pm_metal_lifecycle_focused", (void *)pm_metal_lifecycle_focused_native, "(i)i", NULL },
};

int pm_metal_lifecycle_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_LIFECYCLE_WASI_MODULE,
                                     g_pm_metal_lifecycle_native_symbols,
                                     sizeof(g_pm_metal_lifecycle_native_symbols) /
                                       sizeof(g_pm_metal_lifecycle_native_symbols[0]))) {
    return -1;
  }

  return 0;
}
