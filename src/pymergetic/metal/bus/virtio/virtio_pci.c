/** @file
  Virtio 1.0 PCI modern transport via EFI_PCI_IO. (shared; CF8/CFC PCI + MMIO)
**/
#include <pymergetic/metal/bus/virtio/virtio.h>
#include <runtime/mem/mem.h>
#include <runtime/mem/arena.h>
#include "../pci/pci.h"

#include <Uefi.h>
#include <IndustryStandard/Pci.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>

#define VIRTIO_PCI_CAP_COMMON_CFG  1u
#define VIRTIO_PCI_CAP_NOTIFY_CFG  2u
#define VIRTIO_PCI_CAP_DEVICE_CFG  4u

#pragma pack (1)
typedef struct {
  UINT8   CapId;
  UINT8   Next;
  UINT8   CapLen;
  UINT8   ConfigType;
  UINT8   Bar;
  UINT8   Pad[3];
  UINT32  Offset;
  UINT32  Length;
} metal_virtio_pci_cap_t;

typedef struct {
  UINT32  DeviceFeatureSelect;
  UINT32  DeviceFeature;
  UINT32  DriverFeatureSelect;
  UINT32  DriverFeature;
  UINT16  MsixConfig;
  UINT16  NumQueues;
  UINT8   DeviceStatus;
  UINT8   ConfigGeneration;
  UINT16  QueueSelect;
  UINT16  QueueSize;
  UINT16  QueueMsixVector;
  UINT16  QueueEnable;
  UINT16  QueueNotifyOff;
  UINT64  QueueDesc;
  UINT64  QueueAvail;
  UINT64  QueueUsed;
} metal_virtio_common_cfg_t;

typedef struct {
  UINT64  Addr;
  UINT32  Len;
  UINT16  Flags;
  UINT16  Next;
} metal_vring_desc_t;

typedef struct {
  UINT16  Flags;
  UINT16  Idx;
  UINT16  Ring[];
} metal_vring_avail_t;

typedef struct {
  UINT32  Id;
  UINT32  Len;
} metal_vring_used_elem_t;

typedef struct {
  UINT16  Flags;
  UINT16  Idx;
  metal_vring_used_elem_t  Ring[];
} metal_vring_used_t;
#pragma pack ()

#define VRING_DESC_F_NEXT   1u
#define VRING_DESC_F_WRITE  2u

typedef struct {
  UINT8                 Bus;
  UINT8                 Dev;
  UINT8                 Func;
  UINT8                 CommonBar;
  UINT32                CommonOff;
  UINT8                 NotifyBar;
  UINT32                NotifyOff;
  UINT32                NotifyMult;
  UINT8                 DeviceBar;
  UINT32                DeviceOff;
  UINT32                DeviceLen;
  UINT8                *CommonBase;
  UINT8                *NotifyBase;
  UINT8                *DeviceBase;
  INT32                 UseMmio;
  UINT64                Features;
  pm_metal_virtq_t     *Vqs;
  UINT16                NVqs;
  UINT16                PciDeviceId;
} metal_vdev_priv_t;

/* Store priv at start of opaque fields via cast from pm_metal_virtio_dev_t */
STATIC
metal_vdev_priv_t *
Priv (
  pm_metal_virtio_dev_t  *dev
  )
{
  return (metal_vdev_priv_t *)(UINTN)dev->pci_io;
}

void *
pm_metal_virtio_pages_alloc (
  unsigned  pages
  )
{
  if (pages == 0) {
    return NULL;
  }

#if !defined(BH_PLATFORM_METAL_BIOS)
  if (gBS != NULL) {
    EFI_PHYSICAL_ADDRESS  Pa;
    EFI_STATUS            Status;

    Status = gBS->AllocatePages (
                    AllocateAnyPages,
                    EfiLoaderData,
                    (UINTN)pages,
                    &Pa
                    );
    if (!EFI_ERROR (Status)) {
      return (VOID *)(UINTN)Pa;
    }
  }
#endif

  return pm_metal_arena_map ((size_t)pages * EFI_PAGE_SIZE);
}

void
pm_metal_virtio_pages_free (
  VOID     *buf,
  unsigned  pages
  )
{
  if (buf == NULL || pages == 0) {
    return;
  }

#if !defined(BH_PLATFORM_METAL_BIOS)
  if (gBS != NULL) {
    (VOID)gBS->FreePages ((EFI_PHYSICAL_ADDRESS)(UINTN)buf, (UINTN)pages);
    return;
  }
#endif

  (VOID)pm_metal_arena_unmap (buf, (size_t)pages * EFI_PAGE_SIZE);
}

