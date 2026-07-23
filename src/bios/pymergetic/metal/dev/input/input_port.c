/** @file
  BIOS input port — soft i8042 drain into shared rings (kbd + AUX mouse).
**/
#include <pymergetic/metal/dev/input/input.h>
#include <pymergetic/metal/dev/input/virtio_input.h>
#include <pymergetic/metal/dev/gfx/gfx.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>

STATIC UINT8   mPs2ShiftDown;
STATIC UINT8   mPs2Ext;
STATIC INT32   mPs2Inited;
STATIC INT32   mAuxPktLen;     /* 3 or 4 after IMPS/2 probe */
STATIC UINT8   mAuxPkt[4];
STATIC UINT8   mAuxIdx;
STATIC INT32   mPtrX;
STATIC INT32   mPtrY;
STATIC UINT32  mPtrButtons;

/*
 * Soft i8042 bring-up for real PCs after Multiboot/iPXE/VESA.
 *
 * Rules learned on ThinkPad T42:
 *  - Never leave the keyboard disabled (no AD without guaranteed AE).
 *  - AUX was once disabled (TrackPoint OBF flood). Re-enabled with a hard
 *    byte budget + packet resync so storms cannot spin shell_poll.
 *  - Keep translation on so set-2 keyboards yield set-1 make codes.
 *  - Bounded waits only; partial success still marks inited.
 */

STATIC VOID
Ps2Delay (
  VOID
  )
{
  UINTN  i;

  for (i = 0; i < 200u; i++) {
    IoRead8 (0x80);
  }
}

STATIC INT32
Ps2WaitInEmpty (
  VOID
  )
{
  UINTN  i;

  for (i = 0; i < 10000u; i++) {
    if ((IoRead8 (0x64) & 0x02u) == 0) {
      return 0;
    }

    Ps2Delay ();
  }

  return -1;
}

STATIC INT32
Ps2WaitOutFull (
  VOID
  )
{
  UINTN  i;

  for (i = 0; i < 10000u; i++) {
    if ((IoRead8 (0x64) & 0x01u) != 0) {
      return 0;
    }

    Ps2Delay ();
  }

  return -1;
}

STATIC VOID
Ps2FlushOut (
  VOID
  )
{
  UINTN  i;

  for (i = 0; i < 64u; i++) {
    if ((IoRead8 (0x64) & 0x01u) == 0) {
      break;
    }

    (VOID)IoRead8 (0x60);
    Ps2Delay ();
  }
}

STATIC INT32
Ps2WriteCmd (
  UINT8  cmd
  )
{
  if (Ps2WaitInEmpty () != 0) {
    return -1;
  }

  IoWrite8 (0x64, cmd);
  return 0;
}

STATIC INT32
Ps2WriteData (
  UINT8  data
  )
{
  if (Ps2WaitInEmpty () != 0) {
    return -1;
  }

  IoWrite8 (0x60, data);
  return 0;
}

STATIC INT32
Ps2ReadByte (
  UINT8  *out
  )
{
  if (out == NULL || Ps2WaitOutFull () != 0) {
    return -1;
  }

  *out = IoRead8 (0x60);
  return 0;
}

STATIC INT32
Ps2WriteAux (
  UINT8  data
  )
{
  UINT8  ack;

  if (Ps2WriteCmd (0xD4u) != 0) {
    return -1;
  }

  if (Ps2WriteData (data) != 0) {
    return -1;
  }

  if (Ps2ReadByte (&ack) != 0 || ack != 0xFAu) {
    return -1;
  }

  return 0;
}

STATIC INT32
Ps2AuxSetSample (
  UINT8  rate
  )
{
  if (Ps2WriteAux (0xF3u) != 0) {
    return -1;
  }

  return Ps2WriteAux (rate);
}

