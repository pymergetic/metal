/** @file
  Virtio 1.0 PCI modern transport via EFI_PCI_IO. (shared; CF8/CFC PCI + MMIO)
**/
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/bus/virtio/__init__.h>
#include <pymergetic/metal/bus/pci/_cfg.h>
#include <pymergetic/metal/mem/__init__.h>

#define PM_METAL_MEM_PAGE_SIZE 4096u
static inline void pm_metal_mem_fence(void) { __asm__ volatile("mfence" ::: "memory"); }

/* PCI config-space header offsets/bits (PCI 2.2 spec, IndustryStandard/Pci.h
 * values) — plain numeric constants, not an EDK2 touchpoint. */
#define VIRTIO_PCI_CAP_ID_VENDOR     0x09u
#define PCI_VENDOR_ID_OFFSET         0x00u
#define PCI_DEVICE_ID_OFFSET         0x02u
#define PCI_HEADER_TYPE_OFFSET       0x0Eu
#define HEADER_TYPE_MULTI_FUNCTION   0x80u
#define PCI_CAPBILITY_POINTER_OFFSET 0x34u

#define VIRTIO_PCI_CAP_COMMON_CFG 1u
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2u
#define VIRTIO_PCI_CAP_DEVICE_CFG 4u

/* Local status codes for the internal CfgRd/CfgWr/FindCap/TryOpenBdf/ProbeBdf
 * helpers below (all static — this is an internal convention, not an ABI). */
#define PM_ST_OK           0
#define PM_ST_UNSUPPORTED  (-1)
#define PM_ST_DEVICE_ERROR (-2)
#define PM_ST_NOT_FOUND    (-3)
#define PM_ST_FAILED(s)    ((s) != 0)

/* efi: real EFI_PHYSICAL_ADDRESS/EFI_STATUS + gBS->Allocate/FreePages;
 * bios: no boot-services page pool exists, always defers to the arena.
 * impl: src/{efi,bios}/pymergetic/metal/bus/virtio/virtio_pci_port.c */
void *pm_metal_virtio_port_alloc_pages(unsigned pages);
bool  pm_metal_virtio_port_free_pages(void *buf, unsigned pages);

#pragma pack(1)
typedef struct {
  uint8_t  CapId;
  uint8_t  Next;
  uint8_t  CapLen;
  uint8_t  ConfigType;
  uint8_t  Bar;
  uint8_t  Pad[3];
  uint32_t Offset;
  uint32_t Length;
} metal_virtio_pci_cap_t;

typedef struct {
  uint32_t DeviceFeatureSelect;
  uint32_t DeviceFeature;
  uint32_t DriverFeatureSelect;
  uint32_t DriverFeature;
  uint16_t MsixConfig;
  uint16_t NumQueues;
  uint8_t  DeviceStatus;
  uint8_t  ConfigGeneration;
  uint16_t QueueSelect;
  uint16_t QueueSize;
  uint16_t QueueMsixVector;
  uint16_t QueueEnable;
  uint16_t QueueNotifyOff;
  uint64_t QueueDesc;
  uint64_t QueueAvail;
  uint64_t QueueUsed;
} metal_virtio_common_cfg_t;

typedef struct {
  uint64_t Addr;
  uint32_t Len;
  uint16_t Flags;
  uint16_t Next;
} metal_vring_desc_t;

typedef struct {
  uint16_t Flags;
  uint16_t Idx;
  uint16_t Ring[];
} metal_vring_avail_t;

typedef struct {
  uint32_t Id;
  uint32_t Len;
} metal_vring_used_elem_t;

typedef struct {
  uint16_t                Flags;
  uint16_t                Idx;
  metal_vring_used_elem_t Ring[];
} metal_vring_used_t;
#pragma pack()

#define VRING_DESC_F_NEXT  1u
#define VRING_DESC_F_WRITE 2u

typedef struct {
  uint8_t           Bus;
  uint8_t           Dev;
  uint8_t           Func;
  uint8_t           CommonBar;
  uint32_t          CommonOff;
  uint8_t           NotifyBar;
  uint32_t          NotifyOff;
  uint32_t          NotifyMult;
  uint8_t           DeviceBar;
  uint32_t          DeviceOff;
  uint32_t          DeviceLen;
  uint8_t          *CommonBase;
  uint8_t          *NotifyBase;
  uint8_t          *DeviceBase;
  int32_t           UseMmio;
  uint64_t          Features;
  pm_metal_virtq_t *Vqs;
  uint16_t          NVqs;
  uint16_t          PciDeviceId;
} metal_vdev_priv_t;

/* Store priv at start of opaque fields via cast from pm_metal_virtio_dev_t */
static metal_vdev_priv_t *Priv(pm_metal_virtio_dev_t *dev)
{
  return (metal_vdev_priv_t *)(uintptr_t)dev->pci_io;
}

