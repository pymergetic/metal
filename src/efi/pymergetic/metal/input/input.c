/** @file
  Game key ring + pointer + lock + WASI natives. (impl: efi)

  EFI SimpleTextIn has no key-up events. Hold-keys stay down until a long idle
  after the last ConIn (covers missing VNC typematic); menu keys pulse short.
**/
#include <pymergetic/metal/input/input.h>
#include <pymergetic/metal/gfx/gfx.h>

#include <Uefi.h>
#include <Protocol/AbsolutePointer.h>
#include <Protocol/SimplePointer.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>
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

STATIC pm_metal_input_pointer_t  mPtrQ[PM_METAL_INPUT_Q];
STATIC UINT32                    mPtrHead;
STATIC UINT32                    mPtrTail;
STATIC INT32                     mPtrLocked;
STATIC UINT32                    mPtrLockSurf;
STATIC INT32                     mPtrX;
STATIC INT32                     mPtrY;
STATIC UINT32                    mPtrButtons;
STATIC INT32                     mPtrHaveAbs;
STATIC EFI_ABSOLUTE_POINTER_PROTOCOL  *mAbs;
STATIC EFI_SIMPLE_POINTER_PROTOCOL    *mSimple;
STATIC INT32                           mPtrProbed;
STATIC wasm_module_inst_t              mInputInst;
STATIC UINT8                           mPs2ShiftDown;
STATIC UINT8                           mPs2Ext;

STATIC
UINT16
MetalDoomToMetalKey (
  unsigned char  doom_key
  )
{
  switch (doom_key) {
    case 0x1b:
      return PM_METAL_KEY_ESCAPE;
    case 0xac:
      return PM_METAL_KEY_LEFT;
    case 0xae:
      return PM_METAL_KEY_RIGHT;
    case 0xad:
      return PM_METAL_KEY_UP;
    case 0xaf:
      return PM_METAL_KEY_DOWN;
    case ' ':
      return PM_METAL_KEY_SPACE;
    case '\r':
      return PM_METAL_KEY_ENTER;
    default:
      if (doom_key >= 'a' && doom_key <= 'z') {
        return (UINT16)(PM_METAL_KEY_A + (doom_key - 'a'));
      }

      if (doom_key >= 'A' && doom_key <= 'Z') {
        return (UINT16)(PM_METAL_KEY_A + (doom_key - 'A'));
      }

      return (UINT16)doom_key;
  }
}

STATIC
VOID
MetalPtrEnqueue (
  CONST pm_metal_input_pointer_t  *ev
  )
{
  UINT32  next;

  if (ev == NULL) {
    return;
  }

  next = (mPtrHead + 1u) % PM_METAL_INPUT_Q;
  if (next == mPtrTail) {
    return;
  }

  mPtrQ[mPtrHead] = *ev;
  mPtrHead        = next;
}

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

    pm_metal_input_pointer_unlock ();
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

int32_t
pm_metal_input_poll_key_event (
  pm_metal_input_key_event_t  *out
  )
{
  UINT16  v;

  if (out == NULL || mHead == mTail) {
    return 0;
  }

  v     = mQ[mTail];
  mTail = (mTail + 1u) % PM_METAL_INPUT_Q;
  out->pressed = (UINT8)((v >> 8) & 1u);
  out->mods    = 0;
  out->code    = MetalDoomToMetalKey ((unsigned char)(v & 0xffu));
  return 1;
}

int32_t
pm_metal_input_poll_pointer (
  pm_metal_input_pointer_t  *out
  )
{
  if (out == NULL || mPtrHead == mPtrTail) {
    return 0;
  }

  *out     = mPtrQ[mPtrTail];
  mPtrTail = (mPtrTail + 1u) % PM_METAL_INPUT_Q;
  return 1;
}

int32_t
pm_metal_input_pointer_lock (
  uint32_t  surface
  )
{
  if (surface != 0 && surface != PM_METAL_GFX_SURFACE_DEFAULT) {
    /* Tab surfaces OK once compositing lands; accept any non-zero for now. */
  }

  mPtrLocked   = 1;
  mPtrLockSurf = surface == 0 ? PM_METAL_GFX_SURFACE_DEFAULT : surface;
  return 0;
}