STATIC
VOID
Ps2AuxProbeImps2 (
  VOID
  )
{
  UINT8  id;

  mAuxPktLen = 3;
  mAuxIdx    = 0;

  /* Magic sample rates → IntelliMouse ID 0x03 (or Explorer 0x04). */
  if (Ps2AuxSetSample (200) != 0
      || Ps2AuxSetSample (100) != 0
      || Ps2AuxSetSample (80) != 0)
  {
    return;
  }

  if (Ps2WriteAux (0xF2u) != 0) {
    return;
  }

  if (Ps2ReadByte (&id) != 0) {
    return;
  }

  if (id == 0x03u || id == 0x04u) {
    mAuxPktLen = 4;
  }
}

STATIC
VOID
Ps2AuxEmitPacket (
  VOID
  )
{
  INT32  dx;
  INT32  dy;
  INT32  dz;

  if ((mAuxPkt[0] & 0x08u) == 0) {
    /* Lost sync — drop first byte by shifting. */
    if (mAuxIdx > 0) {
      CopyMem (mAuxPkt, mAuxPkt + 1, mAuxIdx - 1u);
      mAuxIdx--;
    }

    return;
  }

  dx = (INT32)mAuxPkt[1];
  dy = (INT32)mAuxPkt[2];
  if ((mAuxPkt[0] & 0x10u) != 0) {
    dx |= ~0xFF;
  }

  if ((mAuxPkt[0] & 0x20u) != 0) {
    dy |= ~0xFF;
  }

  /* Overflow: keep saturated deltas (TrackPoint flicks used to drop). */
  (VOID)(mAuxPkt[0] & 0xC0u);

  /* PS/2 Y is opposite screen Y. */
  dy = -dy;

  mPtrButtons = 0;
  if ((mAuxPkt[0] & 0x01u) != 0) {
    mPtrButtons |= 1u;
  }

  if ((mAuxPkt[0] & 0x02u) != 0) {
    mPtrButtons |= 2u;
  }

  if ((mAuxPkt[0] & 0x04u) != 0) {
    mPtrButtons |= 4u;
  }

  dz = 0;
  if (mAuxPktLen >= 4) {
    dz = (INT8)mAuxPkt[3];
  }

  pm_metal_input_pointer_rel (
    &mPtrX,
    &mPtrY,
    &mPtrButtons,
    dx,
    dy,
    dz,
    (mAuxPktLen >= 4) ? 1 : 0
    );
  mAuxIdx = 0;
}

STATIC
VOID
Ps2AuxFeed (
  UINT8  b
  )
{
  if (mAuxIdx == 0 && (b & 0x08u) == 0) {
    return;
  }

  mAuxPkt[mAuxIdx++] = b;
  if (mAuxIdx >= (UINT8)mAuxPktLen) {
    Ps2AuxEmitPacket ();
  }
}

int
pm_metal_input_ps2_init (
  VOID
  )
{
  UINT8  cfg;

  if (mPs2Inited) {
    return 0;
  }

  /* Enable kbd first — never AD-then-fail (that bricked T42 keyboards). */
  (VOID)Ps2WriteCmd (0xAEu);
  Ps2FlushOut ();

  if (Ps2WriteCmd (0x20u) == 0 && Ps2WaitOutFull () == 0) {
    cfg = IoRead8 (0x60);
    cfg &= (UINT8)~0x10u; /* keyboard clock enabled */
    cfg &= (UINT8)~0x20u; /* AUX clock enabled */
    cfg |= 0x40u;         /* translation → set-1 */
    cfg &= (UINT8)~0x03u; /* IRQ1/IRQ12 off — we poll */
    if (Ps2WriteCmd (0x60u) == 0) {
      (VOID)Ps2WriteData (cfg);
    }
  }

  (VOID)Ps2WriteCmd (0xAEu);
  (VOID)Ps2WriteCmd (0xA8u); /* enable AUX interface */
  Ps2FlushOut ();

  /* Mouse defaults + IMPS/2 probe (falls back to 3-byte TrackPoint). */
  (VOID)Ps2WriteAux (0xF6u);
  Ps2FlushOut ();
  Ps2AuxProbeImps2 ();
  Ps2FlushOut ();
  if (mAuxPktLen < 4) {
    /* Probe may leave rates weird — reset + max resolution / sample. */
    (VOID)Ps2WriteAux (0xF6u);
    Ps2FlushOut ();
  }

  if (Ps2WriteAux (0xE8u) == 0) {
    (VOID)Ps2WriteAux (0x03u); /* 8 counts/mm */
  }

  (VOID)Ps2AuxSetSample (200);
  Ps2FlushOut ();
  (VOID)Ps2WriteAux (0xF4u); /* enable data reporting */
  Ps2FlushOut ();

  /* Enable keyboard scanning; ignore timeout — many firmwares already scan. */
  if (Ps2WriteData (0xF4u) == 0 && Ps2WaitOutFull () == 0) {
    (VOID)IoRead8 (0x60);
  }

  Ps2FlushOut ();
  mPs2ShiftDown = 0;
  mPs2Ext       = 0;
  mAuxIdx       = 0;
  {
    INT32  gw;
    INT32  gh;

    gw = pm_metal_gfx_width ();
    gh = pm_metal_gfx_height ();
    mPtrX = (gw > 0) ? (gw / 2) : 0;
    mPtrY = (gh > 0) ? (gh / 2) : 0;
  }
  mPtrButtons = 0;
  pm_metal_input_pointer_set_sample (mPtrX, mPtrY, mPtrButtons);
  mPs2Inited = 1;
  return 0;
}

