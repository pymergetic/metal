/** @file bge L2 wrapper for lwIP (BSD-4-Clause driver). */
#include "bge_netif.h"
#include "metal_bge.h"

#include "../../bus/pci/pci.h"
#include <pymergetic/metal/log/log.h>

#include <Library/BaseMemoryLib.h>

STATIC metal_bge_softc_t  mSc;
STATIC INT32              mReady;
STATIC UINT8              mBus;
STATIC UINT8              mDev;
STATIC UINT8              mFunc;

STATIC int
bge_pci_match (
  UINT16  vendor,
  UINT16  device
  )
{
  switch (device) {
    case BGE_DEVICE_BCM5755:
    case BGE_DEVICE_BCM5755M:
    case BGE_DEVICE_BCM5751M:
    case 0x1677u:
    case 0x1678u:
    case 0x1679u:
    case 0x167au:
    case 0x167cu:
      return (vendor == BGE_VENDOR_BCOM) ? 0 : -1;
    default:
      return -1;
  }
}

int
pm_metal_bge_netif_detect (
  VOID
  )
{
  UINT8   bus;
  UINT8   dev;
  UINT8   func;
  UINT16  ids[] = {
    BGE_DEVICE_BCM5751M,
    BGE_DEVICE_BCM5755,
    BGE_DEVICE_BCM5755M,
  };

  for (UINTN i = 0; i < sizeof (ids) / sizeof (ids[0]); i++) {
    if (pm_bios_pci_find (BGE_VENDOR_BCOM, ids[i], &bus, &dev, &func) == 0) {
      mBus = bus;
      mDev = dev;
      mFunc = func;
      return 0;
    }
  }

  for (bus = 0; bus < 8; bus++) {
    for (dev = 0; dev < 32; dev++) {
      UINT16  ven;

      ven = pm_bios_pci_read16 (bus, dev, 0, 0x00);
      if (ven != BGE_VENDOR_BCOM) {
        continue;
      }

      for (func = 0; func < 8; func++) {
        UINT16  did;

        did = pm_bios_pci_read16 (bus, dev, func, 0x02);
        if (bge_pci_match (ven, did) == 0) {
          mBus  = bus;
          mDev  = dev;
          mFunc = func;
          return 0;
        }
      }
    }
  }

  return -1;
}

int
pm_metal_bge_netif_open (
  UINT8  mac_out[6]
  )
{
  if (mReady) {
    if (mac_out != NULL) {
      CopyMem (mac_out, mSc.mac, 6);
    }

    return 0;
  }

  if (pm_metal_bge_netif_detect () != 0) {
    return -1;
  }

  if (metal_bge_attach (&mSc, mBus, mDev, mFunc) != 0) {
    return -1;
  }

  if (metal_bge_init (&mSc) != 0) {
    metal_bge_detach (&mSc);
    return -1;
  }

  mReady = 1;
  if (mac_out != NULL) {
    CopyMem (mac_out, mSc.mac, 6);
  }

  /* MAC/link show under boot init tree (| +-- net → ethN). */
  return 0;
}

int
pm_metal_bge_netif_ready (
  VOID
  )
{
  return mReady ? 1 : 0;
}

CONST UINT8 *
pm_metal_bge_netif_mac (
  VOID
  )
{
  return mSc.mac;
}

int
pm_metal_bge_netif_tx (
  CONST VOID  *frame,
  UINT32       len
  )
{
  if (!mReady) {
    return -1;
  }

  return metal_bge_tx (&mSc, frame, len);
}

void
pm_metal_bge_netif_poll (
  pm_metal_bge_netif_rx_fn  on_frame,
  VOID                     *ctx
  )
{
  if (!mReady) {
    return;
  }

  metal_bge_poll (&mSc, (metal_bge_rx_fn)on_frame, ctx);
}
