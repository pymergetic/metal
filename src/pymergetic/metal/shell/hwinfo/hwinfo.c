/** @file
  hwinfo — Metal device tree, backends, PCI net/virtio scan. (shared host)
**/
#include <pymergetic/metal/shell/hwinfo/hwinfo.h>
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/bus/io/io.h>
#include <pymergetic/metal/bus/virtio/virtio.h>
#include "../../bus/pci/pci.h"
#include <pymergetic/metal/dev/net/net_ops.h>
#include <pymergetic/metal/dev/net/net_cfg.h>
#include <pymergetic/metal/dev/audio/audio_ops.h>
#include <pymergetic/metal/dev/blk/blk.h>
#include <pymergetic/metal/log/log.h>

#include <Uefi.h>
#include <IndustryStandard/Pci.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>

#include "wasm_export.h"

STATIC
CONST CHAR8 *
HwClassName (
  pm_metal_io_class_t  class
  )
{
  switch (class) {
    case PM_METAL_IO_TIME:
      return "time";
    case PM_METAL_IO_GFX:
      return "gfx";
    case PM_METAL_IO_AUDIO:
      return "audio";
    case PM_METAL_IO_INPUT:
      return "input";
    case PM_METAL_IO_FS:
      return "fs";
    case PM_METAL_IO_STREAM:
      return "stream";
    case PM_METAL_IO_NET:
      return "net";
    case PM_METAL_IO_RANDOM:
      return "random";
    case PM_METAL_IO_BLK:
      return "blk";
    default:
      return "?";
  }
}

STATIC
CONST CHAR8 *
HwBusName (
  UINT32  bus
  )
{
  switch (bus) {
    case PM_METAL_IO_BUS_PCI:
      return "pci";
    case PM_METAL_IO_BUS_ISA:
      return "isa";
    default:
      return "platform";
  }
}

STATIC
int
HwinfoDtIter (
  CONST pm_metal_io_node_t  *n,
  VOID                      *ctx
  )
{
  CONST CHAR8  *cls;
  CONST CHAR8  *compat;
  CONST CHAR8  *bus;

  (VOID)ctx;
  cls    = HwClassName (n->class);
  compat = (n->compat != NULL) ? n->compat : "?";
  bus    = HwBusName (n->bus);

  if (n->class == PM_METAL_IO_BLK) {
    pm_metal_blk_h  h;

    h = pm_metal_blk_at (n->unit);
    if (h != PM_METAL_BLK_INVALID && pm_metal_blk_ready (h)) {
      pm_metal_logf (
        "  %a/%a#%u  %a  %Lu sectors",
        cls,
        compat,
        n->unit,
        bus,
        pm_metal_blk_capacity_sectors (h)
        );
    } else {
      pm_metal_logf ("  %a/%a#%u  %a", cls, compat, n->unit, bus);
    }

    return 0;
  }

  if (pm_metal_io_dt_count_class (n->class) > 1) {
    pm_metal_logf ("  %a/%a#%u  %a", cls, compat, n->unit, bus);
  } else {
    pm_metal_logf ("  %a/%a  %a", cls, compat, bus);
  }

  return 0;
}

STATIC
CONST CHAR8 *
HwPciClassHint (
  UINT8  base_class
  )
{
  switch (base_class) {
    case 0x01:
      return "storage";
    case 0x02:
      return "network";
    case 0x03:
      return "display";
    case 0x04:
      return "multimedia";
    default:
      return "device";
  }
}

STATIC
CONST CHAR8 *
HwPciNetRole (
  UINT8  subclass
  )
{
  switch (subclass) {
    case 0x00:
      return "ethernet";
    case 0x80:
      return "wlan";
    default:
      return "network";
  }
}