STATIC
EFI_STATUS
CfgRd32 (
  metal_vdev_priv_t  *p,
  UINT32              Off,
  UINT32             *Val
  )
{
  if (p->UseMmio && p->CommonBase != NULL) {
    *Val = *(volatile UINT32 *)(p->CommonBase + Off);
    return EFI_SUCCESS;
  }

  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
CfgWr32 (
  metal_vdev_priv_t  *p,
  UINT32              Off,
  UINT32              Val
  )
{
  if (p->UseMmio && p->CommonBase != NULL) {
    *(volatile UINT32 *)(p->CommonBase + Off) = Val;
    MemoryFence ();
    return EFI_SUCCESS;
  }

  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
CfgRd16 (
  metal_vdev_priv_t  *p,
  UINT32              Off,
  UINT16             *Val
  )
{
  if (p->UseMmio && p->CommonBase != NULL) {
    *Val = *(volatile UINT16 *)(p->CommonBase + Off);
    return EFI_SUCCESS;
  }

  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
CfgWr16 (
  metal_vdev_priv_t  *p,
  UINT32              Off,
  UINT16              Val
  )
{
  if (p->UseMmio && p->CommonBase != NULL) {
    *(volatile UINT16 *)(p->CommonBase + Off) = Val;
    MemoryFence ();
    return EFI_SUCCESS;
  }

  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
CfgRd8 (
  metal_vdev_priv_t  *p,
  UINT32              Off,
  UINT8              *Val
  )
{
  if (p->UseMmio && p->CommonBase != NULL) {
    *Val = *(volatile UINT8 *)(p->CommonBase + Off);
    return EFI_SUCCESS;
  }

  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
CfgWr8 (
  metal_vdev_priv_t  *p,
  UINT32              Off,
  UINT8               Val
  )
{
  if (p->UseMmio && p->CommonBase != NULL) {
    *(volatile UINT8 *)(p->CommonBase + Off) = Val;
    MemoryFence ();
    return EFI_SUCCESS;
  }

  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
CfgWr64 (
  metal_vdev_priv_t  *p,
  UINT32              Off,
  UINT64              Val
  )
{
  UINT32  Lo;
  UINT32  Hi;

  Lo = (UINT32)Val;
  Hi = (UINT32)(Val >> 32);
  if (EFI_ERROR (CfgWr32 (p, Off, Lo))) {
    return EFI_DEVICE_ERROR;
  }

  return CfgWr32 (p, Off + 4, Hi);
}

STATIC
EFI_STATUS
FindCap (
  UINT8                   Bus,
  UINT8                   Dev,
  UINT8                   Func,
  UINT8                   Type,
  metal_virtio_pci_cap_t  *Out,
  UINT32                 *Extra32
  )
{
  UINT8  Ptr;
  UINT8  Id;

  Ptr = pm_bios_pci_read8 (Bus, Dev, Func, PCI_CAPBILITY_POINTER_OFFSET);
  while (Ptr >= 0x40 && Ptr != 0xff) {
    Id = pm_bios_pci_read8 (Bus, Dev, Func, Ptr);
    if (Id == EFI_PCI_CAPABILITY_ID_VENDOR) {
      metal_virtio_pci_cap_t  Cap;
      UINT8                   Buf[sizeof (Cap) + 4];
      UINTN                   i;

      ZeroMem (Buf, sizeof (Buf));
      for (i = 0; i < sizeof (Buf); i++) {
        Buf[i] = pm_bios_pci_read8 (Bus, Dev, Func, (UINT8)(Ptr + i));
      }

      CopyMem (&Cap, Buf, sizeof (Cap));
      if (Cap.ConfigType == Type) {
        CopyMem (Out, &Cap, sizeof (Cap));
        if (Extra32 != NULL) {
          CopyMem (Extra32, Buf + sizeof (Cap), 4);
        }

        return EFI_SUCCESS;
      }
    }

    Ptr = pm_bios_pci_read8 (Bus, Dev, Func, (UINT8)(Ptr + 1));
  }

  return EFI_NOT_FOUND;
}

STATIC
EFI_STATUS
TryOpenBdf (
  UINT8                  Bus,
  UINT8                  Dev,
  UINT8                  Func,
  UINT16                 WantId,
  pm_metal_virtio_dev_t  *Out
  )
{
  UINT16                  VendorId;
  UINT16                  DeviceId;
  metal_virtio_pci_cap_t  Common;
  metal_virtio_pci_cap_t  Notify;
  metal_virtio_pci_cap_t  Device;
  UINT32                  NotifyMult;
  metal_vdev_priv_t      *p;
  UINT64                  BarBase;
  UINT8                   Consumed;

  VendorId = pm_bios_pci_read16 (Bus, Dev, Func, PCI_VENDOR_ID_OFFSET);
  if (VendorId != PM_METAL_VIRTIO_VENDOR) {
    return EFI_UNSUPPORTED;
  }

  DeviceId = pm_bios_pci_read16 (Bus, Dev, Func, PCI_DEVICE_ID_OFFSET);
  if (DeviceId != WantId) {
    return EFI_UNSUPPORTED;
  }

  pm_bios_pci_enable_mem_bm (Bus, Dev, Func);

  NotifyMult = 0;
  if (EFI_ERROR (FindCap (Bus, Dev, Func, VIRTIO_PCI_CAP_COMMON_CFG, &Common, NULL))) {
    return EFI_NOT_FOUND;
  }

  if (EFI_ERROR (FindCap (Bus, Dev, Func, VIRTIO_PCI_CAP_NOTIFY_CFG, &Notify, &NotifyMult))) {
    return EFI_NOT_FOUND;
  }

  ZeroMem (&Device, sizeof (Device));
  (VOID)FindCap (Bus, Dev, Func, VIRTIO_PCI_CAP_DEVICE_CFG, &Device, NULL);

  p = (metal_vdev_priv_t *)pm_metal_mem_alloc (
                             sizeof (*p),
                             PM_METAL_MEM_HEAP,
                             PM_METAL_MEM_ID_NONE
                             );
  if (p == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem (p, sizeof (*p));

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

  BarBase = pm_bios_pci_bar_mmio (Bus, Dev, Func, Common.Bar, &Consumed);
  if (BarBase == 0) {
    pm_metal_mem_free (p);
    return EFI_UNSUPPORTED;
  }

  p->CommonBase = (UINT8 *)(UINTN)(BarBase + Common.Offset);

  BarBase = pm_bios_pci_bar_mmio (Bus, Dev, Func, Notify.Bar, &Consumed);
  if (BarBase == 0) {
    pm_metal_mem_free (p);
    return EFI_UNSUPPORTED;
  }

  p->NotifyBase = (UINT8 *)(UINTN)(BarBase + Notify.Offset);

  if (Device.Length != 0) {
    BarBase = pm_bios_pci_bar_mmio (Bus, Dev, Func, Device.Bar, &Consumed);
    if (BarBase != 0) {
      p->DeviceBase = (UINT8 *)(UINTN)(BarBase + Device.Offset);
    }
  }

  ZeroMem (Out, sizeof (*Out));
  Out->pci_io        = (VOID *)(UINTN)p;
  Out->pci_device_id = DeviceId;
  Out->common        = p->CommonBase;
  Out->notify        = p->NotifyBase;
  Out->device_cfg    = p->DeviceBase;
  Out->notify_off_mult = p->NotifyMult;
  Out->mmio          = 1;

  /* Reset + ack + driver */
  (VOID)CfgWr8 (p, OFFSET_OF (metal_virtio_common_cfg_t, DeviceStatus), 0);
  (VOID)CfgWr8 (
          p,
          OFFSET_OF (metal_virtio_common_cfg_t, DeviceStatus),
          (UINT8)(PM_METAL_VIRTIO_S_ACK | PM_METAL_VIRTIO_S_DRIVER)
          );

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ProbeBdf (
  UINT8   Bus,
  UINT8   Dev,
  UINT8   Func,
  UINT16  WantId
  )
{
  UINT16  VendorId;
  UINT16  DeviceId;
  metal_virtio_pci_cap_t  Common;
  metal_virtio_pci_cap_t  Notify;

  VendorId = pm_bios_pci_read16 (Bus, Dev, Func, PCI_VENDOR_ID_OFFSET);
  if (VendorId != PM_METAL_VIRTIO_VENDOR) {
    return EFI_UNSUPPORTED;
  }

  DeviceId = pm_bios_pci_read16 (Bus, Dev, Func, PCI_DEVICE_ID_OFFSET);
  if (DeviceId != WantId) {
    return EFI_UNSUPPORTED;
  }

  if (EFI_ERROR (FindCap (Bus, Dev, Func, VIRTIO_PCI_CAP_COMMON_CFG, &Common, NULL))) {
    return EFI_NOT_FOUND;
  }

  if (EFI_ERROR (FindCap (Bus, Dev, Func, VIRTIO_PCI_CAP_NOTIFY_CFG, &Notify, NULL))) {
    return EFI_NOT_FOUND;
  }

  return EFI_SUCCESS;
}

int
pm_metal_virtio_find (
  uint16_t  pci_device_id
  )
{
  UINT8  Bus;
  UINT8  Dev;
  UINT8  Func;

  for (Bus = 0; Bus < 8; Bus++) {
    for (Dev = 0; Dev < 32; Dev++) {
      UINT8  Fmax;
      UINT8  Hdr;

      if (pm_bios_pci_read16 (Bus, Dev, 0, PCI_VENDOR_ID_OFFSET) == 0xffff) {
        continue;
      }

      Hdr  = pm_bios_pci_read8 (Bus, Dev, 0, PCI_HEADER_TYPE_OFFSET);
      Fmax = (Hdr & HEADER_TYPE_MULTI_FUNCTION) ? 8 : 1;
      for (Func = 0; Func < Fmax; Func++) {
        if (ProbeBdf (Bus, Dev, Func, pci_device_id) == EFI_SUCCESS) {
          return 0;
        }
      }
    }
  }

  return -1;
}

int
pm_metal_virtio_open (
  uint16_t               pci_device_id,
  pm_metal_virtio_dev_t  *out
  )
{
  UINT8  Bus;
  UINT8  Dev;
  UINT8  Func;

  if (out == NULL) {
    return -1;
  }

  for (Bus = 0; Bus < 8; Bus++) {
    for (Dev = 0; Dev < 32; Dev++) {
      UINT8  Fmax;
      UINT8  Hdr;

      if (pm_bios_pci_read16 (Bus, Dev, 0, PCI_VENDOR_ID_OFFSET) == 0xffff) {
        continue;
      }

      Hdr  = pm_bios_pci_read8 (Bus, Dev, 0, PCI_HEADER_TYPE_OFFSET);
      Fmax = (Hdr & HEADER_TYPE_MULTI_FUNCTION) ? 8 : 1;
      for (Func = 0; Func < Fmax; Func++) {
        if (TryOpenBdf (Bus, Dev, Func, pci_device_id, out) == EFI_SUCCESS) {
          return 0;
        }
      }
    }
  }

  return -1;
}

void
pm_metal_virtio_close (
  pm_metal_virtio_dev_t  *dev
  )
{
  metal_vdev_priv_t  *p;
  UINT16              i;

  if (dev == NULL || dev->pci_io == NULL) {
    return;
  }

  p = Priv (dev);
  (VOID)CfgWr8 (p, OFFSET_OF (metal_virtio_common_cfg_t, DeviceStatus), 0);

  if (p->Vqs != NULL) {
    for (i = 0; i < p->NVqs; i++) {
      if (p->Vqs[i].ring_mem != NULL) {
        pm_metal_virtio_pages_free (p->Vqs[i].ring_mem, p->Vqs[i].ring_pages);
      }

      if (p->Vqs[i].next != NULL) {
        pm_metal_mem_free (p->Vqs[i].next);
      }
    }

    pm_metal_mem_free (p->Vqs);
  }

  pm_metal_mem_free (p);
  ZeroMem (dev, sizeof (*dev));
}

uint64_t
pm_metal_virtio_get_features (
  pm_metal_virtio_dev_t  *dev
  )
{
  metal_vdev_priv_t  *p;
  UINT32              Lo;
  UINT32              Hi;

  if (dev == NULL || dev->pci_io == NULL) {
    return 0;
  }

  p = Priv (dev);
  (VOID)CfgWr32 (p, OFFSET_OF (metal_virtio_common_cfg_t, DeviceFeatureSelect), 0);
  (VOID)CfgRd32 (p, OFFSET_OF (metal_virtio_common_cfg_t, DeviceFeature), &Lo);
  (VOID)CfgWr32 (p, OFFSET_OF (metal_virtio_common_cfg_t, DeviceFeatureSelect), 1);
  (VOID)CfgRd32 (p, OFFSET_OF (metal_virtio_common_cfg_t, DeviceFeature), &Hi);
  return ((UINT64)Hi << 32) | Lo;
}

int
pm_metal_virtio_set_features (
  pm_metal_virtio_dev_t  *dev,
  uint64_t                features
  )
{
  metal_vdev_priv_t  *p;
  UINT8               St;

  if (dev == NULL || dev->pci_io == NULL) {
    return -1;
  }

  p = Priv (dev);
  (VOID)CfgWr32 (p, OFFSET_OF (metal_virtio_common_cfg_t, DriverFeatureSelect), 0);
  (VOID)CfgWr32 (p, OFFSET_OF (metal_virtio_common_cfg_t, DriverFeature), (UINT32)features);
  (VOID)CfgWr32 (p, OFFSET_OF (metal_virtio_common_cfg_t, DriverFeatureSelect), 1);
  (VOID)CfgWr32 (
          p,
          OFFSET_OF (metal_virtio_common_cfg_t, DriverFeature),
          (UINT32)(features >> 32)
          );

  (VOID)CfgRd8 (p, OFFSET_OF (metal_virtio_common_cfg_t, DeviceStatus), &St);
  St |= PM_METAL_VIRTIO_S_FEATURES;
  (VOID)CfgWr8 (p, OFFSET_OF (metal_virtio_common_cfg_t, DeviceStatus), St);
  (VOID)CfgRd8 (p, OFFSET_OF (metal_virtio_common_cfg_t, DeviceStatus), &St);
  if ((St & PM_METAL_VIRTIO_S_FEATURES) == 0) {
    return -1;
  }

  p->Features   = features;
  dev->features = features;
  return 0;
}

void
pm_metal_virtio_set_status (
  pm_metal_virtio_dev_t  *dev,
  uint8_t                 status
  )
{
  if (dev == NULL || dev->pci_io == NULL) {
    return;
  }

  (VOID)CfgWr8 (
          Priv (dev),
          OFFSET_OF (metal_virtio_common_cfg_t, DeviceStatus),
          status
          );
}

uint8_t
pm_metal_virtio_get_status (
  pm_metal_virtio_dev_t  *dev
  )
{
  UINT8  St;

  if (dev == NULL || dev->pci_io == NULL) {
    return 0;
  }

  St = 0;
  (VOID)CfgRd8 (Priv (dev), OFFSET_OF (metal_virtio_common_cfg_t, DeviceStatus), &St);
  return St;
}

int
pm_metal_virtio_driver_ok (
  pm_metal_virtio_dev_t  *dev
  )
{
  UINT8  St;

  St  = pm_metal_virtio_get_status (dev);
  St |= PM_METAL_VIRTIO_S_DRIVER_OK;
  pm_metal_virtio_set_status (dev, St);
  return 0;
}

int
pm_metal_virtio_setup_queue (
  pm_metal_virtio_dev_t  *dev,
  uint16_t                qidx,
  uint16_t                want_size
  )
{
  metal_vdev_priv_t  *p;
  pm_metal_virtq_t   *vq;
  UINT16              Qsz;
  UINTN               DescBytes;
  UINTN               AvailBytes;
  UINTN               UsedBytes;
  UINTN               Total;
  UINTN               Pages;
  UINT8              *Mem;
  UINT16              i;

  if (dev == NULL || dev->pci_io == NULL) {
    return -1;
  }

  p = Priv (dev);
  (VOID)CfgWr16 (p, OFFSET_OF (metal_virtio_common_cfg_t, QueueSelect), qidx);
  Qsz = 0;
  (VOID)CfgRd16 (p, OFFSET_OF (metal_virtio_common_cfg_t, QueueSize), &Qsz);
  if (Qsz == 0) {
    return -1;
  }

  if (want_size > 0 && want_size < Qsz) {
    Qsz = want_size;
  }

  if (p->Vqs == NULL) {
    p->Vqs = (pm_metal_virtq_t *)pm_metal_mem_alloc (
                                   sizeof (pm_metal_virtq_t) * 8u,
                                   PM_METAL_MEM_HEAP,
                                   PM_METAL_MEM_ID_NONE
                                   );
    if (p->Vqs == NULL) {
      return -1;
    }

    ZeroMem (p->Vqs, sizeof (pm_metal_virtq_t) * 8u);
    p->NVqs    = 8;
    dev->vqs   = p->Vqs;
    dev->n_vqs = 8;
  }

  if (qidx >= p->NVqs) {
    return -1;
  }

  vq         = &p->Vqs[qidx];
  DescBytes  = sizeof (metal_vring_desc_t) * Qsz;
  AvailBytes = sizeof (UINT16) * (3u + Qsz);
  UsedBytes  = sizeof (UINT16) * 3u + sizeof (metal_vring_used_elem_t) * Qsz;
  Total      = DescBytes + AvailBytes + 4096u + UsedBytes;
  Pages      = EFI_SIZE_TO_PAGES (Total);
  Mem        = pm_metal_virtio_pages_alloc ((unsigned)Pages);
  if (Mem == NULL) {
    return -1;
  }

  ZeroMem (Mem, Pages * EFI_PAGE_SIZE);
  vq->qidx       = qidx;
  vq->size       = Qsz;
  vq->ring_mem   = Mem;
  vq->ring_pages = (UINT32)Pages;
  vq->desc       = Mem;
  vq->avail      = Mem + DescBytes;
  vq->used       = Mem + ((DescBytes + AvailBytes + 4095u) & ~4095u);
  vq->desc_phys  = (UINT64)(UINTN)vq->desc;
  vq->avail_phys = (UINT64)(UINTN)vq->avail;
  vq->used_phys  = (UINT64)(UINTN)vq->used;
  vq->free_head  = 0;
  vq->num_free   = Qsz;
  vq->last_used  = 0;
  vq->next       = (UINT16 *)pm_metal_mem_alloc (
                               sizeof (UINT16) * Qsz,
                               PM_METAL_MEM_HEAP,
                               PM_METAL_MEM_ID_NONE
                               );
  if (vq->next == NULL) {
    pm_metal_virtio_pages_free (Mem, (unsigned)Pages);
    return -1;
  }

  for (i = 0; i < Qsz - 1; i++) {
    vq->next[i] = (UINT16)(i + 1);
  }

  vq->next[Qsz - 1] = 0xffff;

  (VOID)CfgWr16 (p, OFFSET_OF (metal_virtio_common_cfg_t, QueueSelect), qidx);
  (VOID)CfgWr16 (p, OFFSET_OF (metal_virtio_common_cfg_t, QueueSize), Qsz);
  (VOID)CfgWr64 (p, OFFSET_OF (metal_virtio_common_cfg_t, QueueDesc), vq->desc_phys);
  (VOID)CfgWr64 (p, OFFSET_OF (metal_virtio_common_cfg_t, QueueAvail), vq->avail_phys);
  (VOID)CfgWr64 (p, OFFSET_OF (metal_virtio_common_cfg_t, QueueUsed), vq->used_phys);
  (VOID)CfgRd16 (p, OFFSET_OF (metal_virtio_common_cfg_t, QueueNotifyOff), &vq->notify_off);
  (VOID)CfgWr16 (p, OFFSET_OF (metal_virtio_common_cfg_t, QueueEnable), 1);
  return 0;
}

int
pm_metal_virtio_queue_reset (
  pm_metal_virtio_dev_t  *dev,
  uint16_t                qidx
  )
{
  metal_vdev_priv_t    *p;
  pm_metal_virtq_t       *vq;
  metal_vring_avail_t    *Avail;
  metal_vring_used_t     *Used;
  metal_vring_desc_t     *Desc;
  UINT16                  i;
  UINT8                   St;

  if (dev == NULL || dev->pci_io == NULL) {
    return -1;
  }

  p = Priv (dev);
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
  ZeroMem (Desc, sizeof (*Desc) * vq->size);
  Avail->Flags = 0;
  Avail->Idx   = 0;
  Used->Flags  = 0;
  Used->Idx    = 0;
  vq->last_used = 0;
  vq->free_head = 0;
  vq->num_free  = vq->size;
  for (i = 0; i < vq->size - 1; i++) {
    vq->next[i] = (UINT16)(i + 1u);
  }

  vq->next[vq->size - 1] = 0xffff;

  (VOID)CfgWr16 (p, OFFSET_OF (metal_virtio_common_cfg_t, QueueSelect), qidx);
  (VOID)CfgWr16 (p, OFFSET_OF (metal_virtio_common_cfg_t, QueueEnable), 0);
  (VOID)CfgWr64 (p, OFFSET_OF (metal_virtio_common_cfg_t, QueueDesc), vq->desc_phys);
  (VOID)CfgWr64 (p, OFFSET_OF (metal_virtio_common_cfg_t, QueueAvail), vq->avail_phys);
  (VOID)CfgWr64 (p, OFFSET_OF (metal_virtio_common_cfg_t, QueueUsed), vq->used_phys);
  (VOID)CfgWr16 (p, OFFSET_OF (metal_virtio_common_cfg_t, QueueSize), vq->size);
  (VOID)CfgRd16 (p, OFFSET_OF (metal_virtio_common_cfg_t, QueueNotifyOff), &vq->notify_off);
  (VOID)CfgWr16 (p, OFFSET_OF (metal_virtio_common_cfg_t, QueueEnable), 1);

  St  = pm_metal_virtio_get_status (dev);
  St |= PM_METAL_VIRTIO_S_DRIVER_OK;
  pm_metal_virtio_set_status (dev, St);
  return 0;
}

STATIC
VOID
MetalVirtioRemapBars (
  metal_vdev_priv_t  *p
  )
{
  UINT64  BarBase;
  UINT8   Consumed;

  pm_bios_pci_enable_mem_bm (p->Bus, p->Dev, p->Func);

  BarBase = pm_bios_pci_bar_mmio (p->Bus, p->Dev, p->Func, p->CommonBar, &Consumed);
  if (BarBase != 0) {
    p->CommonBase = (UINT8 *)(UINTN)(BarBase + p->CommonOff);
  }

  BarBase = pm_bios_pci_bar_mmio (p->Bus, p->Dev, p->Func, p->NotifyBar, &Consumed);
  if (BarBase != 0) {
    p->NotifyBase = (UINT8 *)(UINTN)(BarBase + p->NotifyOff);
  }

  if (p->DeviceLen != 0) {
    BarBase = pm_bios_pci_bar_mmio (p->Bus, p->Dev, p->Func, p->DeviceBar, &Consumed);
    if (BarBase != 0) {
      p->DeviceBase = (UINT8 *)(UINTN)(BarBase + p->DeviceOff);
    }
  }
}

int
pm_metal_virtio_post_ebs_resume (
  pm_metal_virtio_dev_t  *dev,
  uint16_t                qidx
  )
{
  metal_vdev_priv_t  *p;
  uint64_t            feats;

  if (dev == NULL || dev->pci_io == NULL) {
    return -1;
  }

  p = Priv (dev);
  MetalVirtioRemapBars (p);
  dev->common     = p->CommonBase;
  dev->notify     = p->NotifyBase;
  dev->device_cfg = p->DeviceBase;

  feats = (dev->features != 0) ? dev->features : p->Features;
  pm_metal_virtio_set_status (dev, 0);
  pm_metal_virtio_set_status (
    dev,
    (UINT8)(PM_METAL_VIRTIO_S_ACK | PM_METAL_VIRTIO_S_DRIVER)
    );
  if (pm_metal_virtio_set_features (dev, feats) != 0) {
    return -1;
  }

  return pm_metal_virtio_queue_reset (dev, qidx);
}

int
pm_metal_virtq_add (
  pm_metal_virtq_t  *vq,
  VOID              *buf,
  uint32_t           len,
  int                device_writeable,
  uint16_t          *head_out
  )
{
  metal_vring_desc_t   *Desc;
  metal_vring_avail_t  *Avail;
  UINT16                Head;
  UINT16                Aidx;

  if (vq == NULL || buf == NULL || len == 0 || vq->num_free == 0) {
    return -1;
  }

  Head          = vq->free_head;
  vq->free_head = vq->next[Head];
  vq->num_free--;

  Desc             = (metal_vring_desc_t *)vq->desc;
  Desc[Head].Addr  = (UINT64)(UINTN)buf;
  Desc[Head].Len   = len;
  Desc[Head].Flags = (UINT16)(device_writeable ? VRING_DESC_F_WRITE : 0);
  Desc[Head].Next  = 0;

  Avail = (metal_vring_avail_t *)vq->avail;
  Aidx  = Avail->Idx;
  MemoryFence ();
  Avail->Ring[Aidx % vq->size] = Head;
  MemoryFence ();
  Avail->Idx = (UINT16)(Aidx + 1u);

  if (head_out != NULL) {
    *head_out = Head;
  }

  return 0;
}

int
pm_metal_virtq_add2 (
  pm_metal_virtq_t  *vq,
  VOID              *buf0,
  uint32_t           len0,
  int                write0,
  VOID              *buf1,
  uint32_t           len1,
  int                write1,
  uint16_t          *head_out
  )
{
  metal_vring_desc_t   *Desc;
  metal_vring_avail_t  *Avail;
  UINT16                Head;
  UINT16                Next;
  UINT16                Aidx;

  if (vq == NULL || buf0 == NULL || buf1 == NULL || len0 == 0 || len1 == 0
      || vq->num_free < 2)
  {
    return -1;
  }

  Head          = vq->free_head;
  Next          = vq->next[Head];
  vq->free_head = vq->next[Next];
  vq->num_free  = (UINT16)(vq->num_free - 2u);

  Desc            = (metal_vring_desc_t *)vq->desc;
  Desc[Head].Addr = (UINT64)(UINTN)buf0;
  Desc[Head].Len  = len0;
  Desc[Head].Flags = (UINT16)(VRING_DESC_F_NEXT | (write0 ? VRING_DESC_F_WRITE : 0));
  Desc[Head].Next = Next;
  Desc[Next].Addr = (UINT64)(UINTN)buf1;
  Desc[Next].Len  = len1;
  Desc[Next].Flags = (UINT16)(write1 ? VRING_DESC_F_WRITE : 0);
  Desc[Next].Next = 0;

  Avail = (metal_vring_avail_t *)vq->avail;
  Aidx  = Avail->Idx;
  MemoryFence ();
  Avail->Ring[Aidx % vq->size] = Head;
  MemoryFence ();
  Avail->Idx = (UINT16)(Aidx + 1u);

  if (head_out != NULL) {
    *head_out = Head;
  }

  return 0;
}

int
pm_metal_virtq_add3 (
  pm_metal_virtq_t  *vq,
  VOID              *buf0,
  uint32_t           len0,
  int                write0,
  VOID              *buf1,
  uint32_t           len1,
  int                write1,
  VOID              *buf2,
  uint32_t           len2,
  int                write2,
  uint16_t          *head_out
  )
{
  metal_vring_desc_t   *Desc;
  metal_vring_avail_t  *Avail;
  UINT16                A;
  UINT16                B;
  UINT16                C;
  UINT16                Aidx;

  if (vq == NULL || buf0 == NULL || buf1 == NULL || buf2 == NULL
      || len0 == 0 || len1 == 0 || len2 == 0 || vq->num_free < 3)
  {
    return -1;
  }

  A             = vq->free_head;
  B             = vq->next[A];
  C             = vq->next[B];
  vq->free_head = vq->next[C];
  vq->num_free  = (UINT16)(vq->num_free - 3u);

  Desc           = (metal_vring_desc_t *)vq->desc;
  Desc[A].Addr   = (UINT64)(UINTN)buf0;
  Desc[A].Len    = len0;
  Desc[A].Flags  = (UINT16)(VRING_DESC_F_NEXT | (write0 ? VRING_DESC_F_WRITE : 0));
  Desc[A].Next   = B;
  Desc[B].Addr   = (UINT64)(UINTN)buf1;
  Desc[B].Len    = len1;
  Desc[B].Flags  = (UINT16)(VRING_DESC_F_NEXT | (write1 ? VRING_DESC_F_WRITE : 0));
  Desc[B].Next   = C;
  Desc[C].Addr   = (UINT64)(UINTN)buf2;
  Desc[C].Len    = len2;
  Desc[C].Flags  = (UINT16)(write2 ? VRING_DESC_F_WRITE : 0);
  Desc[C].Next   = 0;

  Avail = (metal_vring_avail_t *)vq->avail;
  Aidx  = Avail->Idx;
  MemoryFence ();
  Avail->Ring[Aidx % vq->size] = A;
  MemoryFence ();
  Avail->Idx = (UINT16)(Aidx + 1u);

  if (head_out != NULL) {
    *head_out = A;
  }

  return 0;
}

void
pm_metal_virtq_kick (
  pm_metal_virtio_dev_t  *dev,
  pm_metal_virtq_t       *vq
  )
{
  metal_vdev_priv_t  *p;
  UINT16              Sel;
  UINT32              Off;

  if (dev == NULL || vq == NULL || dev->pci_io == NULL) {
    return;
  }

  p   = Priv (dev);
  Off = (UINT32)vq->notify_off * p->NotifyMult;
  Sel = vq->qidx;
  if (p->UseMmio && p->NotifyBase != NULL) {
    *(volatile UINT16 *)(p->NotifyBase + Off) = Sel;
    MemoryFence ();
    return;
  }

  return;
}

int
pm_metal_virtq_get_used (
  pm_metal_virtq_t  *vq,
  uint16_t          *head,
  uint32_t          *len
  )
{
  metal_vring_used_t  *Used;
  UINT16               Uidx;

  if (vq == NULL) {
    return 0;
  }

  Used = (metal_vring_used_t *)vq->used;
  MemoryFence ();
  Uidx = Used->Idx;
  if (Uidx == vq->last_used) {
    return 0;
  }

  if (head != NULL) {
    *head = (UINT16)Used->Ring[vq->last_used % vq->size].Id;
  }

  if (len != NULL) {
    *len = Used->Ring[vq->last_used % vq->size].Len;
  }

  vq->last_used = (UINT16)(vq->last_used + 1u);
  return 1;
}

void
pm_metal_virtq_free_chain (
  pm_metal_virtq_t  *vq,
  uint16_t           head
  )
{
  metal_vring_desc_t  *Desc;
  UINT16               Cur;
  UINT16               Next;

  if (vq == NULL) {
    return;
  }

  Desc = (metal_vring_desc_t *)vq->desc;
  Cur  = head;
  for (;;) {
    Next = Desc[Cur].Next;
    vq->next[Cur] = vq->free_head;
    vq->free_head = Cur;
    vq->num_free++;
    if ((Desc[Cur].Flags & VRING_DESC_F_NEXT) == 0) {
      break;
    }

    Cur = Next;
  }
}

int
pm_metal_virtio_cfg_read (
  pm_metal_virtio_dev_t  *dev,
  uint32_t                offset,
  VOID                   *buf,
  uint32_t                len
  )
{
  metal_vdev_priv_t  *p;
  UINT32              i;

  if (dev == NULL || buf == NULL || len == 0 || dev->pci_io == NULL) {
    return -1;
  }

  p = Priv (dev);
  if (p->DeviceLen == 0 || offset + len > p->DeviceLen) {
    return -1;
  }

  if (p->UseMmio && p->DeviceBase != NULL) {
    for (i = 0; i < len; i++) {
      ((UINT8 *)buf)[i] = *(volatile UINT8 *)(p->DeviceBase + offset + i);
    }

    return 0;
  }

  return -1;
}

int
pm_metal_virtio_cfg_write (
  pm_metal_virtio_dev_t  *dev,
  uint32_t                offset,
  CONST VOID             *buf,
  uint32_t                len
  )
{
  metal_vdev_priv_t  *p;
  UINT32              i;

  if (dev == NULL || buf == NULL || len == 0 || dev->pci_io == NULL) {
    return -1;
  }

  p = Priv (dev);
  if (p->DeviceLen == 0 || offset + len > p->DeviceLen) {
    return -1;
  }

  if (p->UseMmio && p->DeviceBase != NULL) {
    for (i = 0; i < len; i++) {
      *(volatile UINT8 *)(p->DeviceBase + offset + i) = ((CONST UINT8 *)buf)[i];
    }

    MemoryFence ();
    return 0;
  }

  return -1;
}

void
pm_metal_virtio_ack_isr (
  pm_metal_virtio_dev_t  *dev
  )
{
  (VOID)dev;
}
