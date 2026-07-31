/** @file bge L2 wrapper for lwIP (BSD-4-Clause driver). */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bge_netif.h"
#include "metal_bge.h"

#include "../../bus/pci/pci.h"
#include <pymergetic/metal/log/log.h>

static metal_bge_softc_t mSc;
static int32_t           mReady;
static uint8_t           mBus;
static uint8_t           mDev;
static uint8_t           mFunc;

static int bge_pci_match(uint16_t vendor, uint16_t device)
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

int pm_metal_bge_netif_detect(void)
{
  uint8_t  bus;
  uint8_t  dev;
  uint8_t  func;
  uint16_t ids[] = {
    BGE_DEVICE_BCM5751M,
    BGE_DEVICE_BCM5755,
    BGE_DEVICE_BCM5755M,
  };

  for (uintptr_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
    if (pm_bios_pci_find(BGE_VENDOR_BCOM, ids[i], &bus, &dev, &func) == 0) {
      mBus  = bus;
      mDev  = dev;
      mFunc = func;
      return 0;
    }
  }

  for (bus = 0; bus < 8; bus++) {
    for (dev = 0; dev < 32; dev++) {
      uint16_t ven;

      ven = pm_bios_pci_read16(bus, dev, 0, 0x00);
      if (ven != BGE_VENDOR_BCOM) {
        continue;
      }

      for (func = 0; func < 8; func++) {
        uint16_t did;

        did = pm_bios_pci_read16(bus, dev, func, 0x02);
        if (bge_pci_match(ven, did) == 0) {
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

int pm_metal_bge_netif_open(uint8_t mac_out[6])
{
  if (mReady) {
    if (mac_out != NULL) {
      memcpy(mac_out, mSc.mac, 6);
    }

    return 0;
  }

  if (pm_metal_bge_netif_detect() != 0) {
    return -1;
  }

  if (metal_bge_attach(&mSc, mBus, mDev, mFunc) != 0) {
    return -1;
  }

  if (metal_bge_init(&mSc) != 0) {
    metal_bge_detach(&mSc);
    return -1;
  }

  mReady = 1;
  if (mac_out != NULL) {
    memcpy(mac_out, mSc.mac, 6);
  }

  /* MAC/link show under boot init tree (| +-- net → ethN). */
  return 0;
}

int pm_metal_bge_netif_ready(void)
{
  return mReady ? 1 : 0;
}

const uint8_t *pm_metal_bge_netif_mac(void)
{
  return mSc.mac;
}

int pm_metal_bge_netif_tx(const void *frame, uint32_t len)
{
  if (!mReady) {
    return -1;
  }

  return metal_bge_tx(&mSc, frame, len);
}

void pm_metal_bge_netif_poll(pm_metal_bge_netif_rx_fn on_frame, void *ctx)
{
  if (!mReady) {
    return;
  }

  metal_bge_poll(&mSc, (metal_bge_rx_fn)on_frame, ctx);
}
