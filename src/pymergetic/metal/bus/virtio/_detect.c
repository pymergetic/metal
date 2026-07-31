#include <stdint.h>

#include <pymergetic/metal/bus/pci/_cfg.h>
#include <pymergetic/metal/bus/virtio/__init__.h>
#include <pymergetic/metal/dt/__init__.h>

#define PCI_VENDOR_INVALID     0xFFFFu
#define PCI_STATUS_OFF         0x06u
#define PCI_STATUS_CAP_LIST    0x10u
#define PCI_CAP_PTR            0x34u
#define PCI_CAP_ID_VENDOR      0x09u
#define VIRTIO_PCI_CAP_COMMON  1u
#define VIRTIO_PCI_CAP_NOTIFY  2u
#define MAX_BUS                7u

typedef struct {
  uint16_t modern;
  uint16_t legacy;
  pm_metal_dt_class_t class_;
  const uint8_t *compat;
} virtio_kind_t;

static const uint8_t k_net[] = "virtio-net";
static const uint8_t k_blk[] = "virtio-blk";
static const uint8_t k_console[] = "virtio-console";
static const uint8_t k_tablet[] = "virtio-tablet";

static const virtio_kind_t k_kinds[] = {
  { PM_METAL_VIRTIO_DEV_NET, PM_METAL_VIRTIO_DEV_NET_LEGACY, PM_METAL_DT_CLASS_NET, k_net },
  { PM_METAL_VIRTIO_DEV_BLK, PM_METAL_VIRTIO_DEV_BLK_LEGACY, PM_METAL_DT_CLASS_BLK, k_blk },
  { PM_METAL_VIRTIO_DEV_CONSOLE, PM_METAL_VIRTIO_DEV_CONSOLE_LEGACY, PM_METAL_DT_CLASS_STREAM,
    k_console },
  { PM_METAL_VIRTIO_DEV_INPUT, 0u, PM_METAL_DT_CLASS_INPUT, k_tablet },
};

static int32_t has_virtio_cap(uint8_t bus, uint8_t dev, uint8_t func, uint8_t want_type)
{
  uint16_t status;
  uint8_t ptr;
  uint32_t guard;

  status = pm_metal_bus_pci_read16(bus, dev, func, PCI_STATUS_OFF);
  if ((status & PCI_STATUS_CAP_LIST) == 0u) {
    return 0;
  }
  ptr = (uint8_t)(pm_metal_bus_pci_read8(bus, dev, func, PCI_CAP_PTR) & 0xFCu);
  for (guard = 0u; ptr >= 0x40u && ptr != 0xFFu && guard < 48u; guard++) {
    uint8_t id = pm_metal_bus_pci_read8(bus, dev, func, ptr);
    if (id == PCI_CAP_ID_VENDOR) {
      uint8_t cfg_type = pm_metal_bus_pci_read8(bus, dev, func, (uint8_t)(ptr + 3u));
      if (cfg_type == want_type) {
        return 1;
      }
    }
    ptr = (uint8_t)(pm_metal_bus_pci_read8(bus, dev, func, (uint8_t)(ptr + 1u)) & 0xFCu);
  }
  return 0;
}

static const virtio_kind_t *match_kind(uint16_t device)
{
  uint32_t i;
  for (i = 0; i < (uint32_t)(sizeof(k_kinds) / sizeof(k_kinds[0])); i++) {
    if (device == k_kinds[i].modern || (k_kinds[i].legacy != 0u && device == k_kinds[i].legacy)) {
      return &k_kinds[i];
    }
  }
  return NULL;
}

static int32_t already_listed(uint8_t bus, uint8_t dev, uint8_t func)
{
  uint32_t n = pm_metal_dt_count();
  uint32_t i;
  for (i = 0; i < n; i++) {
    const DtNode *node = pm_metal_dt_get(i);
    if (node == NULL || node->bus != PM_METAL_DT_BUS_PCI) {
      continue;
    }
    if (node->loc[0] == bus && node->loc[1] == dev && node->loc[2] == func) {
      return 1;
    }
  }
  return 0;
}

static void probe_bdf(uint8_t bus, uint8_t dev, uint8_t func)
{
  uint16_t vendor;
  uint16_t device;
  const virtio_kind_t *kind;
  DtNode node;

  vendor = pm_metal_bus_pci_read16(bus, dev, func, 0u);
  if (vendor != PM_METAL_VIRTIO_VENDOR) {
    return;
  }
  device = pm_metal_bus_pci_read16(bus, dev, func, 2u);
  kind = match_kind(device);
  if (kind == NULL || already_listed(bus, dev, func)) {
    return;
  }
  if (!has_virtio_cap(bus, dev, func, VIRTIO_PCI_CAP_COMMON) ||
      !has_virtio_cap(bus, dev, func, VIRTIO_PCI_CAP_NOTIFY)) {
    return;
  }
  node.class  = kind->class_;
  node.compat = kind->compat;
  node.unit   = 0;
  node.caps   = 0;
  node.bus    = PM_METAL_DT_BUS_PCI;
  node.loc[0] = bus;
  node.loc[1] = dev;
  node.loc[2] = func;
  node.loc[3] = ((uint32_t)vendor << 16) | (uint32_t)device;
  (void)pm_metal_dt_add(&node);
}

int32_t pm_metal_bus_virtio_detect(void)
{
  uint8_t bus;
  uint8_t dev;

  for (bus = 0u; bus <= MAX_BUS; bus++) {
    for (dev = 0u; dev < 32u; dev++) {
      uint16_t vendor;
      uint8_t hdr;
      uint8_t fmax;
      uint8_t func;

      vendor = pm_metal_bus_pci_read16(bus, dev, 0u, 0u);
      if (vendor == PCI_VENDOR_INVALID) {
        continue;
      }
      hdr = pm_metal_bus_pci_read8(bus, dev, 0u, 0x0Eu);
      fmax = ((hdr & 0x80u) != 0u) ? 8u : 1u;
      for (func = 0u; func < fmax; func++) {
        probe_bdf(bus, dev, func);
      }
    }
  }
  return 0;
}
