/** @file
  Game key ring + WASI natives. (impl: efi)

  EFI SimpleTextIn has no key-up events. Hold-keys stay down until a long idle
  after the last ConIn (covers missing VNC typematic); menu keys pulse short.
**/
#include <pymergetic/metal/input.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <stdint.h>

#include "wasm_export.h"

#define PM_METAL_INPUT_Q  64
/* Menu / digits — short synthetic keyup. */
#define PM_METAL_INPUT_TAP_MS    90u
/* Turn left/right — short pulse (long sticky = massive overshoot). */
#define PM_METAL_INPUT_TURN_MS   70u
/* Walk / strafe — longer so a hold works when VNC skips typematic. */
#define PM_METAL_INPUT_WALK_MS   600u
/* Fire / use / run — medium latch. */
#define PM_METAL_INPUT_ACTION_MS 220u

STATIC UINT16  mQ[PM_METAL_INPUT_Q];
STATIC UINT32  mHead;
STATIC UINT32  mTail;
STATIC INT32   mGameFocus;

STATIC UINT8    mHeld[256];
STATIC uint64_t mHeldMs[256];

STATIC
UINT32
MetalInputHoldMs (
  unsigned char  doom_key
  )
{
  switch (doom_key) {
    case 0xac: /* LEFT — turn */
    case 0xae: /* RIGHT — turn */
      return PM_METAL_INPUT_TURN_MS;
    case 0xad: /* UP — forward */
    case 0xaf: /* DOWN — back */
    case 0xa0: /* STRAFE_L */
    case 0xa1: /* STRAFE_R */
      return PM_METAL_INPUT_WALK_MS;
    case 0xa2: /* USE */
    case 0xa3: /* FIRE */
    case (0x80 + 0x36): /* RSHIFT */
    case (0x80 + 0x38): /* RALT */
      return PM_METAL_INPUT_ACTION_MS;
    default:
      return PM_METAL_INPUT_TAP_MS;
  }
}

STATIC
INT32
MetalInputIsWalkKey (
  unsigned char  doom_key
  )
{
  switch (doom_key) {
    case 0xad: /* UP */
    case 0xaf: /* DOWN */
    case 0xa0: /* STRAFE_L */
    case 0xa1: /* STRAFE_R */
      return 1;
    default:
      return 0;
  }
}

STATIC
INT32
MetalInputIsMoveOrAction (
  unsigned char  doom_key
  )
{
  switch (doom_key) {
    case 0xac:
    case 0xad:
    case 0xae:
    case 0xaf:
    case 0xa0:
    case 0xa1:
    case 0xa2:
    case 0xa3:
    case (0x80 + 0x36):
    case (0x80 + 0x38):
      return 1;
    default:
      return 0;
  }
}

STATIC
VOID
MetalInputEnqueue (
  INT32          pressed,
  unsigned char  doom_key
  )
{
  UINT32  next;

  if (doom_key == 0) {
    return;
  }

  next = (mHead + 1u) % PM_METAL_INPUT_Q;
  if (next == mTail) {
    return;
  }

  mQ[mHead] = (UINT16)(((pressed ? 1u : 0u) << 8) | (UINT16)doom_key);
  mHead     = next;
}

void
pm_metal_input_set_game_focus (
  INT32  on
  )
{
  UINT32  i;

  mGameFocus = on ? 1 : 0;
  if (mGameFocus) {
    return;
  }

  for (i = 0; i < 256u; i++) {
    if (mHeld[i]) {
      MetalInputEnqueue (0, (unsigned char)i);
      mHeld[i]   = 0;
      mHeldMs[i] = 0;
    }
  }

  mHead = 0;
  mTail = 0;
}

int
pm_metal_input_game_focus (
  VOID
  )
{
  return mGameFocus;
}

void
pm_metal_input_push_key (
  INT32          pressed,
  unsigned char  doom_key
  )
{
  MetalInputEnqueue (pressed, doom_key);
}

void
pm_metal_input_note_key (
  unsigned char  doom_key,
  uint64_t       now_ms
  )
{
  UINT32  i;

  if (doom_key == 0) {
    return;
  }

  /* Esc clears sticky movement so the player is never wedged. */
  if (doom_key == 0x1b) {
    for (i = 1; i < 256u; i++) {
      if (mHeld[i] && MetalInputIsMoveOrAction ((unsigned char)i)) {
        MetalInputEnqueue (0, (unsigned char)i);
        mHeld[i]   = 0;
        mHeldMs[i] = 0;
      }
    }
  }

  /*
   * Any ConIn activity refreshes walk timers. Without typematic, pressing
   * turn would otherwise let forward expire and cancel the walk.
   */
  for (i = 1; i < 256u; i++) {
    if (mHeld[i] && MetalInputIsWalkKey ((unsigned char)i)) {
      mHeldMs[i] = now_ms;
    }
  }

  if (!mHeld[doom_key]) {
    MetalInputEnqueue (1, doom_key);
    mHeld[doom_key] = 1;
  }

  mHeldMs[doom_key] = now_ms;
}

void
pm_metal_input_set_held (
  unsigned char  doom_key,
  int            held,
  uint64_t       now_ms
  )
{
  if (doom_key == 0) {
    return;
  }

  if (held) {
    pm_metal_input_note_key (doom_key, now_ms);
    return;
  }

  if (mHeld[doom_key]) {
    MetalInputEnqueue (0, doom_key);
    mHeld[doom_key]   = 0;
    mHeldMs[doom_key] = 0;
  }
}

void
pm_metal_input_tick (
  uint64_t  now_ms
  )
{
  UINT32  i;

  if (!mGameFocus) {
    return;
  }

  for (i = 1; i < 256u; i++) {
    uint64_t  lim;

    if (!mHeld[i]) {
      continue;
    }

    lim = (uint64_t)MetalInputHoldMs ((unsigned char)i);
    if (now_ms >= mHeldMs[i] + lim) {
      MetalInputEnqueue (0, (unsigned char)i);
      mHeld[i]   = 0;
      mHeldMs[i] = 0;
    }
  }
}

int32_t
pm_metal_input_poll_key (
  INT32  *pressed,
  INT32  *key
  )
{
  INT32  packed;

  packed = pm_metal_input_poll_key_packed ();
  if (packed == 0) {
    return 0;
  }

  if (pressed != NULL) {
    *pressed = (packed >> 8) & 1;
  }

  if (key != NULL) {
    *key = packed & 0xff;
  }

  return 1;
}

int32_t
pm_metal_input_poll_key_packed (
  VOID
  )
{
  UINT16  v;

  if (mHead == mTail) {
    return 0;
  }

  v     = mQ[mTail];
  mTail = (mTail + 1u) % PM_METAL_INPUT_Q;
  return (INT32)v == 0 ? 0x100 : (INT32)v;
}

STATIC INT32
pm_metal_input_poll_key_packed_native (
  wasm_exec_env_t  exec_env
  )
{
  (VOID)exec_env;
  return pm_metal_input_poll_key_packed ();
}

STATIC NativeSymbol g_pm_metal_input_native_symbols[] = {
  { "pm_metal_input_poll_key_packed", (VOID *)pm_metal_input_poll_key_packed_native, "()i", NULL },
};

int
pm_metal_input_native_register (
  VOID
  )
{
  if (!wasm_runtime_register_natives (
         PM_METAL_INPUT_WASI_MODULE,
         g_pm_metal_input_native_symbols,
         sizeof (g_pm_metal_input_native_symbols)
           / sizeof (g_pm_metal_input_native_symbols[0])
         ))
  {
    return -1;
  }

  return 0;
}