void *pm_metal_virtio_pages_alloc(unsigned pages)
{
  void *p;

  if (pages == 0) {
    return NULL;
  }

  p = pm_metal_virtio_port_alloc_pages(pages);
  if (p != NULL) {
    return p;
  }

  return pm_metal_mem_memalign(PM_METAL_MEM_PAGE_SIZE, (size_t)pages * PM_METAL_MEM_PAGE_SIZE);
}

void pm_metal_virtio_pages_free(void *buf, unsigned pages)
{
  if (buf == NULL || pages == 0) {
    return;
  }

  if (pm_metal_virtio_port_free_pages(buf, pages)) {
    return;
  }

  pm_metal_mem_free(buf);
}

static int CfgRd32(metal_vdev_priv_t *p, uint32_t Off, uint32_t *Val)
{
  if (p->UseMmio && p->CommonBase != NULL) {
    *Val = *(volatile uint32_t *)(p->CommonBase + Off);
    return PM_ST_OK;
  }

  return PM_ST_UNSUPPORTED;
}

static int CfgWr32(metal_vdev_priv_t *p, uint32_t Off, uint32_t Val)
{
  if (p->UseMmio && p->CommonBase != NULL) {
    *(volatile uint32_t *)(p->CommonBase + Off) = Val;
    pm_metal_mem_fence();
    return PM_ST_OK;
  }

  return PM_ST_UNSUPPORTED;
}

static int CfgRd16(metal_vdev_priv_t *p, uint32_t Off, uint16_t *Val)
{
  if (p->UseMmio && p->CommonBase != NULL) {
    *Val = *(volatile uint16_t *)(p->CommonBase + Off);
    return PM_ST_OK;
  }

  return PM_ST_UNSUPPORTED;
}

static int CfgWr16(metal_vdev_priv_t *p, uint32_t Off, uint16_t Val)
{
  if (p->UseMmio && p->CommonBase != NULL) {
    *(volatile uint16_t *)(p->CommonBase + Off) = Val;
    pm_metal_mem_fence();
    return PM_ST_OK;
  }

  return PM_ST_UNSUPPORTED;
}

static int CfgRd8(metal_vdev_priv_t *p, uint32_t Off, uint8_t *Val)
{
  if (p->UseMmio && p->CommonBase != NULL) {
    *Val = *(volatile uint8_t *)(p->CommonBase + Off);
    return PM_ST_OK;
  }

  return PM_ST_UNSUPPORTED;
}

static int CfgWr8(metal_vdev_priv_t *p, uint32_t Off, uint8_t Val)
{
  if (p->UseMmio && p->CommonBase != NULL) {
    *(volatile uint8_t *)(p->CommonBase + Off) = Val;
    pm_metal_mem_fence();
    return PM_ST_OK;
  }

  return PM_ST_UNSUPPORTED;
}

static int CfgWr64(metal_vdev_priv_t *p, uint32_t Off, uint64_t Val)
{
  uint32_t Lo;
  uint32_t Hi;

  Lo = (uint32_t)Val;
  Hi = (uint32_t)(Val >> 32);
  if (PM_ST_FAILED(CfgWr32(p, Off, Lo))) {
    return PM_ST_DEVICE_ERROR;
  }

  return CfgWr32(p, Off + 4, Hi);
}

static int FindCap(uint8_t                 Bus,
                   uint8_t                 Dev,
                   uint8_t                 Func,
                   uint8_t                 Type,
                   metal_virtio_pci_cap_t *Out,
                   uint32_t               *Extra32)
{
  uint8_t Ptr;
  uint8_t Id;

  Ptr = pm_metal_bus_pci_read8(Bus, Dev, Func, PCI_CAPBILITY_POINTER_OFFSET);
  while (Ptr >= 0x40 && Ptr != 0xff) {
    Id = pm_metal_bus_pci_read8(Bus, Dev, Func, Ptr);
    if (Id == VIRTIO_PCI_CAP_ID_VENDOR) {
      metal_virtio_pci_cap_t Cap;
      uint8_t                Buf[sizeof(Cap) + 4];
      uintptr_t              i;

      memset(Buf, 0, sizeof(Buf));
      for (i = 0; i < sizeof(Buf); i++) {
        Buf[i] = pm_metal_bus_pci_read8(Bus, Dev, Func, (uint8_t)(Ptr + i));
      }

      memcpy(&Cap, Buf, sizeof(Cap));
      if (Cap.ConfigType == Type) {
        memcpy(Out, &Cap, sizeof(Cap));
        if (Extra32 != NULL) {
          memcpy(Extra32, Buf + sizeof(Cap), 4);
        }

        return PM_ST_OK;
      }
    }

    Ptr = pm_metal_bus_pci_read8(Bus, Dev, Func, (uint8_t)(Ptr + 1));
  }

  return PM_ST_NOT_FOUND;
}

