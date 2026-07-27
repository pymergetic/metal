/** @file
  hwinfo — Metal device tree, backends, PCI net/virtio scan. (shared host)
**/
#include <pymergetic/metal/shell/hwinfo/hwinfo.h>
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/host/host.h>
#include <pymergetic/metal/bus/io/io.h>
#include <pymergetic/metal/bus/virtio/virtio.h>
#include "../../bus/pci/pci.h"
#include <pymergetic/metal/net/ip/ip_ops.h>
#include <pymergetic/metal/net/ip/ip_cfg.h>
#include <pymergetic/metal/dev/audio/audio_ops.h>
#include <pymergetic/metal/dev/blk/blk.h>
#include <pymergetic/metal/log/log.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* PCI config-space offsets (PCI 2.2 spec; see external/edk2 IndustryStandard/Pci22.h). */
#define PM_HWINFO_PCI_VENDOR_ID_OFFSET       0x00
#define PM_HWINFO_PCI_DEVICE_ID_OFFSET       0x02
#define PM_HWINFO_PCI_CLASSCODE_OFFSET       0x09
#define PM_HWINFO_PCI_HEADER_TYPE_OFFSET     0x0E
#define PM_HWINFO_HEADER_TYPE_MULTI_FUNCTION 0x80

#include "wasm_export.h"

static const char *HwClassName(pm_metal_io_class_t class)
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

static const char *HwBusName(uint32_t bus)
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

static int HwinfoDtIter(const pm_metal_io_node_t *n, void *ctx)
{
  const char *cls;
  const char *compat;
  const char *bus;

  (void)ctx;
  cls    = HwClassName(n->class);
  compat = (n->compat != NULL) ? n->compat : "?";
  bus    = HwBusName(n->bus);

  if (n->class == PM_METAL_IO_BLK) {
    pm_metal_blk_h h;

    h = pm_metal_blk_at(n->unit);
    if (h != PM_METAL_BLK_INVALID && pm_metal_blk_ready(h)) {
      pm_metal_logf("  %s/%s#%u  %s  %llu sectors",
                    cls,
                    compat,
                    n->unit,
                    bus,
                    pm_metal_blk_capacity_sectors(h));
    } else {
      pm_metal_logf("  %s/%s#%u  %s", cls, compat, n->unit, bus);
    }

    return 0;
  }

  if (n->class == PM_METAL_IO_GFX && n->bus == PM_METAL_IO_BUS_PCI) {
    pm_metal_logf("  %s/%s  %s  %04x:%04x @%02x:%02x.%x",
                  cls,
                  compat,
                  bus,
                  (uint32_t)((n->loc[3] >> 16) & 0xffffu),
                  (uint32_t)(n->loc[3] & 0xffffu),
                  (uint32_t)n->loc[0],
                  (uint32_t)n->loc[1],
                  (uint32_t)n->loc[2]);
    return 0;
  }

  if (pm_metal_io_dt_count_class(n->class) > 1) {
    pm_metal_logf("  %s/%s#%u  %s", cls, compat, n->unit, bus);
  } else {
    pm_metal_logf("  %s/%s  %s", cls, compat, bus);
  }

  return 0;
}

static const char *HwPciClassHint(uint8_t base_class)
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

static const char *HwPciNetRole(uint8_t subclass)
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

static const char *HwPciDeviceName(uint16_t vendor, uint16_t device)
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

  (void)vendor;
  return NULL;
}

static const char *HwPciVendorHint(uint16_t vendor)
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

static const char *HwPciMetalDriver(uint16_t vendor, uint16_t device)
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

  if (device == PM_METAL_VIRTIO_DEV_NET || device == PM_METAL_VIRTIO_DEV_NET_LEGACY) {
    return "virtio-net";
  }

  if (device == PM_METAL_VIRTIO_DEV_BLK || device == PM_METAL_VIRTIO_DEV_BLK_LEGACY) {
    return "virtio-blk";
  }

  if (device == PM_METAL_VIRTIO_DEV_CONSOLE || device == PM_METAL_VIRTIO_DEV_CONSOLE_LEGACY) {
    return "virtio-console";
  }

  if (device == PM_METAL_VIRTIO_DEV_SOUND) {
    return "virtio-snd";
  }

  return "virtio";
}

