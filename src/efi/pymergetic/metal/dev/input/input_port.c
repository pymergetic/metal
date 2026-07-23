/** @file
  EFI input port — ConIn/Absolute/Simple pre-EBS; i8042 kbd+AUX post-owned.
**/
#include <pymergetic/metal/dev/input/input.h>
#include <pymergetic/metal/dev/input/virtio_input.h>
#include <pymergetic/metal/dev/gfx/gfx.h>
#include <pymergetic/metal/boot/port.h>
#include <runtime/time/time.h>

#include <Uefi.h>
#include <Protocol/AbsolutePointer.h>
#include <Protocol/SimplePointer.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>

STATIC INT32                             mPtrHaveAbs;
STATIC EFI_ABSOLUTE_POINTER_PROTOCOL    *mAbs;
STATIC EFI_SIMPLE_POINTER_PROTOCOL      *mSimple;
STATIC INT32                             mPtrProbed;
STATIC UINT8                             mPs2ShiftDown;
STATIC UINT8                             mPs2Ext;
STATIC INT32                             mPs2Inited;
STATIC INT32                             mAuxPktLen; /* 3 or 4 after IMPS/2 */
STATIC UINT8                             mAuxPkt[4];
STATIC UINT8                             mAuxIdx;
STATIC INT32                             mPtrX;
STATIC INT32                             mPtrY;
STATIC UINT32                            mPtrButtons;

STATIC VOID MetalInputI8042Drain (VOID);
STATIC VOID MetalInputConInDrain (VOID);

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

STATIC VOID
Ps2AuxProbeImps2 (
  VOID
  )
{
  UINT8  id;

  mAuxPktLen = 3;
  mAuxIdx    = 0;

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

STATIC VOID
Ps2AuxEmitPacket (
  VOID
  )
{
  INT32  dx;
  INT32  dy;
  INT32  dz;

  if ((mAuxPkt[0] & 0x08u) == 0) {
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

  /* Overflow: keep saturated deltas (don't drop TrackPoint flicks). */
  (VOID)(mAuxPkt[0] & 0xC0u);

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

STATIC VOID
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

/*
 * Post-owned i8042: kbd + AUX mouse. Pre-owned uses ConIn + EFI pointer.
 */
STATIC VOID
MetalInputI8042Drain (
  VOID
  )
{
  UINTN  budget;

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
      /* Tablet owns absolute pointer — ignore PS/2 AUX deltas. */
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

STATIC VOID
MetalInputConInDrain (
  VOID
  )
{
  EFI_STATUS     Status;
  EFI_INPUT_KEY  Key;

  if (pm_metal_port_owned () || gST == NULL || gST->ConIn == NULL || gBS == NULL) {
    return;
  }

  for (;;) {
    Status = gBS->CheckEvent (gST->ConIn->WaitForKey);
    if (Status != EFI_SUCCESS) {
      break;
    }

    Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
    if (Status != EFI_SUCCESS) {
      break;
    }

    if (pm_metal_input_focus () == PM_METAL_INPUT_FOCUS_GUEST) {
      pm_metal_keycode_t  code;
      UINT64              now_ms;

      code = PM_METAL_KEY_NONE;
      if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN || Key.UnicodeChar == CHAR_LINEFEED) {
        code = PM_METAL_KEY_ENTER;
      } else if (Key.UnicodeChar == CHAR_BACKSPACE) {
        code = PM_METAL_KEY_BACKSPACE;
      } else if (Key.UnicodeChar == CHAR_TAB) {
        code = PM_METAL_KEY_TAB;
      } else if (Key.UnicodeChar == L' ') {
        code = PM_METAL_KEY_SPACE;
      } else if (Key.UnicodeChar >= L'a' && Key.UnicodeChar <= L'z') {
        code = (pm_metal_keycode_t)(PM_METAL_KEY_A + (Key.UnicodeChar - L'a'));
      } else if (Key.UnicodeChar >= L'A' && Key.UnicodeChar <= L'Z') {
        code = (pm_metal_keycode_t)(PM_METAL_KEY_A + (Key.UnicodeChar - L'A'));
      } else if (Key.ScanCode == SCAN_ESC) {
        code = PM_METAL_KEY_ESCAPE;
      } else if (Key.ScanCode == SCAN_UP) {
        code = PM_METAL_KEY_UP;
      } else if (Key.ScanCode == SCAN_DOWN) {
        code = PM_METAL_KEY_DOWN;
      } else if (Key.ScanCode == SCAN_LEFT) {
        code = PM_METAL_KEY_LEFT;
      } else if (Key.ScanCode == SCAN_RIGHT) {
        code = PM_METAL_KEY_RIGHT;
      } else if (Key.ScanCode == SCAN_PAGE_UP) {
        code = PM_METAL_KEY_PAGEUP;
      } else if (Key.ScanCode == SCAN_PAGE_DOWN) {
        code = PM_METAL_KEY_PAGEDOWN;
      }

      if (code != PM_METAL_KEY_NONE) {
        now_ms = pm_metal_time_mono_us () / 1000u;
        pm_metal_input_note_key (code, now_ms);
      }

      continue;
    }

    if (Key.ScanCode == SCAN_PAGE_UP) {
      pm_metal_input_push_key (1, PM_METAL_KEY_PAGEUP);
      continue;
    }

    if (Key.ScanCode == SCAN_PAGE_DOWN) {
      pm_metal_input_push_key (1, PM_METAL_KEY_PAGEDOWN);
      continue;
    }

    if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN || Key.UnicodeChar == CHAR_LINEFEED) {
      pm_metal_input_ascii_push ('\r');
    } else if (Key.UnicodeChar == CHAR_BACKSPACE) {
      pm_metal_input_ascii_push (0x08);
    } else if (Key.UnicodeChar >= 32 && Key.UnicodeChar < 127) {
      pm_metal_input_ascii_push ((CHAR8)Key.UnicodeChar);
    }
  }
}

void
pm_metal_input_poll_port (
  VOID
  )
{
  pm_metal_input_pointer_t  ev;
  INT32                     gw;
  INT32                     gh;

  /*
   * Prefer virtio-tablet (absolute; aligns with VNC). After EBS,
   * Absolute/SimplePointer are dead — fall back to i8042 AUX relative.
   */
  if (pm_metal_input_virtio_tablet_ready () != 0) {
    pm_metal_input_virtio_tablet_poll ();
    if (pm_metal_port_owned ()) {
      (VOID)pm_metal_input_ps2_init ();
      MetalInputI8042Drain ();
    } else {
      MetalInputConInDrain ();
    }

    return;
  }

  if (pm_metal_port_owned ()) {
    (VOID)pm_metal_input_ps2_init ();
    MetalInputI8042Drain ();
    return;
  }

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
      ev.flags = (pm_metal_input_pointer_locked () != 0)
                   ? PM_METAL_INPUT_PTR_RELATIVE
                   : PM_METAL_INPUT_PTR_ABSOLUTE;
      if ((pm_metal_input_pointer_locked () != 0)) {
        ev.x = -1;
        ev.y = -1;
      }

      if (ev.dx != 0 || ev.dy != 0 || ev.buttons != 0) {
        pm_metal_input_pointer_enqueue (&ev);
      }

      pm_metal_input_pointer_set_sample (mPtrX, mPtrY, mPtrButtons);
      MetalInputConInDrain ();
      return;
    }
  }

  if (mSimple != NULL) {
    EFI_STATUS               Status;
    EFI_SIMPLE_POINTER_STATE St;
    INT32                    wheel;

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

      ev.x       = (pm_metal_input_pointer_locked () != 0) ? -1 : mPtrX;
      ev.y       = (pm_metal_input_pointer_locked () != 0) ? -1 : mPtrY;
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
        pm_metal_input_pointer_enqueue (&ev);
      }

      wheel = -(INT32)St.RelativeMovementZ;
      if (wheel != 0) {
        if (wheel > 120 || wheel < -120) {
          wheel /= 40;
        } else if (wheel > 8 || wheel < -8) {
          wheel /= 8;
        }

        if (wheel == 0) {
          wheel = (St.RelativeMovementZ < 0) ? 1 : -1;
        }

        ZeroMem (&ev, sizeof (ev));
        ev.x       = (pm_metal_input_pointer_locked () != 0) ? -1 : mPtrX;
        ev.y       = (pm_metal_input_pointer_locked () != 0) ? -1 : mPtrY;
        ev.dx      = 0;
        ev.dy      = wheel;
        ev.buttons = mPtrButtons;
        ev.flags   = PM_METAL_INPUT_PTR_WHEEL;
        pm_metal_input_pointer_enqueue (&ev);
      }
    }
  }

  pm_metal_input_pointer_set_sample (mPtrX, mPtrY, mPtrButtons);
  MetalInputConInDrain ();
}

