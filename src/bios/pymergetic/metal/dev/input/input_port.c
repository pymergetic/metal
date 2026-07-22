/** @file
  BIOS input port — soft i8042 drain into shared rings.
**/
#include <pymergetic/metal/dev/input/input.h>

#include <Uefi.h>
#include <Library/IoLib.h>

STATIC UINT8  mPs2ShiftDown;
STATIC UINT8  mPs2Ext;
STATIC INT32  mPs2Inited;

/*
 * Soft i8042 bring-up for real PCs after Multiboot/iPXE/VESA.
 *
 * Rules learned on ThinkPad T42:
 *  - Never leave the keyboard disabled (no AD without guaranteed AE).
 *  - Disable AUX — TrackPoint floods OBF and used to spin shell_poll forever.
 *  - Keep translation on so set-2 keyboards yield set-1 make codes.
 *  - Bounded waits only; partial success still marks inited (poll path works).
 *  - Drain into rings in poll_port; never INT 16 (RM bounce is the lag).
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
  (VOID)Ps2WriteCmd (0xA7u); /* disable AUX interface */
  Ps2FlushOut ();

  if (Ps2WriteCmd (0x20u) == 0 && Ps2WaitOutFull () == 0) {
    cfg = IoRead8 (0x60);
    cfg &= (UINT8)~0x10u; /* keyboard clock enabled */
    cfg |= 0x20u;         /* AUX clock disabled */
    cfg |= 0x40u;         /* translation → set-1 */
    cfg &= (UINT8)~0x03u; /* IRQ1/IRQ12 off — we poll */
    if (Ps2WriteCmd (0x60u) == 0) {
      (VOID)Ps2WriteData (cfg);
    }
  }

  (VOID)Ps2WriteCmd (0xAEu);

  /* Enable scanning; ignore timeout — many firmwares already scan. */
  if (Ps2WriteData (0xF4u) == 0 && Ps2WaitOutFull () == 0) {
    (VOID)IoRead8 (0x60);
  }

  Ps2FlushOut ();
  mPs2ShiftDown = 0;
  mPs2Ext       = 0;
  mPs2Inited    = 1;
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
   * Focus: app → Metal key ring; shell → ASCII ring (stdio feed).
   */
  for (budget = 0; budget < 64u; budget++) {
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
      mPs2Ext = 0;
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
  /* Drain i8042 into ASCII / key rings per focus. */
  I8042Drain ();
}

