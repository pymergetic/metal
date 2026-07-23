/** @file
  Virtio-input tablet — absolute X/Y for VNC/QEMU cursor alignment.
**/
#include <pymergetic/metal/dev/input/virtio_input.h>
#include <pymergetic/metal/dev/input/input.h>
#include <pymergetic/metal/dev/gfx/gfx.h>
#include <pymergetic/metal/bus/virtio/virtio.h>
#include <pymergetic/metal/bus/io/io.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>

#define VINPUT_EVENTQ  0
#define VINPUT_STATUSQ 1
#define VINPUT_QSZ     64
#define VINPUT_BUFS    32

#define VIRTIO_INPUT_CFG_EV_BITS  0x11u
#define VIRTIO_INPUT_CFG_ABS_INFO 0x12u

#define EV_SYN  0x00u
#define EV_KEY  0x01u
#define EV_REL  0x02u
#define EV_ABS  0x03u
#define SYN_REPORT 0
#define ABS_X   0
#define ABS_Y   1
#define REL_WHEEL  0x08u
#define REL_HWHEEL 0x06u
#define BTN_LEFT   0x110u
#define BTN_RIGHT  0x111u
#define BTN_MIDDLE 0x112u

#pragma pack (1)
typedef struct {
  UINT16  Type;
  UINT16  Code;
  UINT32  Value;
} vinput_event_t;

typedef struct {
  UINT32  Min;
  UINT32  Max;
  UINT32  Fuzz;
  UINT32  Flat;
  UINT32  Res;
} vinput_absinfo_t;
#pragma pack ()

STATIC pm_metal_virtio_dev_t  mDev;
STATIC INT32                  mReady;
STATIC vinput_event_t        *mEvBufs[VINPUT_BUFS];
STATIC INT32                  mAbsX;
STATIC INT32                  mAbsY;
STATIC INT32                  mHaveAbs;
STATIC INT32                  mAbsPend; /* ABS updated since last SYN */
STATIC INT32                  mWheel;   /* REL_WHEEL ticks since last SYN */
STATIC INT32                  mBtnPend; /* buttons changed since last SYN */
STATIC UINT32                 mButtons;
STATIC INT32                  mXMin;
STATIC INT32                  mXMax;
STATIC INT32                  mYMin;
STATIC INT32                  mYMax;

STATIC
INT32
CfgSelect (
  UINT8  select,
  UINT8  subsel
  )
{
  if (pm_metal_virtio_cfg_write (&mDev, 0, &select, 1) != 0
      || pm_metal_virtio_cfg_write (&mDev, 1, &subsel, 1) != 0)
  {
    return -1;
  }

  return 0;
}

STATIC
INT32
CfgReadAbs (
  UINT8             axis,
  vinput_absinfo_t  *out
  )
{
  UINT8  size;

  if (CfgSelect (VIRTIO_INPUT_CFG_ABS_INFO, axis) != 0) {
    return -1;
  }

  if (pm_metal_virtio_cfg_read (&mDev, 2, &size, 1) != 0 || size < sizeof (*out)) {
    return -1;
  }

  return pm_metal_virtio_cfg_read (&mDev, 8, out, sizeof (*out));
}

STATIC
INT32
MapAxis (
  INT32  value,
  INT32  amin,
  INT32  amax,
  INT32  span
  )
{
  INT64  num;
  INT32  den;

  if (span <= 1) {
    return 0;
  }

  den = amax - amin;
  if (den <= 0) {
    return 0;
  }

  if (value < amin) {
    value = amin;
  }

  if (value > amax) {
    value = amax;
  }

  num = (INT64)(value - amin) * (INT64)(span - 1);
  return (INT32)(num / den);
}