static int TryOpenBdf(
  uint8_t Bus, uint8_t Dev, uint8_t Func, uint16_t WantId, pm_metal_virtio_dev_t *Out)
{
  uint16_t               VendorId;
  uint16_t               DeviceId;
  metal_virtio_pci_cap_t Common;
  metal_virtio_pci_cap_t Notify;
  metal_virtio_pci_cap_t Device;
  uint32_t               NotifyMult;
  metal_vdev_priv_t     *p;
  uint64_t               BarBase;
  uint8_t                Consumed;

  VendorId = pm_metal_bus_pci_read16(Bus, Dev, Func, PCI_VENDOR_ID_OFFSET);
  if (VendorId != PM_METAL_VIRTIO_VENDOR) {
    return PM_ST_UNSUPPORTED;
  }

  DeviceId = pm_metal_bus_pci_read16(Bus, Dev, Func, PCI_DEVICE_ID_OFFSET);
  if (DeviceId != WantId) {
    return PM_ST_UNSUPPORTED;
  }

  pm_metal_bus_pci_enable_mem_bm(Bus, Dev, Func);

  NotifyMult = 0;
  if (PM_ST_FAILED(FindCap(Bus, Dev, Func, VIRTIO_PCI_CAP_COMMON_CFG, &Common, NULL))) {
    return PM_ST_NOT_FOUND;
  }

  if (PM_ST_FAILED(FindCap(Bus, Dev, Func, VIRTIO_PCI_CAP_NOTIFY_CFG, &Notify, &NotifyMult))) {
    return PM_ST_NOT_FOUND;
  }

  memset(&Device, 0, sizeof(Device));
  (void)FindCap(Bus, Dev, Func, VIRTIO_PCI_CAP_DEVICE_CFG, &Device, NULL);

  p = (metal_vdev_priv_t *)pm_metal_mem_alloc(sizeof(*p));
  if (p == NULL) {
    return PM_ST_UNSUPPORTED;
  }

  memset(p, 0, sizeof(*p));

  p->Bus         = Bus;
  p->Dev         = Dev;
  p->Func        = Func;
  p->CommonBar   = Common.Bar;
  p->CommonOff   = Common.Offset;
  p->NotifyBar   = Notify.Bar;
  p->NotifyOff   = Notify.Offset;
  p->NotifyMult  = NotifyMult ? NotifyMult : 1;
  p->DeviceBar   = Device.Bar;
  p->DeviceOff   = Device.Offset;
  p->DeviceLen   = Device.Length;
  p->PciDeviceId = DeviceId;
  p->UseMmio     = 1;

  BarBase = pm_metal_bus_pci_bar_mmio(Bus, Dev, Func, Common.Bar, &Consumed);
  if (BarBase == 0) {
    pm_metal_mem_free((uint8_t *)p);
    return PM_ST_UNSUPPORTED;
  }

  p->CommonBase = (uint8_t *)(uintptr_t)(BarBase + Common.Offset);

  BarBase = pm_metal_bus_pci_bar_mmio(Bus, Dev, Func, Notify.Bar, &Consumed);
  if (BarBase == 0) {
    pm_metal_mem_free((uint8_t *)p);
    return PM_ST_UNSUPPORTED;
  }

  p->NotifyBase = (uint8_t *)(uintptr_t)(BarBase + Notify.Offset);

  if (Device.Length != 0) {
    BarBase = pm_metal_bus_pci_bar_mmio(Bus, Dev, Func, Device.Bar, &Consumed);
    if (BarBase != 0) {
      p->DeviceBase = (uint8_t *)(uintptr_t)(BarBase + Device.Offset);
    }
  }

  memset(Out, 0, sizeof(*Out));
  Out->pci_io          = (void *)(uintptr_t)p;
  Out->pci_device_id   = DeviceId;
  Out->common          = p->CommonBase;
  Out->notify          = p->NotifyBase;
  Out->device_cfg      = p->DeviceBase;
  Out->notify_off_mult = p->NotifyMult;
  Out->mmio            = 1;

  /* Reset + ack + driver */
  (void)CfgWr8(p, offsetof(metal_virtio_common_cfg_t, DeviceStatus), 0);
  (void)CfgWr8(p,
               offsetof(metal_virtio_common_cfg_t, DeviceStatus),
               (uint8_t)(PM_METAL_VIRTIO_S_ACK | PM_METAL_VIRTIO_S_DRIVER));

  return PM_ST_OK;
}

static int ProbeBdf(uint8_t Bus, uint8_t Dev, uint8_t Func, uint16_t WantId)
{
  uint16_t               VendorId;
  uint16_t               DeviceId;
  metal_virtio_pci_cap_t Common;
  metal_virtio_pci_cap_t Notify;

  VendorId = pm_metal_bus_pci_read16(Bus, Dev, Func, PCI_VENDOR_ID_OFFSET);
  if (VendorId != PM_METAL_VIRTIO_VENDOR) {
    return PM_ST_UNSUPPORTED;
  }

  DeviceId = pm_metal_bus_pci_read16(Bus, Dev, Func, PCI_DEVICE_ID_OFFSET);
  if (DeviceId != WantId) {
    return PM_ST_UNSUPPORTED;
  }

  if (PM_ST_FAILED(FindCap(Bus, Dev, Func, VIRTIO_PCI_CAP_COMMON_CFG, &Common, NULL))) {
    return PM_ST_NOT_FOUND;
  }

  if (PM_ST_FAILED(FindCap(Bus, Dev, Func, VIRTIO_PCI_CAP_NOTIFY_CFG, &Notify, NULL))) {
    return PM_ST_NOT_FOUND;
  }

  return PM_ST_OK;
}

