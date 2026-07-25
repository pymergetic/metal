/** @file
  Virtio-input tablet — absolute X/Y for VNC/QEMU cursor alignment.
**/
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/dev/input/virtio_input.h>
#include <pymergetic/metal/dev/input/input.h>
#include <pymergetic/metal/dev/gfx/gfx.h>
#include <pymergetic/metal/bus/virtio/virtio.h>
#include <pymergetic/metal/bus/io/io.h>

#define VINPUT_EVENTQ  0
#define VINPUT_STATUSQ 1
#define VINPUT_QSZ     64
#define VINPUT_BUFS    32

#define VIRTIO_INPUT_CFG_EV_BITS  0x11u
#define VIRTIO_INPUT_CFG_ABS_INFO 0x12u

#define EV_SYN     0x00u
#define EV_KEY     0x01u
#define EV_REL     0x02u
#define EV_ABS     0x03u
#define SYN_REPORT 0
#define ABS_X      0
#define ABS_Y      1
#define REL_WHEEL  0x08u
#define REL_HWHEEL 0x06u
#define BTN_LEFT   0x110u
#define BTN_RIGHT  0x111u
#define BTN_MIDDLE 0x112u

#pragma pack(1)
typedef struct {
  uint16_t Type;
  uint16_t Code;
  uint32_t Value;
} vinput_event_t;

typedef struct {
  uint32_t Min;
  uint32_t Max;
  uint32_t Fuzz;
  uint32_t Flat;
  uint32_t Res;
} vinput_absinfo_t;
#pragma pack()

static pm_metal_virtio_dev_t mDev;
static int32_t               mReady;
static vinput_event_t       *mEvBufs[VINPUT_BUFS];
static int32_t               mAbsX;
static int32_t               mAbsY;
static int32_t               mHaveAbs;
static int32_t               mAbsPend; /* ABS updated since last SYN */
static int32_t               mWheel;   /* REL_WHEEL ticks since last SYN */
static int32_t               mBtnPend; /* buttons changed since last SYN */
static uint32_t              mButtons;
static int32_t               mXMin;
static int32_t               mXMax;
static int32_t               mYMin;
static int32_t               mYMax;

static int32_t CfgSelect(uint8_t select, uint8_t subsel)
{
  if (pm_metal_virtio_cfg_write(&mDev, 0, &select, 1) != 0 ||
      pm_metal_virtio_cfg_write(&mDev, 1, &subsel, 1) != 0) {
    return -1;
  }

  return 0;
}

static int32_t CfgReadAbs(uint8_t axis, vinput_absinfo_t *out)
{
  uint8_t size;

  if (CfgSelect(VIRTIO_INPUT_CFG_ABS_INFO, axis) != 0) {
    return -1;
  }

  if (pm_metal_virtio_cfg_read(&mDev, 2, &size, 1) != 0 || size < sizeof(*out)) {
    return -1;
  }

  return pm_metal_virtio_cfg_read(&mDev, 8, out, sizeof(*out));
}

static int32_t MapAxis(int32_t value, int32_t amin, int32_t amax, int32_t span)
{
  int64_t num;
  int32_t den;

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

  num = (int64_t)(value - amin) * (int64_t)(span - 1);
  return (int32_t)(num / den);
}

