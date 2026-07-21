/** @file
  Virtio-console (single port, no MULTIPORT). (impl: efi)
**/
#include <pymergetic/metal/console/console.h>
#include <pymergetic/metal/virtio/virtio.h>
#include <pymergetic/metal/stream/stream.h>
#include <pymergetic/metal/io/io.h>
#include <mem/mem.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiLib.h>
#include <Library/IoLib.h>

#define VCON_RX   0
#define VCON_TX   1
#define VCON_QSZ  64
#define VCON_BUFS 16
#define VCON_MTU  512

STATIC pm_metal_virtio_dev_t  mDev;
STATIC INT32                  mReady;
STATIC UINT8                 *mRxBufs[VCON_BUFS];
STATIC UINT8                  mTxScratch[VCON_MTU];
STATIC UINT8                  mRxRing[4096];
STATIC UINT32                 mRxHead;
STATIC UINT32                 mRxTail;

STATIC
UINT32
RxUsed (
  VOID
  )
{
  if (mRxHead >= mRxTail) {
    return mRxHead - mRxTail;
  }

  return sizeof (mRxRing) - (mRxTail - mRxHead);
}

STATIC
UINT32
RxSpace (
  VOID
  )
{
  return sizeof (mRxRing) - RxUsed () - 1u;
}

STATIC
VOID
RxPut (
  CONST UINT8  *p,
  UINT32        n
  )
{
  UINT32  i;
  UINT32  room;

  room = RxSpace ();
  if (n > room) {
    n = room;
  }

  for (i = 0; i < n; i++) {
    mRxRing[mRxHead] = p[i];
    mRxHead = (mRxHead + 1u) % sizeof (mRxRing);
  }
}

STATIC
int
VconInit (
  VOID
  )
{
  UINT64  feats;
  UINT32  i;

  if (mReady) {
    return 0;
  }

  if (pm_metal_virtio_open (PM_METAL_VIRTIO_DEV_CONSOLE, &mDev) != 0
      && pm_metal_virtio_open (PM_METAL_VIRTIO_DEV_CONSOLE_LEGACY, &mDev) != 0)
  {
    return -1;
  }

  feats = pm_metal_virtio_get_features (&mDev);
  /* Prefer single-port: clear MULTIPORT (bit 1) if offered. */
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

  if (pm_metal_virtio_setup_queue (&mDev, VCON_RX, VCON_QSZ) != 0
      || pm_metal_virtio_setup_queue (&mDev, VCON_TX, VCON_QSZ) != 0)
  {
    pm_metal_virtio_close (&mDev);
    return -1;
  }

  for (i = 0; i < VCON_BUFS; i++) {
    mRxBufs[i] = AllocatePages (EFI_SIZE_TO_PAGES (VCON_MTU));
    if (mRxBufs[i] == NULL) {
      pm_metal_virtio_close (&mDev);
      return -1;
    }

    ZeroMem (mRxBufs[i], VCON_MTU);
    (VOID)pm_metal_virtq_add (&mDev.vqs[VCON_RX], mRxBufs[i], VCON_MTU, 1, NULL);
  }

  pm_metal_virtq_kick (&mDev, &mDev.vqs[VCON_RX]);
  (VOID)pm_metal_virtio_driver_ok (&mDev);
  mReady = 1;
  
  return 0;
}

int
pm_metal_console_virtio_probe (
  VOID
  )
{
  if (VconInit () != 0) {
    return -1;
  }

  {
    STATIC pm_metal_io_node_t  Node = {
      PM_METAL_IO_STREAM, "virtio-console", 1
    };

    (VOID)pm_metal_io_dt_register (&Node);
  }
  return 0;
}

int
pm_metal_console_ready (
  VOID
  )
{
  return mReady ? 1 : 0;
}

void
pm_metal_console_com1_write (
  CONST VOID  *ptr,
  uint32_t     len
  )
{
  CONST UINT8  *p;
  UINT32        i;

  if (ptr == NULL || len == 0) {
    return;
  }

  p = (CONST UINT8 *)ptr;
  for (i = 0; i < len; i++) {
    UINTN  spins;

    for (spins = 0; spins < 100000; spins++) {
      if ((IoRead8 (0x3FD) & 0x20) != 0) {
        break;
      }
    }

    IoWrite8 (0x3F8, p[i]);
  }
}

uint32_t
pm_metal_console_write (
  CONST VOID  *ptr,
  uint32_t     len
  )
{
  UINT16  head;
  UINT32  ulen;
  UINT32  n;

  if (!mReady || ptr == NULL || len == 0) {
    return 0;
  }

  while (pm_metal_virtq_get_used (&mDev.vqs[VCON_TX], &head, &ulen)) {
    pm_metal_virtq_free_chain (&mDev.vqs[VCON_TX], head);
    (VOID)ulen;
  }

  n = len;
  if (n > sizeof (mTxScratch)) {
    n = sizeof (mTxScratch);
  }

  CopyMem (mTxScratch, ptr, n);
  if (pm_metal_virtq_add (&mDev.vqs[VCON_TX], mTxScratch, n, 0, NULL) != 0) {
    return 0;
  }

  pm_metal_virtq_kick (&mDev, &mDev.vqs[VCON_TX]);
  return n;
}

void
pm_metal_console_poll (
  VOID
  )
{
  UINT16  head;
  UINT32  len;
  UINT8  *buf;

  /* QEMU -serial: host keys land on COM1 even after ExitBootServices. */
  while ((IoRead8 (0x3FD) & 0x01) != 0) {
    UINT8  c;

    c = IoRead8 (0x3F8);
    RxPut (&c, 1);
  }

  if (!mReady) {
    return;
  }

  while (pm_metal_virtq_get_used (&mDev.vqs[VCON_RX], &head, &len)) {
    typedef struct {
      UINT64  Addr;
      UINT32  Len;
      UINT16  Flags;
      UINT16  Next;
    } desc_t;

    desc_t  *d;

    d   = (desc_t *)mDev.vqs[VCON_RX].desc;
    buf = (UINT8 *)(UINTN)d[head].Addr;
    if (len > 0) {
      RxPut (buf, len);
    }

    pm_metal_virtq_free_chain (&mDev.vqs[VCON_RX], head);
    (VOID)pm_metal_virtq_add (&mDev.vqs[VCON_RX], buf, VCON_MTU, 1, NULL);
  }

  pm_metal_virtq_kick (&mDev, &mDev.vqs[VCON_RX]);

  while (pm_metal_virtq_get_used (&mDev.vqs[VCON_TX], &head, &len)) {
    pm_metal_virtq_free_chain (&mDev.vqs[VCON_TX], head);
    (VOID)len;
  }
}

uint32_t
pm_metal_console_read (
  VOID      *ptr,
  uint32_t   len
  )
{
  UINT8   *out;
  UINT32   n;
  UINT32   i;

  if (ptr == NULL || len == 0) {
    return 0;
  }

  pm_metal_console_poll ();
  out = (UINT8 *)ptr;
  n   = RxUsed ();
  if (n > len) {
    n = len;
  }

  for (i = 0; i < n; i++) {
    out[i] = mRxRing[mRxTail];
    mRxTail = (mRxTail + 1u) % sizeof (mRxRing);
  }

  return n;
}
