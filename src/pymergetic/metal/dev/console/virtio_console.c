/** @file
  Virtio-console (single port, no MULTIPORT). (impl: efi|bios)
**/
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/dev/console/console.h>
#include <pymergetic/metal/bus/virtio/virtio.h>
#include <pymergetic/metal/dev/stream/stream.h>
#include <pymergetic/metal/bus/io/io.h>
#include <pymergetic/metal/runtime/mem/mem.h>
#include <runtime/io/io.h>

#define VCON_RX   0
#define VCON_TX   1
#define VCON_QSZ  64
#define VCON_BUFS 16
#define VCON_MTU  512

static pm_metal_virtio_dev_t       mDev;
static int32_t                     mReady;
static uint8_t                    *mRxBufs[VCON_BUFS];
static uint8_t                     mTxScratch[VCON_MTU];
static uint8_t                     mRxRing[4096];
static uint32_t                    mRxHead;
static uint32_t                    mRxTail;
static pm_metal_console_mirror_fn  mMirrorFn;
static void                       *mMirrorCtx;

static uint32_t RxUsed(void)
{
  if (mRxHead >= mRxTail) {
    return mRxHead - mRxTail;
  }

  return sizeof(mRxRing) - (mRxTail - mRxHead);
}

static uint32_t RxSpace(void)
{
  return sizeof(mRxRing) - RxUsed() - 1u;
}

static void RxPut(const uint8_t *p, uint32_t n)
{
  uint32_t i;
  uint32_t room;

  room = RxSpace();
  if (n > room) {
    n = room;
  }

  for (i = 0; i < n; i++) {
    mRxRing[mRxHead] = p[i];
    mRxHead          = (mRxHead + 1u) % sizeof(mRxRing);
  }
}

static int VconInit(void)
{
  uint64_t feats;
  uint32_t i;

  if (mReady) {
    return 0;
  }

  if (pm_metal_virtio_open(PM_METAL_VIRTIO_DEV_CONSOLE, &mDev) != 0 &&
      pm_metal_virtio_open(PM_METAL_VIRTIO_DEV_CONSOLE_LEGACY, &mDev) != 0) {
    return -1;
  }

  feats = pm_metal_virtio_get_features(&mDev);
  /* Prefer single-port: clear MULTIPORT (bit 1) if offered. */
  feats &= PM_METAL_VIRTIO_F_VERSION_1;
  if (pm_metal_virtio_set_features(&mDev, feats) != 0) {
    pm_metal_virtio_set_status(&mDev, 0);
    pm_metal_virtio_set_status(&mDev, (uint8_t)(PM_METAL_VIRTIO_S_ACK | PM_METAL_VIRTIO_S_DRIVER));
    if (pm_metal_virtio_set_features(&mDev, 0) != 0) {
      pm_metal_virtio_close(&mDev);
      return -1;
    }
  }

  if (pm_metal_virtio_setup_queue(&mDev, VCON_RX, VCON_QSZ) != 0 ||
      pm_metal_virtio_setup_queue(&mDev, VCON_TX, VCON_QSZ) != 0) {
    pm_metal_virtio_close(&mDev);
    return -1;
  }

  for (i = 0; i < VCON_BUFS; i++) {
    mRxBufs[i] = pm_metal_virtio_pages_alloc(PM_METAL_VIRTIO_SIZE_TO_PAGES(VCON_MTU));
    if (mRxBufs[i] == NULL) {
      pm_metal_virtio_close(&mDev);
      return -1;
    }

    memset(mRxBufs[i], 0, VCON_MTU);
    (void)pm_metal_virtq_add(&mDev.vqs[VCON_RX], mRxBufs[i], VCON_MTU, 1, NULL);
  }

  pm_metal_virtq_kick(&mDev, &mDev.vqs[VCON_RX]);
  (void)pm_metal_virtio_driver_ok(&mDev);
  mReady = 1;

  return 0;
}

int pm_metal_console_virtio_probe(void)
{
  if (VconInit() != 0) {
    return -1;
  }

  {
    static pm_metal_io_node_t Node = {
      .class = PM_METAL_IO_STREAM, .compat = "virtio-console", .caps = 1, .bus = PM_METAL_IO_BUS_PCI
    };

    (void)pm_metal_io_dt_add(&Node);
  }
  return 0;
}