int pm_metal_virtio_find(uint16_t pci_device_id)
{
  uint8_t Bus;
  uint8_t Dev;
  uint8_t Func;

  for (Bus = 0; Bus < 8; Bus++) {
    for (Dev = 0; Dev < 32; Dev++) {
      uint8_t Fmax;
      uint8_t Hdr;

      if (pm_metal_bus_pci_read16(Bus, Dev, 0, PCI_VENDOR_ID_OFFSET) == 0xffff) {
        continue;
      }

      Hdr  = pm_metal_bus_pci_read8(Bus, Dev, 0, PCI_HEADER_TYPE_OFFSET);
      Fmax = (Hdr & HEADER_TYPE_MULTI_FUNCTION) ? 8 : 1;
      for (Func = 0; Func < Fmax; Func++) {
        if (ProbeBdf(Bus, Dev, Func, pci_device_id) == PM_ST_OK) {
          return 0;
        }
      }
    }
  }

  return -1;
}

int pm_metal_virtio_open(uint16_t pci_device_id, pm_metal_virtio_dev_t *out)
{
  uint8_t Bus;
  uint8_t Dev;
  uint8_t Func;

  if (out == NULL) {
    return -1;
  }

  for (Bus = 0; Bus < 8; Bus++) {
    for (Dev = 0; Dev < 32; Dev++) {
      uint8_t Fmax;
      uint8_t Hdr;

      if (pm_metal_bus_pci_read16(Bus, Dev, 0, PCI_VENDOR_ID_OFFSET) == 0xffff) {
        continue;
      }

      Hdr  = pm_metal_bus_pci_read8(Bus, Dev, 0, PCI_HEADER_TYPE_OFFSET);
      Fmax = (Hdr & HEADER_TYPE_MULTI_FUNCTION) ? 8 : 1;
      for (Func = 0; Func < Fmax; Func++) {
        if (TryOpenBdf(Bus, Dev, Func, pci_device_id, out) == PM_ST_OK) {
          return 0;
        }
      }
    }
  }

  return -1;
}

void pm_metal_virtio_close(pm_metal_virtio_dev_t *dev)
{
  metal_vdev_priv_t *p;
  uint16_t           i;

  if (dev == NULL || dev->pci_io == NULL) {
    return;
  }

  p = Priv(dev);
  (void)CfgWr8(p, offsetof(metal_virtio_common_cfg_t, DeviceStatus), 0);

  if (p->Vqs != NULL) {
    for (i = 0; i < p->NVqs; i++) {
      if (p->Vqs[i].ring_mem != NULL) {
        pm_metal_virtio_pages_free(p->Vqs[i].ring_mem, p->Vqs[i].ring_pages);
      }

      if (p->Vqs[i].next != NULL) {
        pm_metal_mem_free((uint8_t *)p->Vqs[i].next);
      }
    }

    pm_metal_mem_free((uint8_t *)p->Vqs);
  }

  pm_metal_mem_free((uint8_t *)p);
  memset(dev, 0, sizeof(*dev));
}

uint64_t pm_metal_virtio_get_features(pm_metal_virtio_dev_t *dev)
{
  metal_vdev_priv_t *p;
  uint32_t           Lo;
  uint32_t           Hi;

  if (dev == NULL || dev->pci_io == NULL) {
    return 0;
  }

  p = Priv(dev);
  (void)CfgWr32(p, offsetof(metal_virtio_common_cfg_t, DeviceFeatureSelect), 0);
  (void)CfgRd32(p, offsetof(metal_virtio_common_cfg_t, DeviceFeature), &Lo);
  (void)CfgWr32(p, offsetof(metal_virtio_common_cfg_t, DeviceFeatureSelect), 1);
  (void)CfgRd32(p, offsetof(metal_virtio_common_cfg_t, DeviceFeature), &Hi);
  return ((uint64_t)Hi << 32) | Lo;
}