STATIC
VOID
EmitReport (
  VOID
  )
{
  pm_metal_input_pointer_t  ev;
  INT32                     gw;
  INT32                     gh;
  INT32                     nx;
  INT32                     ny;
  INT32                     dx;
  INT32                     dy;
  INT32                     wheel;

  wheel  = mWheel;
  mWheel = 0;
  {
    INT32  abs_pend;
    INT32  btn_pend;

    abs_pend = mAbsPend;
    btn_pend = mBtnPend;
    mAbsPend = 0;
    mBtnPend = 0;
    if (!mHaveAbs && wheel == 0) {
      return;
    }

    if (wheel == 0 && abs_pend == 0 && btn_pend == 0) {
      return;
    }

    gw = pm_metal_gfx_width ();
    gh = pm_metal_gfx_height ();
    if (gw <= 0) {
      gw = 1;
    }

    if (gh <= 0) {
      gh = 1;
    }

    if (mHaveAbs) {
      nx = MapAxis (mAbsX, mXMin, mXMax, gw);
      ny = MapAxis (mAbsY, mYMin, mYMax, gh);
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
    } else {
      UINT32  ob;

      pm_metal_input_pointer_sample (&nx, &ny, &ob);
      (VOID)ob;
    }

    {
      INT32  ox;
      INT32  oy;
      UINT32 ob;

      pm_metal_input_pointer_sample (&ox, &oy, &ob);
      dx = nx - ox;
      dy = ny - oy;
    }

    /*
     * Wheel-only SYN (common from VNC → virtio-tablet): still report position
     * so chrome scroll hits the right console under the cursor.
     */
    if (wheel != 0) {
      ZeroMem (&ev, sizeof (ev));
      ev.x       = (pm_metal_input_pointer_locked () != 0) ? -1 : nx;
      ev.y       = (pm_metal_input_pointer_locked () != 0) ? -1 : ny;
      ev.dx      = 0;
      ev.dy      = wheel;
      ev.buttons = mButtons;
      ev.flags   = PM_METAL_INPUT_PTR_WHEEL;
      if (pm_metal_input_pointer_locked () == 0) {
        ev.flags |= PM_METAL_INPUT_PTR_ABSOLUTE;
      }

      pm_metal_input_pointer_enqueue (&ev);
    }

    if (abs_pend != 0 || btn_pend != 0) {
      ZeroMem (&ev, sizeof (ev));
      ev.x       = (pm_metal_input_pointer_locked () != 0) ? -1 : nx;
      ev.y       = (pm_metal_input_pointer_locked () != 0) ? -1 : ny;
      ev.dx      = dx;
      ev.dy      = dy;
      ev.buttons = mButtons;
      ev.flags   = (pm_metal_input_pointer_locked () != 0)
                     ? PM_METAL_INPUT_PTR_RELATIVE
                     : PM_METAL_INPUT_PTR_ABSOLUTE;
      pm_metal_input_pointer_enqueue (&ev);
      pm_metal_input_pointer_set_sample (nx, ny, mButtons);
    } else if (wheel != 0) {
      /* Keep sample buttons in sync; position unchanged. */
      pm_metal_input_pointer_set_sample (nx, ny, mButtons);
    }
  }
}

STATIC
VOID
HandleEvent (
  CONST vinput_event_t  *e
  )
{
  if (e == NULL) {
    return;
  }

  if (e->Type == EV_ABS) {
    if (e->Code == ABS_X) {
      mAbsX    = (INT32)e->Value;
      mHaveAbs = 1;
      mAbsPend = 1;
    } else if (e->Code == ABS_Y) {
      mAbsY    = (INT32)e->Value;
      mHaveAbs = 1;
      mAbsPend = 1;
    }
  } else if (e->Type == EV_REL) {
    /* QEMU virtio-tablet: VNC wheel → REL_WHEEL (±detents). */
    if (e->Code == REL_WHEEL) {
      mWheel += (INT32)e->Value;
    }
  } else if (e->Type == EV_KEY) {
    UINT32  bit;
    UINT32  prev;

    bit = 0;
    if (e->Code == BTN_LEFT) {
      bit = 1u;
    } else if (e->Code == BTN_RIGHT) {
      bit = 2u;
    } else if (e->Code == BTN_MIDDLE) {
      bit = 4u;
    }

    if (bit != 0) {
      prev = mButtons;
      if (e->Value) {
        mButtons |= bit;
      } else {
        mButtons &= ~bit;
      }

      if (mButtons != prev) {
        mBtnPend = 1;
      }
    }
  } else if (e->Type == EV_SYN && e->Code == SYN_REPORT) {
    EmitReport ();
  }
}

