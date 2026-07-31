/** @file
  Input rings + focus + WASI natives (shared host)

  Focus routes HW: shell → ASCII/stdio; guest → HID key events.
  Sources without break codes (e.g. ConIn) use synthetic hold timeouts.
**/
#include <stddef.h>
#include <string.h>

#include <pymergetic/metal/dev/input/input.h>
#include <pymergetic/metal/dev/gfx/gfx.h>
#include <runtime/time/time.h>

#include <stdint.h>

#include "wasm_export.h"

/* Port: bios|efi dev/input/input_port.c */
void pm_metal_input_poll_port(void);

#define PM_METAL_INPUT_Q 64
#define PM_METAL_ASCII_Q 64
#define PM_METAL_HELD_N  256
/* Synthetic keyup budgets when the source has no break codes (e.g. ConIn). */
#define PM_METAL_INPUT_TAP_MS    90u
#define PM_METAL_INPUT_TURN_MS   70u
#define PM_METAL_INPUT_WALK_MS   600u
#define PM_METAL_INPUT_ACTION_MS 220u

static pm_metal_input_key_event_t mQ[PM_METAL_INPUT_Q];
static uint32_t                   mHead;
static uint32_t                   mTail;
static pm_metal_input_focus_t     mFocus;
static int32_t                    mPs2Trace;

static uint8_t                  mHeld[PM_METAL_HELD_N];
static uint64_t                 mHeldMs[PM_METAL_HELD_N];
static uint8_t                  mMods;
static pm_metal_input_filter_fn mFilter;

static pm_metal_input_pointer_t mPtrQ[PM_METAL_INPUT_Q];
static uint32_t                 mPtrHead;
static uint32_t                 mPtrTail;
static int32_t                  mPtrLocked;
static uint32_t                 mPtrLockSurf;
static int32_t                  mPtrX;
static int32_t                  mPtrY;
static uint32_t                 mPtrButtons;
static wasm_module_inst_t       mInputInst;
static char                     mAsciiQ[PM_METAL_ASCII_Q];
static uint32_t                 mAsciiHead;
static uint32_t                 mAsciiTail;

void pm_metal_input_ascii_push(char ch)
{
  uint32_t next;

  next = (mAsciiHead + 1u) % PM_METAL_ASCII_Q;
  if (next == mAsciiTail) {
    return;
  }

  mAsciiQ[mAsciiHead] = ch;
  mAsciiHead          = next;
}

static int32_t AsciiPop(char *ch)
{
  if (mAsciiHead == mAsciiTail) {
    return 0;
  }

  *ch        = mAsciiQ[mAsciiTail];
  mAsciiTail = (mAsciiTail + 1u) % PM_METAL_ASCII_Q;
  return 1;
}

