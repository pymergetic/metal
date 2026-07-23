/** @file
  Input rings + focus + WASI natives (shared host)

  Focus routes HW: shell → ASCII/stdio; guest → HID key events.
  Sources without break codes (e.g. ConIn) use synthetic hold timeouts.
**/
#include <pymergetic/metal/dev/input/input.h>
#include <pymergetic/metal/dev/gfx/gfx.h>
#include <runtime/time/time.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <stdint.h>

#include "wasm_export.h"

/* Port: bios|efi dev/input/input_port.c */
void pm_metal_input_poll_port(void);

#define PM_METAL_INPUT_Q  64
#define PM_METAL_ASCII_Q  64
#define PM_METAL_HELD_N   256
/* Synthetic keyup budgets when the source has no break codes (e.g. ConIn). */
#define PM_METAL_INPUT_TAP_MS    90u
#define PM_METAL_INPUT_TURN_MS   70u
#define PM_METAL_INPUT_WALK_MS   600u
#define PM_METAL_INPUT_ACTION_MS 220u

STATIC pm_metal_input_key_event_t  mQ[PM_METAL_INPUT_Q];
STATIC UINT32                      mHead;
STATIC UINT32                      mTail;
STATIC pm_metal_input_focus_t      mFocus;

STATIC UINT8                    mHeld[PM_METAL_HELD_N];
STATIC uint64_t                 mHeldMs[PM_METAL_HELD_N];
STATIC UINT8                    mMods;
STATIC pm_metal_input_filter_fn mFilter;

STATIC pm_metal_input_pointer_t  mPtrQ[PM_METAL_INPUT_Q];
STATIC UINT32                    mPtrHead;
STATIC UINT32                    mPtrTail;
STATIC INT32                     mPtrLocked;
STATIC UINT32                    mPtrLockSurf;
STATIC INT32                     mPtrX;
STATIC INT32                     mPtrY;
STATIC UINT32                    mPtrButtons;
STATIC wasm_module_inst_t              mInputInst;
STATIC CHAR8                           mAsciiQ[PM_METAL_ASCII_Q];
STATIC UINT32                          mAsciiHead;
STATIC UINT32                          mAsciiTail;

void
pm_metal_input_ascii_push (
  CHAR8  ch
  )
{
  UINT32  next;

  next = (mAsciiHead + 1u) % PM_METAL_ASCII_Q;
  if (next == mAsciiTail) {
    return;
  }

  mAsciiQ[mAsciiHead] = ch;
  mAsciiHead          = next;
}

STATIC INT32
AsciiPop (
  CHAR8  *ch
  )
{
  if (mAsciiHead == mAsciiTail) {
    return 0;
  }

  *ch        = mAsciiQ[mAsciiTail];
  mAsciiTail = (mAsciiTail + 1u) % PM_METAL_ASCII_Q;
  return 1;
}