int pm_metal_virtio_set_features(pm_metal_virtio_dev_t *dev, uint64_t features)
{
  metal_vdev_priv_t *p;
  uint8_t            St;

  if (dev == NULL || dev->pci_io == NULL) {
    return -1;
  }

  p = Priv(dev);
  (void)CfgWr32(p, offsetof(metal_virtio_common_cfg_t, DriverFeatureSelect), 0);
  (void)CfgWr32(p, offsetof(metal_virtio_common_cfg_t, DriverFeature), (uint32_t)features);
  (void)CfgWr32(p, offsetof(metal_virtio_common_cfg_t, DriverFeatureSelect), 1);
  (void)CfgWr32(p, offsetof(metal_virtio_common_cfg_t, DriverFeature), (uint32_t)(features >> 32));

  (void)CfgRd8(p, offsetof(metal_virtio_common_cfg_t, DeviceStatus), &St);
  St |= PM_METAL_VIRTIO_S_FEATURES;
  (void)CfgWr8(p, offsetof(metal_virtio_common_cfg_t, DeviceStatus), St);
  (void)CfgRd8(p, offsetof(metal_virtio_common_cfg_t, DeviceStatus), &St);
  if ((St & PM_METAL_VIRTIO_S_FEATURES) == 0) {
    return -1;
  }

  p->Features   = features;
  dev->features = features;
  return 0;
}

void pm_metal_virtio_set_status(pm_metal_virtio_dev_t *dev, uint8_t status)
{
  if (dev == NULL || dev->pci_io == NULL) {
    return;
  }

  (void)CfgWr8(Priv(dev), offsetof(metal_virtio_common_cfg_t, DeviceStatus), status);
}

uint8_t pm_metal_virtio_get_status(pm_metal_virtio_dev_t *dev)
{
  uint8_t St;

  if (dev == NULL || dev->pci_io == NULL) {
    return 0;
  }

  St = 0;
  (void)CfgRd8(Priv(dev), offsetof(metal_virtio_common_cfg_t, DeviceStatus), &St);
  return St;
}

int pm_metal_virtio_driver_ok(pm_metal_virtio_dev_t *dev)
{
  uint8_t St;

  St = pm_metal_virtio_get_status(dev);
  St |= PM_METAL_VIRTIO_S_DRIVER_OK;
  pm_metal_virtio_set_status(dev, St);
  return 0;
}

int pm_metal_virtio_setup_queue(pm_metal_virtio_dev_t *dev, uint16_t qidx, uint16_t want_size)
{
  metal_vdev_priv_t *p;
  pm_metal_virtq_t  *vq;
  uint16_t           Qsz;
  uintptr_t          DescBytes;
  uintptr_t          AvailBytes;
  uintptr_t          UsedBytes;
  uintptr_t          Total;
  uintptr_t          Pages;
  uint8_t           *Mem;
  uint16_t           i;

  if (dev == NULL || dev->pci_io == NULL) {
    return -1;
  }

  p = Priv(dev);
  (void)CfgWr16(p, offsetof(metal_virtio_common_cfg_t, QueueSelect), qidx);
  Qsz = 0;
  (void)CfgRd16(p, offsetof(metal_virtio_common_cfg_t, QueueSize), &Qsz);
  if (Qsz == 0) {
    return -1;
  }

  if (want_size > 0 && want_size < Qsz) {
    Qsz = want_size;
  }

  if (p->Vqs == NULL) {
    p->Vqs = (pm_metal_virtq_t *)pm_metal_mem_alloc(
      sizeof(pm_metal_virtq_t) * 8u);
    if (p->Vqs == NULL) {
      return -1;
    }

    memset(p->Vqs, 0, sizeof(pm_metal_virtq_t) * 8u);
    p->NVqs    = 8;
    dev->vqs   = p->Vqs;
    dev->n_vqs = 8;
  }

  if (qidx >= p->NVqs) {
    return -1;
  }

  vq         = &p->Vqs[qidx];
  DescBytes  = sizeof(metal_vring_desc_t) * Qsz;
  AvailBytes = sizeof(uint16_t) * (3u + Qsz);
  UsedBytes  = sizeof(uint16_t) * 3u + sizeof(metal_vring_used_elem_t) * Qsz;
  Total      = DescBytes + AvailBytes + 4096u + UsedBytes;
  Pages      = PM_METAL_VIRTIO_SIZE_TO_PAGES(Total);
  Mem        = pm_metal_virtio_pages_alloc((unsigned)Pages);
  if (Mem == NULL) {
    return -1;
  }

  memset(Mem, 0, Pages * PM_METAL_MEM_PAGE_SIZE);
  vq->qidx       = qidx;
  vq->size       = Qsz;
  vq->ring_mem   = Mem;
  vq->ring_pages = (uint32_t)Pages;
  vq->desc       = Mem;
  vq->avail      = Mem + DescBytes;
  vq->used       = Mem + ((DescBytes + AvailBytes + 4095u) & ~4095u);
  vq->desc_phys  = (uint64_t)(uintptr_t)vq->desc;
  vq->avail_phys = (uint64_t)(uintptr_t)vq->avail;
  vq->used_phys  = (uint64_t)(uintptr_t)vq->used;
  vq->free_head  = 0;
  vq->num_free   = Qsz;
  vq->last_used  = 0;
  vq->next =
    (uint16_t *)pm_metal_mem_alloc(sizeof(uint16_t) * Qsz);
  if (vq->next == NULL) {
    pm_metal_virtio_pages_free(Mem, (unsigned)Pages);
    return -1;
  }

  for (i = 0; i < Qsz - 1; i++) {
    vq->next[i] = (uint16_t)(i + 1);
  }

  vq->next[Qsz - 1] = 0xffff;

  (void)CfgWr16(p, offsetof(metal_virtio_common_cfg_t, QueueSelect), qidx);
  (void)CfgWr16(p, offsetof(metal_virtio_common_cfg_t, QueueSize), Qsz);
  (void)CfgWr64(p, offsetof(metal_virtio_common_cfg_t, QueueDesc), vq->desc_phys);
  (void)CfgWr64(p, offsetof(metal_virtio_common_cfg_t, QueueAvail), vq->avail_phys);
  (void)CfgWr64(p, offsetof(metal_virtio_common_cfg_t, QueueUsed), vq->used_phys);
  (void)CfgRd16(p, offsetof(metal_virtio_common_cfg_t, QueueNotifyOff), &vq->notify_off);
  (void)CfgWr16(p, offsetof(metal_virtio_common_cfg_t, QueueEnable), 1);
  return 0;
}