void pm_metal_input_pointer_enqueue(const pm_metal_input_pointer_t *ev)
{
  uint32_t next;

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

void pm_metal_input_pointer_set_sample(int32_t x, int32_t y, uint32_t buttons)
{
  mPtrX       = x;
  mPtrY       = y;
  mPtrButtons = buttons;
}

static int32_t PtrAccelAxis(int32_t v)
{
  int32_t a;
  int32_t s;

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

void pm_metal_input_ptr_accel(int32_t *dx, int32_t *dy)
{
  if (dx != NULL) {
    *dx = PtrAccelAxis(*dx);
  }

  if (dy != NULL) {
    *dy = PtrAccelAxis(*dy);
  }
}

void pm_metal_input_pointer_rel(
  int32_t *x, int32_t *y, uint32_t *buttons, int32_t dx, int32_t dy, int32_t dz, int32_t dz_valid)
{
  int32_t                  gw;
  int32_t                  gh;
  int32_t                  nx;
  int32_t                  ny;
  uint32_t                 btns;
  int32_t                  wheel;
  pm_metal_input_pointer_t ev;

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
    pm_metal_input_ptr_accel(&dx, &dy);
  }

  gw = pm_metal_gfx_width();
  gh = pm_metal_gfx_height();
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

  memset(&ev, 0, sizeof(ev));
  ev.x       = (pm_metal_input_pointer_locked() != 0) ? -1 : nx;
  ev.y       = (pm_metal_input_pointer_locked() != 0) ? -1 : ny;
  ev.dx      = dx;
  ev.dy      = dy;
  ev.buttons = btns;
  ev.flags   = PM_METAL_INPUT_PTR_RELATIVE;
  if (dx != 0 || dy != 0 || btns != 0) {
    pm_metal_input_pointer_enqueue(&ev);
  }

  if (wheel != 0) {
    memset(&ev, 0, sizeof(ev));
    ev.x       = (pm_metal_input_pointer_locked() != 0) ? -1 : nx;
    ev.y       = (pm_metal_input_pointer_locked() != 0) ? -1 : ny;
    ev.dx      = 0;
    ev.dy      = wheel;
    ev.buttons = btns;
    ev.flags   = PM_METAL_INPUT_PTR_WHEEL;
    pm_metal_input_pointer_enqueue(&ev);
  }

  pm_metal_input_pointer_set_sample(nx, ny, btns);
}

/* HID letters used as movement in Metal guests (doom WASD). */
#define PM_METAL_KEY_W ((pm_metal_keycode_t)(PM_METAL_KEY_A + 22u))
#define PM_METAL_KEY_S ((pm_metal_keycode_t)(PM_METAL_KEY_A + 18u))
#define PM_METAL_KEY_D ((pm_metal_keycode_t)(PM_METAL_KEY_A + 3u))

static uint32_t MetalInputHoldMs(pm_metal_keycode_t code)
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

static int32_t MetalInputIsWalkKey(pm_metal_keycode_t code)
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

static int32_t MetalInputIsMoveOrAction(pm_metal_keycode_t code)
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

static void MetalInputUpdateMods(int32_t pressed, pm_metal_keycode_t code)
{
  uint8_t bit;

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
    mMods = (uint8_t)(mMods | bit);
  } else {
    mMods = (uint8_t)(mMods & (uint8_t)~bit);
  }
}

static void MetalInputEnqueue(int32_t pressed, pm_metal_keycode_t code)
{
  uint32_t                   next;
  pm_metal_input_key_event_t ev;

  if (code == PM_METAL_KEY_NONE) {
    return;
  }

  MetalInputUpdateMods(pressed, code);

  ev.code    = code;
  ev.pressed = pressed ? 1u : 0u;
  ev.mods    = mMods;
  if (mFilter != NULL && mFilter(&ev) != 0) {
    return;
  }

  next = (mHead + 1u) % PM_METAL_INPUT_Q;
  if (next == mTail) {
    return;
  }

  mQ[mHead] = ev;
  mHead     = next;
}

void pm_metal_input_set_filter(pm_metal_input_filter_fn fn)
{
  mFilter = fn;
}

void pm_metal_input_set_focus(pm_metal_input_focus_t focus)
{
  uint32_t i;

  mFocus = focus;
  if (mFocus == PM_METAL_INPUT_FOCUS_GUEST) {
    return;
  }

  for (i = 0; i < PM_METAL_HELD_N; i++) {
    if (mHeld[i]) {
      MetalInputEnqueue(0, (pm_metal_keycode_t)i);
      mHeld[i]   = 0;
      mHeldMs[i] = 0;
    }
  }

  mHead = 0;
  mTail = 0;
  mMods = 0;
}

uint8_t pm_metal_input_mod_state(void)
{
  return mMods;
}

pm_metal_input_focus_t pm_metal_input_focus(void)
{
  return mFocus;
}

void pm_metal_input_push_key(int32_t pressed, pm_metal_keycode_t code)
{
  MetalInputEnqueue(pressed, code);
}

void pm_metal_input_note_key(pm_metal_keycode_t code, uint64_t now_ms)
{
  uint32_t i;

  if (code == PM_METAL_KEY_NONE || code >= PM_METAL_HELD_N) {
    return;
  }

  /* Esc clears sticky movement so the player is never wedged. */
  if (code == PM_METAL_KEY_ESCAPE) {
    for (i = 1; i < PM_METAL_HELD_N; i++) {
      if (mHeld[i] && MetalInputIsMoveOrAction((pm_metal_keycode_t)i)) {
        MetalInputEnqueue(0, (pm_metal_keycode_t)i);
        mHeld[i]   = 0;
        mHeldMs[i] = 0;
      }
    }

    pm_metal_input_pointer_unlock();
  }

  /*
   * Any activity refreshes walk timers. Sources without typematic
   * (e.g. ConIn) would otherwise drop forward on a turn press.
   */
  for (i = 1; i < PM_METAL_HELD_N; i++) {
    if (mHeld[i] && MetalInputIsWalkKey((pm_metal_keycode_t)i)) {
      mHeldMs[i] = now_ms;
    }
  }

  if (!mHeld[code]) {
    MetalInputEnqueue(1, code);
    mHeld[code] = 1;
  }

  mHeldMs[code] = now_ms;
}