void
pm_metal_input_pointer_unlock (
  VOID
  )
{
  mPtrLocked   = 0;
  mPtrLockSurf = 0;
}

int32_t
pm_metal_input_pointer_locked (
  VOID
  )
{
  return mPtrLocked;
}

STATIC
VOID
MetalInputProbePointer (
  VOID
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  *Handles;
  UINTN       Count;

  if (mPtrProbed || gBS == NULL) {
    return;
  }

  mPtrProbed = 1;
  Handles    = NULL;
  Count      = 0;
  Status     = gBS->LocateHandleBuffer (
                      ByProtocol,
                      &gEfiAbsolutePointerProtocolGuid,
                      NULL,
                      &Count,
                      &Handles
                      );
  if (!EFI_ERROR (Status) && Count > 0 && Handles != NULL) {
    Status = gBS->HandleProtocol (
                    Handles[0],
                    &gEfiAbsolutePointerProtocolGuid,
                    (VOID **)&mAbs
                    );
    if (EFI_ERROR (Status)) {
      mAbs = NULL;
    }
  }

  if (Handles != NULL) {
    gBS->FreePool (Handles);
  }

  Handles = NULL;
  Count   = 0;
  Status  = gBS->LocateHandleBuffer (
                   ByProtocol,
                   &gEfiSimplePointerProtocolGuid,
                   NULL,
                   &Count,
                   &Handles
                   );
  if (!EFI_ERROR (Status) && Count > 0 && Handles != NULL) {
    Status = gBS->HandleProtocol (
                    Handles[0],
                    &gEfiSimplePointerProtocolGuid,
                    (VOID **)&mSimple
                    );
    if (EFI_ERROR (Status)) {
      mSimple = NULL;
    }
  }

  if (Handles != NULL) {
    gBS->FreePool (Handles);
  }
}

void
pm_metal_input_hw_poll (
  VOID
  )
{
  pm_metal_input_pointer_t  ev;
  INT32                     gw;
  INT32                     gh;

  MetalInputProbePointer ();
  ZeroMem (&ev, sizeof (ev));
  gw = pm_metal_gfx_width ();
  gh = pm_metal_gfx_height ();
  if (gw <= 0) {
    gw = 1;
  }

  if (gh <= 0) {
    gh = 1;
  }

  if (mAbs != NULL) {
    EFI_STATUS                 Status;
    EFI_ABSOLUTE_POINTER_STATE St;
    INT32                      nx;
    INT32                      ny;

    Status = mAbs->GetState (mAbs, &St);
    if (Status == EFI_SUCCESS) {
      if (mAbs->Mode->AbsoluteMaxX > mAbs->Mode->AbsoluteMinX) {
        nx = (INT32)(((St.CurrentX - mAbs->Mode->AbsoluteMinX)
                      * (UINT64)gw)
                     / (mAbs->Mode->AbsoluteMaxX - mAbs->Mode->AbsoluteMinX));
      } else {
        nx = mPtrX;
      }

      if (mAbs->Mode->AbsoluteMaxY > mAbs->Mode->AbsoluteMinY) {
        ny = (INT32)(((St.CurrentY - mAbs->Mode->AbsoluteMinY)
                      * (UINT64)gh)
                     / (mAbs->Mode->AbsoluteMaxY - mAbs->Mode->AbsoluteMinY));
      } else {
        ny = mPtrY;
      }

      if (nx < 0) {
        nx = 0;
      }

      if (ny < 0) {
        ny = 0;
      }

      if (nx >= gw) {
        nx = gw - 1;
      }

      if (ny >= gh) {
        ny = gh - 1;
      }

      ev.dx = mPtrHaveAbs ? (nx - mPtrX) : 0;
      ev.dy = mPtrHaveAbs ? (ny - mPtrY) : 0;
      mPtrX = nx;
      mPtrY = ny;
      mPtrHaveAbs = 1;
      ev.x  = nx;
      ev.y  = ny;
      ev.buttons = 0;
      if (St.ActiveButtons & EFI_ABSP_TouchActive) {
        ev.buttons |= 1u;
      }

      mPtrButtons = ev.buttons;
      ev.flags = mPtrLocked
                   ? PM_METAL_INPUT_PTR_RELATIVE
                   : PM_METAL_INPUT_PTR_ABSOLUTE;
      if (mPtrLocked) {
        ev.x = -1;
        ev.y = -1;
      }

      if (ev.dx != 0 || ev.dy != 0 || ev.buttons != 0) {
        MetalPtrEnqueue (&ev);
      }

      return;
    }
  }

  if (mSimple != NULL) {
    EFI_STATUS               Status;
    EFI_SIMPLE_POINTER_STATE St;

    Status = mSimple->GetState (mSimple, &St);
    if (Status == EFI_SUCCESS) {
      ev.dx = (INT32)St.RelativeMovementX;
      ev.dy = (INT32)St.RelativeMovementY;
      mPtrX += ev.dx;
      mPtrY += ev.dy;
      if (mPtrX < 0) {
        mPtrX = 0;
      }

      if (mPtrY < 0) {
        mPtrY = 0;
      }

      if (mPtrX >= gw) {
        mPtrX = gw - 1;
      }

      if (mPtrY >= gh) {
        mPtrY = gh - 1;
      }

      ev.x       = mPtrLocked ? -1 : mPtrX;
      ev.y       = mPtrLocked ? -1 : mPtrY;
      ev.buttons = 0;
      if (St.LeftButton) {
        ev.buttons |= 1u;
      }

      if (St.RightButton) {
        ev.buttons |= 2u;
      }

      mPtrButtons = ev.buttons;
      ev.flags    = PM_METAL_INPUT_PTR_RELATIVE;
      if (ev.dx != 0 || ev.dy != 0 || ev.buttons != 0) {
        MetalPtrEnqueue (&ev);
      }
    }
  }
}