int pm_metal_virtio_queue_reset(pm_metal_virtio_dev_t *dev, uint16_t qidx)
{
  metal_vdev_priv_t   *p;
  pm_metal_virtq_t    *vq;
  metal_vring_avail_t *Avail;
  metal_vring_used_t  *Used;
  metal_vring_desc_t  *Desc;
  uint16_t             i;
  uint8_t              St;

  if (dev == NULL || dev->pci_io == NULL) {
    return -1;
  }

  p = Priv(dev);
  if (p->Vqs == NULL || qidx >= p->NVqs) {
    return -1;
  }

  vq = &p->Vqs[qidx];
  if (vq->ring_mem == NULL || vq->size == 0) {
    return -1;
  }

  Avail = (metal_vring_avail_t *)vq->avail;
  Used  = (metal_vring_used_t *)vq->used;
  Desc  = (metal_vring_desc_t *)vq->desc;
  memset(Desc, 0, sizeof(*Desc) * vq->size);
  Avail->Flags  = 0;
  Avail->Idx    = 0;
  Used->Flags   = 0;
  Used->Idx     = 0;
  vq->last_used = 0;
  vq->free_head = 0;
  vq->num_free  = vq->size;
  for (i = 0; i < vq->size - 1; i++) {
    vq->next[i] = (uint16_t)(i + 1u);
  }

  vq->next[vq->size - 1] = 0xffff;

  (void)CfgWr16(p, offsetof(metal_virtio_common_cfg_t, QueueSelect), qidx);
  (void)CfgWr16(p, offsetof(metal_virtio_common_cfg_t, QueueEnable), 0);
  (void)CfgWr64(p, offsetof(metal_virtio_common_cfg_t, QueueDesc), vq->desc_phys);
  (void)CfgWr64(p, offsetof(metal_virtio_common_cfg_t, QueueAvail), vq->avail_phys);
  (void)CfgWr64(p, offsetof(metal_virtio_common_cfg_t, QueueUsed), vq->used_phys);
  (void)CfgWr16(p, offsetof(metal_virtio_common_cfg_t, QueueSize), vq->size);
  (void)CfgRd16(p, offsetof(metal_virtio_common_cfg_t, QueueNotifyOff), &vq->notify_off);
  (void)CfgWr16(p, offsetof(metal_virtio_common_cfg_t, QueueEnable), 1);

  St = pm_metal_virtio_get_status(dev);
  St |= PM_METAL_VIRTIO_S_DRIVER_OK;
  pm_metal_virtio_set_status(dev, St);
  return 0;
}

static void MetalVirtioRemapBars(metal_vdev_priv_t *p)
{
  uint64_t BarBase;
  uint8_t  Consumed;

  pm_metal_bus_pci_enable_mem_bm(p->Bus, p->Dev, p->Func);

  BarBase = pm_metal_bus_pci_bar_mmio(p->Bus, p->Dev, p->Func, p->CommonBar, &Consumed);
  if (BarBase != 0) {
    p->CommonBase = (uint8_t *)(uintptr_t)(BarBase + p->CommonOff);
  }

  BarBase = pm_metal_bus_pci_bar_mmio(p->Bus, p->Dev, p->Func, p->NotifyBar, &Consumed);
  if (BarBase != 0) {
    p->NotifyBase = (uint8_t *)(uintptr_t)(BarBase + p->NotifyOff);
  }

  if (p->DeviceLen != 0) {
    BarBase = pm_metal_bus_pci_bar_mmio(p->Bus, p->Dev, p->Func, p->DeviceBar, &Consumed);
    if (BarBase != 0) {
      p->DeviceBase = (uint8_t *)(uintptr_t)(BarBase + p->DeviceOff);
    }
  }
}