static int32_t HwPciInteresting(uint16_t vendor, uint8_t base_class)
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

static void HwinfoPrintPci(void)
{
  uint8_t  bus;
  uint8_t  dev;
  uint8_t  func;
  uint32_t found;

  found = 0;
  pm_metal_log("hwinfo: pci (network + virtio)");
  for (bus = 0; bus < 8; bus++) {
    for (dev = 0; dev < 32; dev++) {
      uint8_t  fmax;
      uint8_t  hdr;
      uint16_t vendor;

      vendor = pm_bios_pci_read16(bus, dev, 0, PM_HWINFO_PCI_VENDOR_ID_OFFSET);
      if (vendor == 0xffff) {
        continue;
      }

      hdr  = pm_bios_pci_read8(bus, dev, 0, PM_HWINFO_PCI_HEADER_TYPE_OFFSET);
      fmax = (hdr & PM_HWINFO_HEADER_TYPE_MULTI_FUNCTION) ? 8 : 1;
      for (func = 0; func < fmax; func++) {
        uint16_t    ven;
        uint16_t    did;
        uint8_t     subclass;
        uint8_t     base_class;
        const char *vend_hint;
        const char *dev_name;
        const char *net_role;
        const char *driver;

        ven = pm_bios_pci_read16(bus, dev, func, PM_HWINFO_PCI_VENDOR_ID_OFFSET);
        if (ven == 0xffff) {
          continue;
        }

        did        = pm_bios_pci_read16(bus, dev, func, PM_HWINFO_PCI_DEVICE_ID_OFFSET);
        subclass   = pm_bios_pci_read8(bus, dev, func, PM_HWINFO_PCI_CLASSCODE_OFFSET + 1);
        base_class = pm_bios_pci_read8(bus, dev, func, PM_HWINFO_PCI_CLASSCODE_OFFSET + 2);
        if (!HwPciInteresting(ven, base_class)) {
          continue;
        }

        found++;
        vend_hint = HwPciVendorHint(ven);
        dev_name  = HwPciDeviceName(ven, did);
        net_role  = (base_class == 0x02) ? HwPciNetRole(subclass) : HwPciClassHint(base_class);
        driver    = HwPciMetalDriver(ven, did);
        if (driver != NULL) {
          pm_metal_logf("  %02x:%02x.%x %04x:%04x  %s  %s  metal:%s",
                        bus,
                        dev,
                        func,
                        ven,
                        did,
                        (dev_name != NULL) ? dev_name : ((vend_hint != NULL) ? vend_hint : "?"),
                        net_role,
                        driver);
        } else {
          pm_metal_logf("  %02x:%02x.%x %04x:%04x  %s  %s  (no metal driver)",
                        bus,
                        dev,
                        func,
                        ven,
                        did,
                        (dev_name != NULL) ? dev_name : ((vend_hint != NULL) ? vend_hint : "?"),
                        net_role);
        }
      }
    }
  }

  if (found == 0) {
    pm_metal_log("  (none found)");
  }
}

static void HwCpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
  __asm__ volatile("cpuid" : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx) : "a"(leaf), "c"(0));
}