void
pm_metal_input_pointer_enqueue (
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

void
pm_metal_input_pointer_set_sample (
  INT32   x,
  INT32   y,
  UINT32  buttons
  )
{
  mPtrX       = x;
  mPtrY       = y;
  mPtrButtons = buttons;
}

STATIC
INT32
PtrAccelAxis (
  INT32  v
  )
{
  INT32  a;
  INT32  s;

  if (v == 0) {
    return 0;
  }

  a = (v < 0) ? -v : v;
  s = (v < 0) ? -1 : 1;
  /* TrackPoint often sends ±1; without gain the cursor crawls. */
  if (a == 1) {
    return s * 3;
  }

  if (a == 2) {
    return s * 6;
  }

  if (a <= 4) {
    return v * 3;
  }

  if (a <= 8) {
    return v * 4;
  }

  if (a <= 16) {
    return v * 5;
  }

  return v * 6;
}

void
pm_metal_input_ptr_accel (
  INT32  *dx,
  INT32  *dy
  )
{
  if (dx != NULL) {
    *dx = PtrAccelAxis (*dx);
  }

  if (dy != NULL) {
    *dy = PtrAccelAxis (*dy);
  }
}

void
pm_metal_input_pointer_rel (
  INT32   *x,
  INT32   *y,
  UINT32  *buttons,
  INT32    dx,
  INT32    dy,
  INT32    dz,
  INT32    dz_valid
  )
{
  INT32                     gw;
  INT32                     gh;
  INT32                     nx;
  INT32                     ny;
  UINT32                    btns;
  INT32                     wheel;
  pm_metal_input_pointer_t  ev;

  if (x == NULL || y == NULL || buttons == NULL) {
    return;
  }

  btns  = *buttons;
  wheel = 0;
  if (dz_valid && dz != 0) {
    wheel = dz;
  }

  /*
   * ThinkPad TrackPoint: no IMPS/2 wheel — middle + drag scrolls.
   * Freeze cursor while middle is held so the stick is a scroll wheel.
   */
  if ((btns & 4u) != 0 && (dx != 0 || dy != 0)) {
    if (wheel == 0) {
      wheel = dy;
      if (wheel > 8) {
        wheel = 8;
      }

      if (wheel < -8) {
        wheel = -8;
      }

      if (wheel == 0) {
        wheel = (dy < 0) ? -1 : 1;
      }
    }

    dx = 0;
    dy = 0;
  } else {
    pm_metal_input_ptr_accel (&dx, &dy);
  }

  gw = pm_metal_gfx_width ();
  gh = pm_metal_gfx_height ();
  if (gw <= 0) {
    gw = 1;
  }

  if (gh <= 0) {
    gh = 1;
  }

  nx = *x + dx;
  ny = *y + dy;
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

  *x       = nx;
  *y       = ny;
  *buttons = btns;

  ZeroMem (&ev, sizeof (ev));
  ev.x       = (pm_metal_input_pointer_locked () != 0) ? -1 : nx;
  ev.y       = (pm_metal_input_pointer_locked () != 0) ? -1 : ny;
  ev.dx      = dx;
  ev.dy      = dy;
  ev.buttons = btns;
  ev.flags   = PM_METAL_INPUT_PTR_RELATIVE;
  if (dx != 0 || dy != 0 || btns != 0) {
    pm_metal_input_pointer_enqueue (&ev);
  }

  if (wheel != 0) {
    ZeroMem (&ev, sizeof (ev));
    ev.x       = (pm_metal_input_pointer_locked () != 0) ? -1 : nx;
    ev.y       = (pm_metal_input_pointer_locked () != 0) ? -1 : ny;
    ev.dx      = 0;
    ev.dy      = wheel;
    ev.buttons = btns;
    ev.flags   = PM_METAL_INPUT_PTR_WHEEL;
    pm_metal_input_pointer_enqueue (&ev);
  }

  pm_metal_input_pointer_set_sample (nx, ny, btns);
}

/* HID letters used as movement in Metal guests (doom WASD). */
#define PM_METAL_KEY_W  ((pm_metal_keycode_t)(PM_METAL_KEY_A + 22u))
#define PM_METAL_KEY_S  ((pm_metal_keycode_t)(PM_METAL_KEY_A + 18u))
#define PM_METAL_KEY_D  ((pm_metal_keycode_t)(PM_METAL_KEY_A + 3u))

STATIC
UINT32
MetalInputHoldMs (
  pm_metal_keycode_t  code
  )
{
  switch (code) {
    case PM_METAL_KEY_LEFT:
    case PM_METAL_KEY_RIGHT:
      return PM_METAL_INPUT_TURN_MS;
    case PM_METAL_KEY_UP:
    case PM_METAL_KEY_DOWN:
    case PM_METAL_KEY_W:
    case PM_METAL_KEY_A:
    case PM_METAL_KEY_S:
    case PM_METAL_KEY_D:
      return PM_METAL_INPUT_WALK_MS;
    case PM_METAL_KEY_LCTRL:
    case PM_METAL_KEY_RCTRL:
    case PM_METAL_KEY_LSHIFT:
    case PM_METAL_KEY_RSHIFT:
    case PM_METAL_KEY_LALT:
    case PM_METAL_KEY_RALT:
    case PM_METAL_KEY_SPACE:
      return PM_METAL_INPUT_ACTION_MS;
    default:
      return PM_METAL_INPUT_TAP_MS;
  }
}

STATIC
INT32
MetalInputIsWalkKey (
  pm_metal_keycode_t  code
  )
{
  switch (code) {
    case PM_METAL_KEY_UP:
    case PM_METAL_KEY_DOWN:
    case PM_METAL_KEY_W:
    case PM_METAL_KEY_A:
    case PM_METAL_KEY_S:
    case PM_METAL_KEY_D:
      return 1;
    default:
      return 0;
  }
}

STATIC
INT32
MetalInputIsMoveOrAction (
  pm_metal_keycode_t  code
  )
{
  switch (code) {
    case PM_METAL_KEY_LEFT:
    case PM_METAL_KEY_RIGHT:
    case PM_METAL_KEY_UP:
    case PM_METAL_KEY_DOWN:
    case PM_METAL_KEY_W:
    case PM_METAL_KEY_A:
    case PM_METAL_KEY_S:
    case PM_METAL_KEY_D:
    case PM_METAL_KEY_LCTRL:
    case PM_METAL_KEY_RCTRL:
    case PM_METAL_KEY_LSHIFT:
    case PM_METAL_KEY_RSHIFT:
    case PM_METAL_KEY_LALT:
    case PM_METAL_KEY_RALT:
    case PM_METAL_KEY_SPACE:
      return 1;
    default:
      return 0;
  }
}

STATIC
VOID
MetalInputUpdateMods (
  INT32               pressed,
  pm_metal_keycode_t  code
  )
{
  UINT8  bit;

  bit = 0;
  switch (code) {
    case PM_METAL_KEY_LCTRL:
    case PM_METAL_KEY_RCTRL:
      bit = PM_METAL_INPUT_MOD_CTRL;
      break;
    case PM_METAL_KEY_LSHIFT:
    case PM_METAL_KEY_RSHIFT:
      bit = PM_METAL_INPUT_MOD_SHIFT;
      break;
    case PM_METAL_KEY_LALT:
    case PM_METAL_KEY_RALT:
      bit = PM_METAL_INPUT_MOD_ALT;
      break;
    default:
      return;
  }

  if (pressed) {
    mMods = (UINT8)(mMods | bit);
  } else {
    mMods = (UINT8)(mMods & (UINT8)~bit);
  }
}

STATIC
VOID
MetalInputEnqueue (
  INT32               pressed,
  pm_metal_keycode_t  code
  )
{
  UINT32                      next;
  pm_metal_input_key_event_t  ev;

  if (code == PM_METAL_KEY_NONE) {
    return;
  }

  MetalInputUpdateMods (pressed, code);

  ev.code    = code;
  ev.pressed = pressed ? 1u : 0u;
  ev.mods    = mMods;
  if (mFilter != NULL && mFilter (&ev) != 0) {
    return;
  }

  next = (mHead + 1u) % PM_METAL_INPUT_Q;
  if (next == mTail) {
    return;
  }

  mQ[mHead] = ev;
  mHead     = next;
}

void
pm_metal_input_set_filter (
  pm_metal_input_filter_fn  fn
  )
{
  mFilter = fn;
}

void
pm_metal_input_set_focus (
  pm_metal_input_focus_t  focus
  )
{
  UINT32  i;

  mFocus = focus;
  if (mFocus == PM_METAL_INPUT_FOCUS_GUEST) {
    return;
  }

  for (i = 0; i < PM_METAL_HELD_N; i++) {
    if (mHeld[i]) {
      MetalInputEnqueue (0, (pm_metal_keycode_t)i);
      mHeld[i]   = 0;
      mHeldMs[i] = 0;
    }
  }

  mHead = 0;
  mTail = 0;
  mMods = 0;
}

pm_metal_input_focus_t
pm_metal_input_focus (
  VOID
  )
{
  return mFocus;
}

void
pm_metal_input_push_key (
  INT32               pressed,
  pm_metal_keycode_t  code
  )
{
  MetalInputEnqueue (pressed, code);
}

void
pm_metal_input_note_key (
  pm_metal_keycode_t  code,
  uint64_t            now_ms
  )
{
  UINT32  i;

  if (code == PM_METAL_KEY_NONE || code >= PM_METAL_HELD_N) {
    return;
  }

  /* Esc clears sticky movement so the player is never wedged. */
  if (code == PM_METAL_KEY_ESCAPE) {
    for (i = 1; i < PM_METAL_HELD_N; i++) {
      if (mHeld[i] && MetalInputIsMoveOrAction ((pm_metal_keycode_t)i)) {
        MetalInputEnqueue (0, (pm_metal_keycode_t)i);
        mHeld[i]   = 0;
        mHeldMs[i] = 0;
      }
    }

    pm_metal_input_pointer_unlock ();
  }

  /*
   * Any activity refreshes walk timers. Sources without typematic
   * (e.g. ConIn) would otherwise drop forward on a turn press.
   */
  for (i = 1; i < PM_METAL_HELD_N; i++) {
    if (mHeld[i] && MetalInputIsWalkKey ((pm_metal_keycode_t)i)) {
      mHeldMs[i] = now_ms;
    }
  }

  if (!mHeld[code]) {
    MetalInputEnqueue (1, code);
    mHeld[code] = 1;
  }

  mHeldMs[code] = now_ms;
}

void
pm_metal_input_set_held (
  pm_metal_keycode_t  code,
  int                 held,
  uint64_t            now_ms
  )
{
  if (code == PM_METAL_KEY_NONE || code >= PM_METAL_HELD_N) {
    return;
  }

  if (held) {
    pm_metal_input_note_key (code, now_ms);
    return;
  }

  if (mHeld[code]) {
    MetalInputEnqueue (0, code);
    mHeld[code]   = 0;
    mHeldMs[code] = 0;
  }
}

void
pm_metal_input_tick (
  uint64_t  now_ms
  )
{
  UINT32  i;

  if (mFocus != PM_METAL_INPUT_FOCUS_GUEST) {
    return;
  }

  for (i = 1; i < PM_METAL_HELD_N; i++) {
    uint64_t  lim;

    if (!mHeld[i]) {
      continue;
    }

    lim = (uint64_t)MetalInputHoldMs ((pm_metal_keycode_t)i);
    if (now_ms >= mHeldMs[i] + lim) {
      MetalInputEnqueue (0, (pm_metal_keycode_t)i);
      mHeld[i]   = 0;
      mHeldMs[i] = 0;
    }
  }
}

int32_t
pm_metal_input_poll_key (
  INT32               *pressed,
  pm_metal_keycode_t  *code
  )
{
  pm_metal_input_key_event_t  ev;

  if (pm_metal_input_poll_key_event (&ev) == 0) {
    return 0;
  }

  if (pressed != NULL) {
    *pressed = ev.pressed ? 1 : 0;
  }

  if (code != NULL) {
    *code = ev.code;
  }

  return 1;
}

int32_t
pm_metal_input_poll_key_event (
  pm_metal_input_key_event_t  *out
  )
{
  if (out == NULL || mHead == mTail) {
    return 0;
  }

  *out  = mQ[mTail];
  mTail = (mTail + 1u) % PM_METAL_INPUT_Q;
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

  /* Pop ASCII ring only. Port drain is pm_metal_input_poll(). */
  n = 0;
  while (n < len) {
    CHAR8  ch;

    if (AsciiPop (&ch) == 0) {
      break;
    }

    buf[n++] = ch;
  }

  return n;
}

void
pm_metal_input_poll (
  VOID
  )
{
  pm_metal_input_poll_port ();
}

STATIC INT32
pm_metal_input_poll_key_native (
  wasm_exec_env_t  exec_env,
  UINT32           pressed_dest,
  UINT32           code_dest
  )
{
  INT32               pressed;
  pm_metal_keycode_t  code;
  INT32              *pn;
  pm_metal_keycode_t *cn;

  (VOID)exec_env;
  if (mInputInst == NULL
      || !wasm_runtime_validate_app_addr (mInputInst, pressed_dest, sizeof (INT32))
      || !wasm_runtime_validate_app_addr (mInputInst, code_dest,
                                          sizeof (pm_metal_keycode_t)))
  {
    return 0;
  }

  if (pm_metal_input_poll_key (&pressed, &code) == 0) {
    return 0;
  }

  pn = (INT32 *)wasm_runtime_addr_app_to_native (mInputInst, pressed_dest);
  cn = (pm_metal_keycode_t *)wasm_runtime_addr_app_to_native (mInputInst, code_dest);
  if (pn == NULL || cn == NULL) {
    return 0;
  }

  *pn = pressed;
  *cn = code;
  return 1;
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
  { "pm_metal_input_poll_key", (VOID *)pm_metal_input_poll_key_native, "(ii)i", NULL },
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