int pm_metal_virtio_post_ebs_resume(pm_metal_virtio_dev_t *dev, uint16_t qidx)
{
  metal_vdev_priv_t *p;
  uint64_t           feats;

  if (dev == NULL || dev->pci_io == NULL) {
    return -1;
  }

  p = Priv(dev);
  MetalVirtioRemapBars(p);
  dev->common     = p->CommonBase;
  dev->notify     = p->NotifyBase;
  dev->device_cfg = p->DeviceBase;

  feats = (dev->features != 0) ? dev->features : p->Features;
  pm_metal_virtio_set_status(dev, 0);
  pm_metal_virtio_set_status(dev, (uint8_t)(PM_METAL_VIRTIO_S_ACK | PM_METAL_VIRTIO_S_DRIVER));
  if (pm_metal_virtio_set_features(dev, feats) != 0) {
    return -1;
  }

  return pm_metal_virtio_queue_reset(dev, qidx);
}

int pm_metal_virtq_add(
  pm_metal_virtq_t *vq, void *buf, uint32_t len, int device_writeable, uint16_t *head_out)
{
  metal_vring_desc_t  *Desc;
  metal_vring_avail_t *Avail;
  uint16_t             Head;
  uint16_t             Aidx;

  if (vq == NULL || buf == NULL || len == 0 || vq->num_free == 0) {
    return -1;
  }

  Head          = vq->free_head;
  vq->free_head = vq->next[Head];
  vq->num_free--;

  Desc             = (metal_vring_desc_t *)vq->desc;
  Desc[Head].Addr  = (uint64_t)(uintptr_t)buf;
  Desc[Head].Len   = len;
  Desc[Head].Flags = (uint16_t)(device_writeable ? VRING_DESC_F_WRITE : 0);
  Desc[Head].Next  = 0;

  Avail = (metal_vring_avail_t *)vq->avail;
  Aidx  = Avail->Idx;
  pm_metal_mem_fence();
  Avail->Ring[Aidx % vq->size] = Head;
  pm_metal_mem_fence();
  Avail->Idx = (uint16_t)(Aidx + 1u);

  if (head_out != NULL) {
    *head_out = Head;
  }

  return 0;
}

int pm_metal_virtq_add2(pm_metal_virtq_t *vq,
                        void             *buf0,
                        uint32_t          len0,
                        int               write0,
                        void             *buf1,
                        uint32_t          len1,
                        int               write1,
                        uint16_t         *head_out)
{
  metal_vring_desc_t  *Desc;
  metal_vring_avail_t *Avail;
  uint16_t             Head;
  uint16_t             Next;
  uint16_t             Aidx;

  if (vq == NULL || buf0 == NULL || buf1 == NULL || len0 == 0 || len1 == 0 || vq->num_free < 2) {
    return -1;
  }

  Head          = vq->free_head;
  Next          = vq->next[Head];
  vq->free_head = vq->next[Next];
  vq->num_free  = (uint16_t)(vq->num_free - 2u);

  Desc             = (metal_vring_desc_t *)vq->desc;
  Desc[Head].Addr  = (uint64_t)(uintptr_t)buf0;
  Desc[Head].Len   = len0;
  Desc[Head].Flags = (uint16_t)(VRING_DESC_F_NEXT | (write0 ? VRING_DESC_F_WRITE : 0));
  Desc[Head].Next  = Next;
  Desc[Next].Addr  = (uint64_t)(uintptr_t)buf1;
  Desc[Next].Len   = len1;
  Desc[Next].Flags = (uint16_t)(write1 ? VRING_DESC_F_WRITE : 0);
  Desc[Next].Next  = 0;

  Avail = (metal_vring_avail_t *)vq->avail;
  Aidx  = Avail->Idx;
  pm_metal_mem_fence();
  Avail->Ring[Aidx % vq->size] = Head;
  pm_metal_mem_fence();
  Avail->Idx = (uint16_t)(Aidx + 1u);

  if (head_out != NULL) {
    *head_out = Head;
  }

  return 0;
}