static void EmitReport(void)
{
  pm_metal_input_pointer_t ev;
  int32_t                  gw;
  int32_t                  gh;
  int32_t                  nx;
  int32_t                  ny;
  int32_t                  dx;
  int32_t                  dy;
  int32_t                  wheel;

  wheel  = mWheel;
  mWheel = 0;
  {
    int32_t abs_pend;
    int32_t btn_pend;

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

    gw = pm_metal_gfx_width();
    gh = pm_metal_gfx_height();
    if (gw <= 0) {
      gw = 1;
    }

    if (gh <= 0) {
      gh = 1;
    }

    if (mHaveAbs) {
      nx = MapAxis(mAbsX, mXMin, mXMax, gw);
      ny = MapAxis(mAbsY, mYMin, mYMax, gh);
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
      uint32_t ob;

      pm_metal_input_pointer_sample(&nx, &ny, &ob);
      (void)ob;
    }

    {
      int32_t  ox;
      int32_t  oy;
      uint32_t ob;

      pm_metal_input_pointer_sample(&ox, &oy, &ob);
      dx = nx - ox;
      dy = ny - oy;
    }

    /*
     * Wheel-only SYN (common from VNC → virtio-tablet): still report position
     * so chrome scroll hits the right console under the cursor.
     */
    if (wheel != 0) {
      memset(&ev, 0, sizeof(ev));
      ev.x       = (pm_metal_input_pointer_locked() != 0) ? -1 : nx;
      ev.y       = (pm_metal_input_pointer_locked() != 0) ? -1 : ny;
      ev.dx      = 0;
      ev.dy      = wheel;
      ev.buttons = mButtons;
      ev.flags   = PM_METAL_INPUT_PTR_WHEEL;
      if (pm_metal_input_pointer_locked() == 0) {
        ev.flags |= PM_METAL_INPUT_PTR_ABSOLUTE;
      }

      pm_metal_input_pointer_enqueue(&ev);
    }

    if (abs_pend != 0 || btn_pend != 0) {
      memset(&ev, 0, sizeof(ev));
      ev.x       = (pm_metal_input_pointer_locked() != 0) ? -1 : nx;
      ev.y       = (pm_metal_input_pointer_locked() != 0) ? -1 : ny;
      ev.dx      = dx;
      ev.dy      = dy;
      ev.buttons = mButtons;
      ev.flags   = (pm_metal_input_pointer_locked() != 0) ? PM_METAL_INPUT_PTR_RELATIVE
                                                          : PM_METAL_INPUT_PTR_ABSOLUTE;
      pm_metal_input_pointer_enqueue(&ev);
      pm_metal_input_pointer_set_sample(nx, ny, mButtons);
    } else if (wheel != 0) {
      /* Keep sample buttons in sync; position unchanged. */
      pm_metal_input_pointer_set_sample(nx, ny, mButtons);
    }
  }
}

static void HandleEvent(const vinput_event_t *e)
{
  if (e == NULL) {
    return;
  }

  if (e->Type == EV_ABS) {
    if (e->Code == ABS_X) {
      mAbsX    = (int32_t)e->Value;
      mHaveAbs = 1;
      mAbsPend = 1;
    } else if (e->Code == ABS_Y) {
      mAbsY    = (int32_t)e->Value;
      mHaveAbs = 1;
      mAbsPend = 1;
    }
  } else if (e->Type == EV_REL) {
    /* QEMU virtio-tablet: VNC wheel → REL_WHEEL (±detents). */
    if (e->Code == REL_WHEEL) {
      mWheel += (int32_t)e->Value;
    }
  } else if (e->Type == EV_KEY) {
    uint32_t bit;
    uint32_t prev;

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
    EmitReport();
  }
}