STATIC VOID
I8042Drain (
  VOID
  )
{
  UINTN  budget;

  (VOID)pm_metal_input_ps2_init ();

  /*
   * Hard byte budget — AUX storms must not spin the coop pump.
   * Focus: app → Metal key ring; shell → ASCII ring (+ PageUp/Down keys).
   */
  /* Higher budget: TrackPoint floods; 64 truncated mid-packet → desync. */
  for (budget = 0; budget < 256u; budget++) {
    UINT8               st;
    UINT8               sc;
    INT32               break_code;
    INT32               ext;
    pm_metal_keycode_t  key;
    CHAR8               ch;

    st = IoRead8 (0x64);
    if ((st & 0x01u) == 0) {
      break;
    }

    sc = IoRead8 (0x60);
    if ((st & 0x20u) != 0) {
      if (pm_metal_input_virtio_tablet_ready () == 0) {
        Ps2AuxFeed (sc);
      }

      continue;
    }

    if (sc == 0xE0u) {
      mPs2Ext = 1;
      continue;
    }

    ext        = mPs2Ext;
    mPs2Ext    = 0;
    break_code = ((sc & 0x80u) != 0) ? 1 : 0;
    sc         = (UINT8)(sc & 0x7Fu);

    if (ext == 0 && (sc == 0x2Au || sc == 0x36u)) {
      mPs2ShiftDown = break_code ? 0 : 1;
    }

    key = pm_metal_input_keyb_hid (sc, ext);
    if (pm_metal_input_focus () == PM_METAL_INPUT_FOCUS_GUEST) {
      if (key != PM_METAL_KEY_NONE) {
        pm_metal_input_push_key (break_code ? 0 : 1, key);
      }

      continue;
    }

    /* Shell: track mods + nav keys for chrome (scroll / tab cycle). */
    if (key == PM_METAL_KEY_LCTRL || key == PM_METAL_KEY_RCTRL
        || key == PM_METAL_KEY_LSHIFT || key == PM_METAL_KEY_RSHIFT
        || key == PM_METAL_KEY_LALT || key == PM_METAL_KEY_RALT
        || key == PM_METAL_KEY_PAGEUP || key == PM_METAL_KEY_PAGEDOWN
        || key == PM_METAL_KEY_LEFT || key == PM_METAL_KEY_RIGHT)
    {
      pm_metal_input_push_key (break_code ? 0 : 1, key);
      continue;
    }

    if (break_code != 0 || ext != 0) {
      continue;
    }

    ch = pm_metal_input_keyb_ascii (sc, mPs2ShiftDown != 0);
    if (ch != 0) {
      pm_metal_input_ascii_push (ch);
    }
  }
}

void
pm_metal_input_poll_port (
  VOID
  )
{
  if (pm_metal_input_virtio_tablet_ready () != 0) {
    pm_metal_input_virtio_tablet_poll ();
  }

  I8042Drain ();
}