int pm_metal_virtq_add3(pm_metal_virtq_t *vq,
                        void             *buf0,
                        uint32_t          len0,
                        int               write0,
                        void             *buf1,
                        uint32_t          len1,
                        int               write1,
                        void             *buf2,
                        uint32_t          len2,
                        int               write2,
                        uint16_t         *head_out)
{
  metal_vring_desc_t  *Desc;
  metal_vring_avail_t *Avail;
  uint16_t             A;
  uint16_t             B;
  uint16_t             C;
  uint16_t             Aidx;

  if (vq == NULL || buf0 == NULL || buf1 == NULL || buf2 == NULL || len0 == 0 || len1 == 0 ||
      len2 == 0 || vq->num_free < 3) {
    return -1;
  }

  A             = vq->free_head;
  B             = vq->next[A];
  C             = vq->next[B];
  vq->free_head = vq->next[C];
  vq->num_free  = (uint16_t)(vq->num_free - 3u);

  Desc          = (metal_vring_desc_t *)vq->desc;
  Desc[A].Addr  = (uint64_t)(uintptr_t)buf0;
  Desc[A].Len   = len0;
  Desc[A].Flags = (uint16_t)(VRING_DESC_F_NEXT | (write0 ? VRING_DESC_F_WRITE : 0));
  Desc[A].Next  = B;
  Desc[B].Addr  = (uint64_t)(uintptr_t)buf1;
  Desc[B].Len   = len1;
  Desc[B].Flags = (uint16_t)(VRING_DESC_F_NEXT | (write1 ? VRING_DESC_F_WRITE : 0));
  Desc[B].Next  = C;
  Desc[C].Addr  = (uint64_t)(uintptr_t)buf2;
  Desc[C].Len   = len2;
  Desc[C].Flags = (uint16_t)(write2 ? VRING_DESC_F_WRITE : 0);
  Desc[C].Next  = 0;

  Avail = (metal_vring_avail_t *)vq->avail;
  Aidx  = Avail->Idx;
  pm_metal_mem_fence();
  Avail->Ring[Aidx % vq->size] = A;
  pm_metal_mem_fence();
  Avail->Idx = (uint16_t)(Aidx + 1u);

  if (head_out != NULL) {
    *head_out = A;
  }

  return 0;
}

void pm_metal_virtq_kick(pm_metal_virtio_dev_t *dev, pm_metal_virtq_t *vq)
{
  metal_vdev_priv_t *p;
  uint16_t           Sel;
  uint32_t           Off;

  if (dev == NULL || vq == NULL || dev->pci_io == NULL) {
    return;
  }

  p   = Priv(dev);
  Off = (uint32_t)vq->notify_off * p->NotifyMult;
  Sel = vq->qidx;
  if (p->UseMmio && p->NotifyBase != NULL) {
    *(volatile uint16_t *)(p->NotifyBase + Off) = Sel;
    pm_metal_mem_fence();
    return;
  }

  return;
}

int pm_metal_virtq_get_used(pm_metal_virtq_t *vq, uint16_t *head, uint32_t *len)
{
  metal_vring_used_t *Used;
  uint16_t            Uidx;

  if (vq == NULL) {
    return 0;
  }

  Used = (metal_vring_used_t *)vq->used;
  pm_metal_mem_fence();
  Uidx = Used->Idx;
  if (Uidx == vq->last_used) {
    return 0;
  }

  if (head != NULL) {
    *head = (uint16_t)Used->Ring[vq->last_used % vq->size].Id;
  }

  if (len != NULL) {
    *len = Used->Ring[vq->last_used % vq->size].Len;
  }

  vq->last_used = (uint16_t)(vq->last_used + 1u);
  return 1;
}

void pm_metal_virtq_free_chain(pm_metal_virtq_t *vq, uint16_t head)
{
  metal_vring_desc_t *Desc;
  uint16_t            Cur;
  uint16_t            Next;

  if (vq == NULL) {
    return;
  }

  Desc = (metal_vring_desc_t *)vq->desc;
  Cur  = head;
  for (;;) {
    Next          = Desc[Cur].Next;
    vq->next[Cur] = vq->free_head;
    vq->free_head = Cur;
    vq->num_free++;
    if ((Desc[Cur].Flags & VRING_DESC_F_NEXT) == 0) {
      break;
    }

    Cur = Next;
  }
}

int pm_metal_virtio_cfg_read(pm_metal_virtio_dev_t *dev, uint32_t offset, void *buf, uint32_t len)
{
  metal_vdev_priv_t *p;
  uint32_t           i;

  if (dev == NULL || buf == NULL || len == 0 || dev->pci_io == NULL) {
    return -1;
  }

  p = Priv(dev);
  if (p->DeviceLen == 0 || offset + len > p->DeviceLen) {
    return -1;
  }

  if (p->UseMmio && p->DeviceBase != NULL) {
    for (i = 0; i < len; i++) {
      ((uint8_t *)buf)[i] = *(volatile uint8_t *)(p->DeviceBase + offset + i);
    }

    return 0;
  }

  return -1;
}

int pm_metal_virtio_cfg_write(pm_metal_virtio_dev_t *dev,
                              uint32_t               offset,
                              const void            *buf,
                              uint32_t               len)
{
  metal_vdev_priv_t *p;
  uint32_t           i;

  if (dev == NULL || buf == NULL || len == 0 || dev->pci_io == NULL) {
    return -1;
  }

  p = Priv(dev);
  if (p->DeviceLen == 0 || offset + len > p->DeviceLen) {
    return -1;
  }

  if (p->UseMmio && p->DeviceBase != NULL) {
    for (i = 0; i < len; i++) {
      *(volatile uint8_t *)(p->DeviceBase + offset + i) = ((const uint8_t *)buf)[i];
    }

    pm_metal_mem_fence();
    return 0;
  }

  return -1;
}

void pm_metal_virtio_ack_isr(pm_metal_virtio_dev_t *dev)
{
  (void)dev;
}