void pm_metal_input_set_held(pm_metal_keycode_t code, int held, uint64_t now_ms)
{
  if (code == PM_METAL_KEY_NONE || code >= PM_METAL_HELD_N) {
    return;
  }

  if (held) {
    pm_metal_input_note_key(code, now_ms);
    return;
  }

  if (mHeld[code]) {
    MetalInputEnqueue(0, code);
    mHeld[code]   = 0;
    mHeldMs[code] = 0;
  }
}

void pm_metal_input_tick(uint64_t now_ms)
{
  uint32_t i;

  if (mFocus != PM_METAL_INPUT_FOCUS_GUEST) {
    return;
  }

  for (i = 1; i < PM_METAL_HELD_N; i++) {
    uint64_t lim;

    if (!mHeld[i]) {
      continue;
    }

    lim = (uint64_t)MetalInputHoldMs((pm_metal_keycode_t)i);
    if (now_ms >= mHeldMs[i] + lim) {
      MetalInputEnqueue(0, (pm_metal_keycode_t)i);
      mHeld[i]   = 0;
      mHeldMs[i] = 0;
    }
  }
}

int32_t pm_metal_input_poll_key(int32_t *pressed, pm_metal_keycode_t *code)
{
  pm_metal_input_key_event_t ev;

  if (pm_metal_input_poll_key_event(&ev) == 0) {
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

int32_t pm_metal_input_poll_key_event(pm_metal_input_key_event_t *out)
{
  /*
   * Drain HW here — guests call this from session_pump / frame steps where
   * shell_poll (the other input_poll site) may not have run yet. Without
   * this, i8042 bytes sit unread while doom's I_GetEvent sees an empty ring.
   */
  pm_metal_input_poll_port();

  if (out == NULL || mHead == mTail) {
    return 0;
  }

  *out  = mQ[mTail];
  mTail = (mTail + 1u) % PM_METAL_INPUT_Q;
  return 1;
}

int32_t pm_metal_input_poll_pointer(pm_metal_input_pointer_t *out)
{
  pm_metal_input_poll_port();

  if (out == NULL || mPtrHead == mPtrTail) {
    return 0;
  }

  *out     = mPtrQ[mPtrTail];
  mPtrTail = (mPtrTail + 1u) % PM_METAL_INPUT_Q;
  return 1;
}

int32_t pm_metal_input_pointer_lock(uint32_t surface)
{
  if (surface != 0 && surface != PM_METAL_GFX_SURFACE_DEFAULT) {
    /* Tab surfaces OK once compositing lands; accept any non-zero for now. */
  }

  mPtrLocked   = 1;
  mPtrLockSurf = surface == 0 ? PM_METAL_GFX_SURFACE_DEFAULT : surface;
  return 0;
}

void pm_metal_input_pointer_unlock(void)
{
  mPtrLocked   = 0;
  mPtrLockSurf = 0;
}

int32_t pm_metal_input_pointer_locked(void)
{
  return mPtrLocked;
}

void pm_metal_input_bind_inst(void *module_inst)
{
  mInputInst = (wasm_module_inst_t)module_inst;
}

void pm_metal_input_pointer_sample(int32_t *x, int32_t *y, uint32_t *buttons)
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

uint32_t pm_metal_input_ps2_read(char *buf, uint32_t len)
{
  uint32_t n;

  if (buf == NULL || len == 0) {
    return 0;
  }

  /* Pop ASCII ring only. Port drain is pm_metal_input_poll(). */
  n = 0;
  while (n < len) {
    char ch;

    if (AsciiPop(&ch) == 0) {
      break;
    }

    buf[n++] = ch;
  }

  return n;
}

void pm_metal_input_poll(void)
{
  pm_metal_input_poll_port();
}

void pm_metal_input_ps2_trace_set(int32_t on)
{
  mPs2Trace = (on != 0) ? 1 : 0;
}

int32_t pm_metal_input_ps2_trace_get(void)
{
  return mPs2Trace;
}

static int32_t pm_metal_input_poll_key_native(wasm_exec_env_t exec_env,
                                              uint32_t        pressed_dest,
                                              uint32_t        code_dest)
{
  int32_t             pressed;
  pm_metal_keycode_t  code;
  int32_t            *pn;
  pm_metal_keycode_t *cn;

  (void)exec_env;
  if (mInputInst == NULL ||
      !wasm_runtime_validate_app_addr(mInputInst, pressed_dest, sizeof(int32_t)) ||
      !wasm_runtime_validate_app_addr(mInputInst, code_dest, sizeof(pm_metal_keycode_t))) {
    return 0;
  }

  if (pm_metal_input_poll_key(&pressed, &code) == 0) {
    return 0;
  }

  pn = (int32_t *)wasm_runtime_addr_app_to_native(mInputInst, pressed_dest);
  cn = (pm_metal_keycode_t *)wasm_runtime_addr_app_to_native(mInputInst, code_dest);
  if (pn == NULL || cn == NULL) {
    return 0;
  }

  *pn = pressed;
  *cn = code;
  return 1;
}

static int32_t pm_metal_input_poll_key_event_native(wasm_exec_env_t exec_env, uint32_t dest)
{
  pm_metal_input_key_event_t ev;
  void                      *native;

  (void)exec_env;
  if (mInputInst == NULL || !wasm_runtime_validate_app_addr(mInputInst, dest, sizeof(ev))) {
    return 0;
  }

  if (pm_metal_input_poll_key_event(&ev) == 0) {
    return 0;
  }

  native = wasm_runtime_addr_app_to_native(mInputInst, dest);
  if (native == NULL) {
    return 0;
  }

  memcpy(native, &ev, sizeof(ev));
  return 1;
}

static int32_t pm_metal_input_poll_pointer_native(wasm_exec_env_t exec_env, uint32_t dest)
{
  pm_metal_input_pointer_t ev;
  void                    *native;

  (void)exec_env;
  if (mInputInst == NULL || !wasm_runtime_validate_app_addr(mInputInst, dest, sizeof(ev))) {
    return 0;
  }

  if (pm_metal_input_poll_pointer(&ev) == 0) {
    return 0;
  }

  native = wasm_runtime_addr_app_to_native(mInputInst, dest);
  if (native == NULL) {
    return 0;
  }

  memcpy(native, &ev, sizeof(ev));
  return 1;
}

static int32_t pm_metal_input_pointer_lock_native(wasm_exec_env_t exec_env, uint32_t surface)
{
  (void)exec_env;
  return pm_metal_input_pointer_lock(surface);
}

static void pm_metal_input_pointer_unlock_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  pm_metal_input_pointer_unlock();
}

static int32_t pm_metal_input_pointer_locked_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_input_pointer_locked();
}

static NativeSymbol g_pm_metal_input_native_symbols[] = {
  { "pm_metal_input_poll_key", (void *)pm_metal_input_poll_key_native, "(ii)i", NULL },
  { "pm_metal_input_poll_key_event", (void *)pm_metal_input_poll_key_event_native, "(i)i", NULL },
  { "pm_metal_input_poll_pointer", (void *)pm_metal_input_poll_pointer_native, "(i)i", NULL },
  { "pm_metal_input_pointer_lock", (void *)pm_metal_input_pointer_lock_native, "(i)i", NULL },
  { "pm_metal_input_pointer_unlock", (void *)pm_metal_input_pointer_unlock_native, "()", NULL },
  { "pm_metal_input_pointer_locked", (void *)pm_metal_input_pointer_locked_native, "()i", NULL },
};

int pm_metal_input_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_INPUT_WASI_MODULE,
                                     g_pm_metal_input_native_symbols,
                                     sizeof(g_pm_metal_input_native_symbols) /
                                       sizeof(g_pm_metal_input_native_symbols[0]))) {
    return -1;
  }

  return 0;
}