STATIC
CONST CHAR8 *
HwPciDeviceName (
  UINT16  vendor,
  UINT16  device
  )
{
  if (vendor == 0x14E4) {
    switch (device) {
      case 0x1677:
        return "NetLink BCM5751 Gigabit Ethernet";
      case 0x167d:
        return "NetLink BCM5755 Gigabit Ethernet";
      case 0x1681:
        return "NetLink BCM5761 Gigabit Ethernet";
      case 0x16f7:
        return "NetLink BCM5787 Gigabit Ethernet";
      case 0x16d8:
        return "NetLink BCM5706 Gigabit Ethernet";
      default:
        return "NetLink Ethernet";
    }
  }

  if (vendor == 0x8086) {
    switch (device) {
      case 0x100e:
        return "82540EM Gigabit Ethernet";
      case 0x10d3:
        return "82574L Gigabit Ethernet";
      case 0x1533:
        return "I217-LM Gigabit Ethernet";
      case 0x4220:
        return "WiFi Link 5100/5300/5350";
      case 0x4227:
        return "WiFi Link 5100 AGN";
      case 0x4232:
        return "WiFi Link 5100/5300/5350";
      case 0x0082:
        return "WiFi 6 AX200";
      case 0x0083:
        return "WiFi 6 AX201";
      case 0x2723:
        return "WiFi 6 AX210";
      default:
        return NULL;
    }
  }

  if (vendor == 0x10EC) {
    switch (device) {
      case 0x8168:
        return "RTL8168 Gigabit Ethernet";
      case 0x8125:
        return "RTL8125 2.5GbE";
      default:
        return "RTL81xx Ethernet";
    }
  }

  if (vendor == PM_METAL_VIRTIO_VENDOR) {
    switch (device) {
      case PM_METAL_VIRTIO_DEV_NET:
      case PM_METAL_VIRTIO_DEV_NET_LEGACY:
        return "Virtio network";
      case PM_METAL_VIRTIO_DEV_BLK:
      case PM_METAL_VIRTIO_DEV_BLK_LEGACY:
        return "Virtio block";
      default:
        return "Virtio device";
    }
  }

  (VOID)vendor;
  return NULL;
}

STATIC
CONST CHAR8 *
HwPciVendorHint (
  UINT16  vendor
  )
{
  switch (vendor) {
    case 0x8086:
      return "Intel";
    case 0x10EC:
      return "Realtek";
    case 0x1AF4:
      return "Virtio";
    case 0x1234:
      return "QEMU";
    case 0x1022:
      return "AMD";
    case 0x14E4:
      return "Broadcom";
    default:
      return NULL;
  }
}

STATIC
CONST CHAR8 *
HwPciMetalDriver (
  UINT16  vendor,
  UINT16  device
  )
{
  if (vendor == 0x14E4) {
    switch (device) {
      case 0x1677:
      case 0x1678:
      case 0x1679:
      case 0x167a:
      case 0x167b:
      case 0x167c:
      case 0x167d:
      case 0x1673:
        return "bge";
      default:
        break;
    }
  }

  if (vendor != PM_METAL_VIRTIO_VENDOR) {
    return NULL;
  }

  if (device == PM_METAL_VIRTIO_DEV_NET
      || device == PM_METAL_VIRTIO_DEV_NET_LEGACY)
  {
    return "virtio-net";
  }

  if (device == PM_METAL_VIRTIO_DEV_BLK
      || device == PM_METAL_VIRTIO_DEV_BLK_LEGACY)
  {
    return "virtio-blk";
  }

  if (device == PM_METAL_VIRTIO_DEV_CONSOLE
      || device == PM_METAL_VIRTIO_DEV_CONSOLE_LEGACY)
  {
    return "virtio-console";
  }

  if (device == PM_METAL_VIRTIO_DEV_SOUND) {
    return "virtio-snd";
  }

  return "virtio";
}

STATIC
INT32
HwPciInteresting (
  UINT16  vendor,
  UINT8   base_class
  )
{
  if (vendor == 0xffff) {
    return 0;
  }

  if (vendor == PM_METAL_VIRTIO_VENDOR) {
    return 1;
  }

  if (base_class == 0x02) {
    return 1;
  }

  return 0;
}