void pm_metal_hwinfo_cpu_brand(char *out, size_t cap)
{
  uint32_t regs[4];
  uint32_t brand[12];
  char     text[49];
  size_t   i;
  size_t   start;
  size_t   end;

  if (out == NULL || cap == 0) {
    return;
  }

  out[0] = '\0';

  HwCpuid(0x80000000u, &regs[0], &regs[1], &regs[2], &regs[3]);
  if (regs[0] < 0x80000004u) {
    /* No brand-string leaf (rare/ancient CPU) -- leaf 0's vendor ID
     * ("GenuineIntel", "AuthenticAMD", "TCGTCGTCGTCG" under QEMU TCG,
     * "KVMKVMKVM\0\0\0" under KVM accel) is always present instead. */
    HwCpuid(0x0u, &regs[0], &regs[1], &regs[2], &regs[3]);
    memcpy(&text[0], &regs[1], 4);
    memcpy(&text[4], &regs[3], 4);
    memcpy(&text[8], &regs[2], 4);
    text[12] = '\0';
    snprintf(out, cap, "%s", text);
    return;
  }

  for (i = 0; i < 3; i++) {
    HwCpuid(0x80000002u + (uint32_t)i,
            &brand[i * 4 + 0],
            &brand[i * 4 + 1],
            &brand[i * 4 + 2],
            &brand[i * 4 + 3]);
  }

  memcpy(text, brand, 48);
  text[48] = '\0';

  /* Brand strings are padded with leading/trailing spaces by convention
   * ("Intel(R) Core(TM) ..." has none, but plenty of virtualized CPUs
   * do) -- trim both ends before handing it to a one-line banner. */
  start = 0;
  while (text[start] == ' ') {
    start++;
  }

  end = strlen(text);
  while (end > start && text[end - 1] == ' ') {
    end--;
  }

  text[end] = '\0';
  snprintf(out, cap, "%s", &text[start]);
}

void pm_metal_hwinfo_print(void)
{
  char                        net_line[640];
  const pm_metal_net_ip_ops_t   *net_ops;
  const pm_metal_audio_ops_t *aud_ops;
  uint32_t                    i;
  uint32_t                    nblk;

  {
    char hostname[PM_METAL_HOST_NAME_MAX];

    if (pm_metal_host_name_get(hostname, sizeof(hostname)) == 0) {
      pm_metal_logf("hwinfo: hostname %s", hostname);
    }
  }

  pm_metal_log("hwinfo: metal devices");
  if (pm_metal_io_dt_count() == 0) {
    pm_metal_log("  (empty)");
  } else {
    pm_metal_io_dt_foreach(HwinfoDtIter, NULL);
  }

  pm_metal_log("hwinfo: backends");
  net_ops = pm_metal_net_ip_get_ops();
  pm_metal_logf("  net ops: %s", (net_ops != NULL && net_ops->name != NULL) ? net_ops->name : "?");
  if (pm_metal_net_ip_if_status(net_line, (uint32_t)sizeof(net_line)) == 0) {
    pm_metal_logf("  net cfg: %s", net_line);
  }

  aud_ops = pm_metal_audio_get_ops();
  pm_metal_logf("  audio ops: %s",
                (aud_ops != NULL && aud_ops->name != NULL) ? aud_ops->name : "?");

  nblk = pm_metal_blk_count();
  pm_metal_logf("  blk devices: %u", nblk);
  for (i = 0; i < nblk; i++) {
    pm_metal_blk_h h;

    h = pm_metal_blk_at(i);
    if (h == PM_METAL_BLK_INVALID) {
      continue;
    }

    pm_metal_logf("    blk#%u  %s  %llu sectors",
                  i,
                  pm_metal_blk_ready(h) ? "ready" : "down",
                  pm_metal_blk_ready(h) ? pm_metal_blk_capacity_sectors(h) : 0);
  }

  HwinfoPrintPci();
}

#if !defined(__wasm__)

static void HwinfoShellCmd(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  pm_metal_hwinfo_print();
  pm_metal_shell_mark_full();
}

PM_METAL_SHELL_CMD(g_pm_metal_shell_cmd_hwinfo,
                   "hwinfo",
                   "hwinfo            metal devices + PCI net/virtio",
                   HwinfoShellCmd);

static NativeSymbol g_pm_metal_hwinfo_native_symbols[] = {
  { "pm_metal_hwinfo_print", (void *)pm_metal_hwinfo_print, "()", NULL },
};

int pm_metal_hwinfo_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_HWINFO_WASI_MODULE,
                                     g_pm_metal_hwinfo_native_symbols,
                                     sizeof(g_pm_metal_hwinfo_native_symbols) /
                                       sizeof(g_pm_metal_hwinfo_native_symbols[0]))) {
    return -1;
  }

  return 0;
}

#endif /* !__wasm__ */