STATIC
INT32
TabletInit (
  VOID
  )
{
  UINT64            feats;
  UINT32            i;
  vinput_absinfo_t  ax;
  vinput_absinfo_t  ay;
  UINT8             size;

  if (mReady) {
    return 0;
  }

  if (pm_metal_virtio_open (PM_METAL_VIRTIO_DEV_INPUT, &mDev) != 0) {
    return -1;
  }

  feats = pm_metal_virtio_get_features (&mDev);
  feats &= PM_METAL_VIRTIO_F_VERSION_1;
  if (pm_metal_virtio_set_features (&mDev, feats) != 0) {
    pm_metal_virtio_set_status (&mDev, 0);
    pm_metal_virtio_set_status (
      &mDev,
      (UINT8)(PM_METAL_VIRTIO_S_ACK | PM_METAL_VIRTIO_S_DRIVER)
      );
    if (pm_metal_virtio_set_features (&mDev, 0) != 0) {
      pm_metal_virtio_close (&mDev);
      return -1;
    }
  }

  /* Require absolute axes (tablet), not relative-only mouse. */
  if (CfgSelect (VIRTIO_INPUT_CFG_EV_BITS, (UINT8)EV_ABS) != 0
      || pm_metal_virtio_cfg_read (&mDev, 2, &size, 1) != 0
      || size == 0)
  {
    pm_metal_virtio_close (&mDev);
    return -1;
  }

  if (CfgReadAbs (ABS_X, &ax) != 0 || CfgReadAbs (ABS_Y, &ay) != 0) {
    /* QEMU tablet default if ABS_INFO is short. */
    mXMin = 0;
    mXMax = 0x7fff;
    mYMin = 0;
    mYMax = 0x7fff;
  } else {
    mXMin = (INT32)ax.Min;
    mXMax = (INT32)ax.Max;
    mYMin = (INT32)ay.Min;
    mYMax = (INT32)ay.Max;
    if (mXMax <= mXMin) {
      mXMin = 0;
      mXMax = 0x7fff;
    }

    if (mYMax <= mYMin) {
      mYMin = 0;
      mYMax = 0x7fff;
    }
  }

  if (pm_metal_virtio_setup_queue (&mDev, VINPUT_EVENTQ, VINPUT_QSZ) != 0
      || pm_metal_virtio_setup_queue (&mDev, VINPUT_STATUSQ, VINPUT_QSZ) != 0)
  {
    pm_metal_virtio_close (&mDev);
    return -1;
  }

  for (i = 0; i < VINPUT_BUFS; i++) {
    mEvBufs[i] = pm_metal_virtio_pages_alloc (
                   EFI_SIZE_TO_PAGES (sizeof (vinput_event_t))
                   );
    if (mEvBufs[i] == NULL) {
      pm_metal_virtio_close (&mDev);
      return -1;
    }

    ZeroMem (mEvBufs[i], sizeof (vinput_event_t));
    (VOID)pm_metal_virtq_add (
            &mDev.vqs[VINPUT_EVENTQ],
            mEvBufs[i],
            sizeof (vinput_event_t),
            1,
            NULL
            );
  }

  pm_metal_virtq_kick (&mDev, &mDev.vqs[VINPUT_EVENTQ]);
  (VOID)pm_metal_virtio_driver_ok (&mDev);
  mReady = 1;
  return 0;
}

int
pm_metal_input_virtio_tablet_probe (
  VOID
  )
{
  if (TabletInit () != 0) {
    return -1;
  }

  {
    STATIC pm_metal_io_node_t  Node = {
      .class = PM_METAL_IO_INPUT,
      .compat = "virtio-tablet",
      .caps = 1,
      .bus = PM_METAL_IO_BUS_PCI
    };

    (VOID)pm_metal_io_dt_add (&Node);
  }
  return 0;
}

int
pm_metal_input_virtio_tablet_ready (
  VOID
  )
{
  return mReady ? 1 : 0;
}

void
pm_metal_input_virtio_tablet_poll (
  VOID
  )
{
  UINT16  head;
  UINT32  len;
  UINTN   budget;

  if (!mReady) {
    return;
  }

  pm_metal_virtio_ack_isr (&mDev);
  for (budget = 0; budget < 64u; budget++) {
    typedef struct {
      UINT64  Addr;
      UINT32  Len;
      UINT16  Flags;
      UINT16  Next;
    } desc_t;

    desc_t         *d;
    vinput_event_t *ev;

    if (!pm_metal_virtq_get_used (&mDev.vqs[VINPUT_EVENTQ], &head, &len)) {
      break;
    }

    (VOID)len;
    d  = (desc_t *)mDev.vqs[VINPUT_EVENTQ].desc;
    ev = (vinput_event_t *)(UINTN)d[head].Addr;
    if (ev != NULL) {
      HandleEvent (ev);
      ZeroMem (ev, sizeof (*ev));
    }

    pm_metal_virtq_free_chain (&mDev.vqs[VINPUT_EVENTQ], head);
    if (ev != NULL) {
      (VOID)pm_metal_virtq_add (
              &mDev.vqs[VINPUT_EVENTQ],
              ev,
              sizeof (*ev),
              1,
              NULL
              );
    }
  }

  if (budget > 0) {
    pm_metal_virtq_kick (&mDev, &mDev.vqs[VINPUT_EVENTQ]);
  }
}