STATIC
VOID
HwinfoPrintPci (
  VOID
  )
{
  UINT8   bus;
  UINT8   dev;
  UINT8   func;
  UINT32  found;

  found = 0;
  pm_metal_log ("hwinfo: pci (network + virtio)");
  for (bus = 0; bus < 8; bus++) {
    for (dev = 0; dev < 32; dev++) {
      UINT8  fmax;
      UINT8  hdr;
      UINT16 vendor;

      vendor = pm_bios_pci_read16 (bus, dev, 0, PCI_VENDOR_ID_OFFSET);
      if (vendor == 0xffff) {
        continue;
      }

      hdr  = pm_bios_pci_read8 (bus, dev, 0, PCI_HEADER_TYPE_OFFSET);
      fmax = (hdr & HEADER_TYPE_MULTI_FUNCTION) ? 8 : 1;
      for (func = 0; func < fmax; func++) {
        UINT16       ven;
        UINT16       did;
        UINT8        subclass;
        UINT8        base_class;
        CONST CHAR8  *vend_hint;
        CONST CHAR8  *dev_name;
        CONST CHAR8  *net_role;
        CONST CHAR8  *driver;

        ven = pm_bios_pci_read16 (bus, dev, func, PCI_VENDOR_ID_OFFSET);
        if (ven == 0xffff) {
          continue;
        }

        did         = pm_bios_pci_read16 (bus, dev, func, PCI_DEVICE_ID_OFFSET);
        subclass    = pm_bios_pci_read8 (bus, dev, func, PCI_CLASSCODE_OFFSET + 1);
        base_class  = pm_bios_pci_read8 (bus, dev, func, PCI_CLASSCODE_OFFSET + 2);
        if (!HwPciInteresting (ven, base_class)) {
          continue;
        }

        found++;
        vend_hint  = HwPciVendorHint (ven);
        dev_name   = HwPciDeviceName (ven, did);
        net_role   = (base_class == 0x02) ? HwPciNetRole (subclass) : HwPciClassHint (base_class);
        driver     = HwPciMetalDriver (ven, did);
        if (driver != NULL) {
          pm_metal_logf (
            "  %02x:%02x.%x %04x:%04x  %a  %a  metal:%a",
            bus,
            dev,
            func,
            ven,
            did,
            (dev_name != NULL) ? dev_name
                               : ((vend_hint != NULL) ? vend_hint : "?"),
            net_role,
            driver
            );
        } else {
          pm_metal_logf (
            "  %02x:%02x.%x %04x:%04x  %a  %a  (no metal driver)",
            bus,
            dev,
            func,
            ven,
            did,
            (dev_name != NULL) ? dev_name
                               : ((vend_hint != NULL) ? vend_hint : "?"),
            net_role
            );
        }
      }
    }
  }

  if (found == 0) {
    pm_metal_log ("  (none found)");
  }
}

void
pm_metal_hwinfo_print (
  VOID
  )
{
  CHAR8                         net_line[640];
  CONST pm_metal_net_ops_t     *net_ops;
  CONST pm_metal_audio_ops_t   *aud_ops;
  UINT32                        i;
  UINT32                        nblk;

  pm_metal_log ("hwinfo: metal devices");
  if (pm_metal_io_dt_count () == 0) {
    pm_metal_log ("  (empty)");
  } else {
    pm_metal_io_dt_foreach (HwinfoDtIter, NULL);
  }

  pm_metal_log ("hwinfo: backends");
  net_ops = pm_metal_net_get_ops ();
  pm_metal_logf (
    "  net ops: %a",
    (net_ops != NULL && net_ops->name != NULL) ? net_ops->name : "?"
    );
  if (pm_metal_net_if_status (net_line, (UINT32)sizeof (net_line)) == 0) {
    pm_metal_logf ("  net cfg: %a", net_line);
  }

  aud_ops = pm_metal_audio_get_ops ();
  pm_metal_logf (
    "  audio ops: %a",
    (aud_ops != NULL && aud_ops->name != NULL) ? aud_ops->name : "?"
    );

  nblk = pm_metal_blk_count ();
  pm_metal_logf ("  blk devices: %u", nblk);
  for (i = 0; i < nblk; i++) {
    pm_metal_blk_h  h;

    h = pm_metal_blk_at (i);
    if (h == PM_METAL_BLK_INVALID) {
      continue;
    }

    pm_metal_logf (
      "    blk#%u  %a  %Lu sectors",
      i,
      pm_metal_blk_ready (h) ? "ready" : "down",
      pm_metal_blk_ready (h) ? pm_metal_blk_capacity_sectors (h) : 0
      );
  }

  HwinfoPrintPci ();
}

#if !defined(__wasm__)

STATIC
VOID
HwinfoShellCmd (
  CONST CHAR8  *arg
  )
{
  (VOID)arg;
  pm_metal_hwinfo_print ();
  pm_metal_shell_mark_full ();
}

STATIC CONST pm_metal_shell_cmd_t  g_pm_metal_shell_cmd_hwinfo = {
  "hwinfo",
  "hwinfo            metal devices + PCI net/virtio",
  HwinfoShellCmd
};

void
pm_metal_shell_cmds_register_hwinfo (
  VOID
  )
{
  pm_metal_shell_cmd_register (&g_pm_metal_shell_cmd_hwinfo);
}

STATIC NativeSymbol g_pm_metal_hwinfo_native_symbols[] = {
  { "pm_metal_hwinfo_print", (VOID *)pm_metal_hwinfo_print, "()", NULL },
};

int
pm_metal_hwinfo_native_register (
  VOID
  )
{
  if (!wasm_runtime_register_natives (
         PM_METAL_HWINFO_WASI_MODULE,
         g_pm_metal_hwinfo_native_symbols,
         sizeof (g_pm_metal_hwinfo_native_symbols)
           / sizeof (g_pm_metal_hwinfo_native_symbols[0])
         ))
  {
    return -1;
  }

  return 0;
}

#endif /* !__wasm__ */