int
pm_metal_input_ps2_init (
  VOID
  )
{
  UINT8  cfg;
  INT32  gw;
  INT32  gh;

  if (mPs2Inited) {
    return 0;
  }

  (VOID)Ps2WriteCmd (0xAEu);
  Ps2FlushOut ();

  if (Ps2WriteCmd (0x20u) == 0 && Ps2WaitOutFull () == 0) {
    cfg = IoRead8 (0x60);
    cfg &= (UINT8)~0x10u;
    cfg &= (UINT8)~0x20u;
    cfg |= 0x40u;
    cfg &= (UINT8)~0x03u;
    if (Ps2WriteCmd (0x60u) == 0) {
      (VOID)Ps2WriteData (cfg);
    }
  }

  (VOID)Ps2WriteCmd (0xAEu);
  (VOID)Ps2WriteCmd (0xA8u);
  Ps2FlushOut ();

  (VOID)Ps2WriteAux (0xF6u);
  Ps2FlushOut ();
  Ps2AuxProbeImps2 ();
  Ps2FlushOut ();
  if (mAuxPktLen < 4) {
    (VOID)Ps2WriteAux (0xF6u);
    Ps2FlushOut ();
  }

  if (Ps2WriteAux (0xE8u) == 0) {
    (VOID)Ps2WriteAux (0x03u); /* 8 counts/mm */
  }

  (VOID)Ps2AuxSetSample (200);
  Ps2FlushOut ();
  (VOID)Ps2WriteAux (0xF4u);
  Ps2FlushOut ();

  if (Ps2WriteData (0xF4u) == 0 && Ps2WaitOutFull () == 0) {
    (VOID)IoRead8 (0x60);
  }

  Ps2FlushOut ();
  mPs2ShiftDown = 0;
  mPs2Ext       = 0;
  mAuxIdx       = 0;

  gw = pm_metal_gfx_width ();
  gh = pm_metal_gfx_height ();
  mPtrX = (gw > 0) ? (gw / 2) : 0;
  mPtrY = (gh > 0) ? (gh / 2) : 0;
  mPtrButtons = 0;
  pm_metal_input_pointer_set_sample (mPtrX, mPtrY, mPtrButtons);

  mPs2Inited = 1;
  return 0;
}