void
pm_metal_input_bind_inst (
  VOID  *module_inst
  )
{
  mInputInst = (wasm_module_inst_t)module_inst;
}

void
pm_metal_input_pointer_sample (
  int32_t   *x,
  int32_t   *y,
  uint32_t  *buttons
  )
{
  if (x != NULL) {
    *x = mPtrX;
  }

  if (y != NULL) {
    *y = mPtrY;
  }

  if (buttons != NULL) {
    *buttons = mPtrButtons;
  }
}

/* Set-1 make → ASCII (unshifted / shifted). Unused slots are 0. */
STATIC CONST CHAR8  mPs2Unshift[0x80] = {
  [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
  [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
  [0x0C] = '-', [0x0D] = '=', [0x0E] = 0x08, [0x0F] = '\t',
  [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
  [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
  [0x1A] = '[', [0x1B] = ']', [0x1C] = '\r',
  [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
  [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = ';',
  [0x28] = '\'', [0x29] = '`', [0x2B] = '\\',
  [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
  [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
  [0x39] = ' ',
};

STATIC CONST CHAR8  mPs2ShiftMap[0x80] = {
  [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%',
  [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')',
  [0x0C] = '_', [0x0D] = '+', [0x0E] = 0x08, [0x0F] = '\t',
  [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T',
  [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
  [0x1A] = '{', [0x1B] = '}', [0x1C] = '\r',
  [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
  [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L', [0x27] = ':',
  [0x28] = '"', [0x29] = '~', [0x2B] = '|',
  [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B',
  [0x31] = 'N', [0x32] = 'M', [0x33] = ',', [0x34] = '.', [0x35] = '?',
  [0x39] = ' ',
};

uint32_t
pm_metal_input_ps2_read (
  char      *buf,
  uint32_t  len
  )
{
  UINT32  n;

  if (buf == NULL || len == 0) {
    return 0;
  }

  n = 0;
  while (n < len) {
    UINT8  st;
    UINT8  sc;
    CHAR8  ch;

    st = IoRead8 (0x64);
    if ((st & 0x01u) == 0) {
      break;
    }

    sc = IoRead8 (0x60);
    if (sc == 0xE0) {
      mPs2Ext = 1;
      continue;
    }

    if (sc == 0x2A || sc == 0x36) {
      mPs2ShiftDown = 1;
      mPs2Ext = 0;
      continue;
    }

    if (sc == 0xAA || sc == 0xB6) {
      mPs2ShiftDown = 0;
      mPs2Ext = 0;
      continue;
    }

    /* Ignore break codes and E0-prefixed (arrows etc.) for UI ASCII. */
    if ((sc & 0x80u) != 0 || mPs2Ext != 0) {
      mPs2Ext = 0;
      continue;
    }

    mPs2Ext = 0;
    ch = (mPs2ShiftDown != 0)
           ? mPs2ShiftMap[sc & 0x7Fu]
           : mPs2Unshift[sc & 0x7Fu];
    if (ch == 0) {
      continue;
    }

    buf[n++] = ch;
  }

  return n;
}

STATIC INT32
pm_metal_input_poll_key_packed_native (
  wasm_exec_env_t  exec_env
  )
{
  (VOID)exec_env;
  return pm_metal_input_poll_key_packed ();
}

STATIC INT32
pm_metal_input_poll_key_event_native (
  wasm_exec_env_t  exec_env,
  UINT32           dest
  )
{
  pm_metal_input_key_event_t  ev;
  VOID                       *native;

  (VOID)exec_env;
  if (mInputInst == NULL
      || !wasm_runtime_validate_app_addr (mInputInst, dest, sizeof (ev)))
  {
    return 0;
  }

  if (pm_metal_input_poll_key_event (&ev) == 0) {
    return 0;
  }

  native = wasm_runtime_addr_app_to_native (mInputInst, dest);
  if (native == NULL) {
    return 0;
  }

  CopyMem (native, &ev, sizeof (ev));
  return 1;
}

STATIC INT32
pm_metal_input_poll_pointer_native (
  wasm_exec_env_t  exec_env,
  UINT32           dest
  )
{
  pm_metal_input_pointer_t  ev;
  VOID                     *native;

  (VOID)exec_env;
  if (mInputInst == NULL
      || !wasm_runtime_validate_app_addr (mInputInst, dest, sizeof (ev)))
  {
    return 0;
  }

  if (pm_metal_input_poll_pointer (&ev) == 0) {
    return 0;
  }

  native = wasm_runtime_addr_app_to_native (mInputInst, dest);
  if (native == NULL) {
    return 0;
  }

  CopyMem (native, &ev, sizeof (ev));
  return 1;
}

STATIC INT32
pm_metal_input_pointer_lock_native (
  wasm_exec_env_t  exec_env,
  UINT32           surface
  )
{
  (VOID)exec_env;
  return pm_metal_input_pointer_lock (surface);
}

STATIC VOID
pm_metal_input_pointer_unlock_native (
  wasm_exec_env_t  exec_env
  )
{
  (VOID)exec_env;
  pm_metal_input_pointer_unlock ();
}

STATIC INT32
pm_metal_input_pointer_locked_native (
  wasm_exec_env_t  exec_env
  )
{
  (VOID)exec_env;
  return pm_metal_input_pointer_locked ();
}

STATIC NativeSymbol g_pm_metal_input_native_symbols[] = {
  { "pm_metal_input_poll_key_packed", (VOID *)pm_metal_input_poll_key_packed_native, "()i", NULL },
  { "pm_metal_input_poll_key_event", (VOID *)pm_metal_input_poll_key_event_native, "(i)i", NULL },
  { "pm_metal_input_poll_pointer", (VOID *)pm_metal_input_poll_pointer_native, "(i)i", NULL },
  { "pm_metal_input_pointer_lock", (VOID *)pm_metal_input_pointer_lock_native, "(i)i", NULL },
  { "pm_metal_input_pointer_unlock", (VOID *)pm_metal_input_pointer_unlock_native, "()", NULL },
  { "pm_metal_input_pointer_locked", (VOID *)pm_metal_input_pointer_locked_native, "()i", NULL },
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