static int32_t TabletInit(void)
{
  uint64_t         feats;
  uint32_t         i;
  vinput_absinfo_t ax;
  vinput_absinfo_t ay;
  uint8_t          size;

  if (mReady) {
    return 0;
  }

  if (pm_metal_virtio_open(PM_METAL_VIRTIO_DEV_INPUT, &mDev) != 0) {
    return -1;
  }

  feats = pm_metal_virtio_get_features(&mDev);
  feats &= PM_METAL_VIRTIO_F_VERSION_1;
  if (pm_metal_virtio_set_features(&mDev, feats) != 0) {
    pm_metal_virtio_set_status(&mDev, 0);
    pm_metal_virtio_set_status(&mDev, (uint8_t)(PM_METAL_VIRTIO_S_ACK | PM_METAL_VIRTIO_S_DRIVER));
    if (pm_metal_virtio_set_features(&mDev, 0) != 0) {
      pm_metal_virtio_close(&mDev);
      return -1;
    }
  }

  /* Require absolute axes (tablet), not relative-only mouse. */
  if (CfgSelect(VIRTIO_INPUT_CFG_EV_BITS, (uint8_t)EV_ABS) != 0 ||
      pm_metal_virtio_cfg_read(&mDev, 2, &size, 1) != 0 || size == 0) {
    pm_metal_virtio_close(&mDev);
    return -1;
  }

  if (CfgReadAbs(ABS_X, &ax) != 0 || CfgReadAbs(ABS_Y, &ay) != 0) {
    /* QEMU tablet default if ABS_INFO is short. */
    mXMin = 0;
    mXMax = 0x7fff;
    mYMin = 0;
    mYMax = 0x7fff;
  } else {
    mXMin = (int32_t)ax.Min;
    mXMax = (int32_t)ax.Max;
    mYMin = (int32_t)ay.Min;
    mYMax = (int32_t)ay.Max;
    if (mXMax <= mXMin) {
      mXMin = 0;
      mXMax = 0x7fff;
    }

    if (mYMax <= mYMin) {
      mYMin = 0;
      mYMax = 0x7fff;
    }
  }

  if (pm_metal_virtio_setup_queue(&mDev, VINPUT_EVENTQ, VINPUT_QSZ) != 0 ||
      pm_metal_virtio_setup_queue(&mDev, VINPUT_STATUSQ, VINPUT_QSZ) != 0) {
    pm_metal_virtio_close(&mDev);
    return -1;
  }

  for (i = 0; i < VINPUT_BUFS; i++) {
    mEvBufs[i] = pm_metal_virtio_pages_alloc(PM_METAL_VIRTIO_SIZE_TO_PAGES(sizeof(vinput_event_t)));
    if (mEvBufs[i] == NULL) {
      pm_metal_virtio_close(&mDev);
      return -1;
    }

    memset(mEvBufs[i], 0, sizeof(vinput_event_t));
    (void)pm_metal_virtq_add(&mDev.vqs[VINPUT_EVENTQ], mEvBufs[i], sizeof(vinput_event_t), 1, NULL);
  }

  pm_metal_virtq_kick(&mDev, &mDev.vqs[VINPUT_EVENTQ]);
  (void)pm_metal_virtio_driver_ok(&mDev);
  mReady = 1;
  return 0;
}

int pm_metal_input_virtio_tablet_probe(void)
{
  if (TabletInit() != 0) {
    return -1;
  }

  {
    static pm_metal_io_node_t Node = {
      .class = PM_METAL_IO_INPUT, .compat = "virtio-tablet", .caps = 1, .bus = PM_METAL_IO_BUS_PCI
    };

    (void)pm_metal_io_dt_add(&Node);
  }
  return 0;
}

int pm_metal_input_virtio_tablet_ready(void)
{
  return mReady ? 1 : 0;
}

void pm_metal_input_virtio_tablet_poll(void)
{
  uint16_t  head;
  uint32_t  len;
  uintptr_t budget;

  if (!mReady) {
    return;
  }

  pm_metal_virtio_ack_isr(&mDev);
  for (budget = 0; budget < 64u; budget++) {
    typedef struct {
      uint64_t Addr;
      uint32_t Len;
      uint16_t Flags;
      uint16_t Next;
    } desc_t;

    desc_t         *d;
    vinput_event_t *ev;

    if (!pm_metal_virtq_get_used(&mDev.vqs[VINPUT_EVENTQ], &head, &len)) {
      break;
    }

    (void)len;
    d  = (desc_t *)mDev.vqs[VINPUT_EVENTQ].desc;
    ev = (vinput_event_t *)(uintptr_t)d[head].Addr;
    if (ev != NULL) {
      HandleEvent(ev);
      memset(ev, 0, sizeof(*ev));
    }

    pm_metal_virtq_free_chain(&mDev.vqs[VINPUT_EVENTQ], head);
    if (ev != NULL) {
      (void)pm_metal_virtq_add(&mDev.vqs[VINPUT_EVENTQ], ev, sizeof(*ev), 1, NULL);
    }
  }

  if (budget > 0) {
    pm_metal_virtq_kick(&mDev, &mDev.vqs[VINPUT_EVENTQ]);
  }
}