int pm_metal_console_ready(void)
{
  return mReady ? 1 : 0;
}

void pm_metal_console_set_mirror(pm_metal_console_mirror_fn fn, void *ctx)
{
  mMirrorFn  = fn;
  mMirrorCtx = ctx;
}

void pm_metal_console_com1_write(const void *ptr, uint32_t len)
{
  const uint8_t *p;
  uint32_t       i;

  if (ptr == NULL || len == 0) {
    return;
  }

  p = (const uint8_t *)ptr;
  for (i = 0; i < len; i++) {
    uintptr_t spins;

    for (spins = 0; spins < 100000; spins++) {
      if ((pm_metal_io_in8(0x3FD) & 0x20) != 0) {
        break;
      }
    }

    pm_metal_io_out8(0x3F8, p[i]);
  }

  /* SSH (etc.): same console bytes as UART, not a second shell. */
  if (mMirrorFn != NULL) {
    mMirrorFn(ptr, len, mMirrorCtx);
  }
}

uint32_t pm_metal_console_write(const void *ptr, uint32_t len)
{
  uint16_t head;
  uint32_t ulen;
  uint32_t n;

  if (!mReady || ptr == NULL || len == 0) {
    return 0;
  }

  while (pm_metal_virtq_get_used(&mDev.vqs[VCON_TX], &head, &ulen)) {
    pm_metal_virtq_free_chain(&mDev.vqs[VCON_TX], head);
    (void)ulen;
  }

  n = len;
  if (n > sizeof(mTxScratch)) {
    n = sizeof(mTxScratch);
  }

  memcpy(mTxScratch, ptr, n);
  if (pm_metal_virtq_add(&mDev.vqs[VCON_TX], mTxScratch, n, 0, NULL) != 0) {
    return 0;
  }

  pm_metal_virtq_kick(&mDev, &mDev.vqs[VCON_TX]);
  return n;
}

void pm_metal_console_poll(void)
{
  uint16_t head;
  uint32_t len;
  uint8_t *buf;

  /* QEMU -serial: host keys land on COM1 even after ExitBootServices. */
  while ((pm_metal_io_in8(0x3FD) & 0x01) != 0) {
    uint8_t c;

    c = pm_metal_io_in8(0x3F8);
    RxPut(&c, 1);
  }

  if (!mReady) {
    return;
  }

  while (pm_metal_virtq_get_used(&mDev.vqs[VCON_RX], &head, &len)) {
    typedef struct {
      uint64_t Addr;
      uint32_t Len;
      uint16_t Flags;
      uint16_t Next;
    } desc_t;

    desc_t *d;

    d   = (desc_t *)mDev.vqs[VCON_RX].desc;
    buf = (uint8_t *)(uintptr_t)d[head].Addr;
    if (len > 0) {
      RxPut(buf, len);
    }

    pm_metal_virtq_free_chain(&mDev.vqs[VCON_RX], head);
    (void)pm_metal_virtq_add(&mDev.vqs[VCON_RX], buf, VCON_MTU, 1, NULL);
  }

  pm_metal_virtq_kick(&mDev, &mDev.vqs[VCON_RX]);

  while (pm_metal_virtq_get_used(&mDev.vqs[VCON_TX], &head, &len)) {
    pm_metal_virtq_free_chain(&mDev.vqs[VCON_TX], head);
    (void)len;
  }
}

uint32_t pm_metal_console_inject_rx(const void *ptr, uint32_t len)
{
  uint32_t before;
  uint32_t after;

  if (ptr == NULL || len == 0) {
    return 0;
  }

  before = RxUsed();
  RxPut((const uint8_t *)ptr, len);
  after = RxUsed();
  return after - before;
}

uint32_t pm_metal_console_read(void *ptr, uint32_t len)
{
  uint8_t *out;
  uint32_t n;
  uint32_t i;

  if (ptr == NULL || len == 0) {
    return 0;
  }

  pm_metal_console_poll();
  out = (uint8_t *)ptr;
  n   = RxUsed();
  if (n > len) {
    n = len;
  }

  for (i = 0; i < n; i++) {
    out[i]  = mRxRing[mRxTail];
    mRxTail = (mRxTail + 1u) % sizeof(mRxRing);
  }

  return n;
}
