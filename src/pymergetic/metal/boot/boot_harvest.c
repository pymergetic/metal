/** @file
  Shared device harvest: platform DT floor + bus probes.
  Port hooks supply only the floor deltas (fs/random compat, input prep).
**/
#include <pymergetic/metal/boot/boot.h>
#include <pymergetic/metal/bus/io/io.h>
#include <pymergetic/metal/dev/net/net_ops.h>
#include <pymergetic/metal/dev/audio/audio_ops.h>
#include <pymergetic/metal/dev/console/console.h>
#include <pymergetic/metal/dev/input/virtio_input.h>
#include <pymergetic/metal/dev/blk/blk.h>
#include "../bus/pci/pci.h"

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>

STATIC
INT32
MetalBootHarvestGfxPci (
  pm_metal_io_node_t  *node
  )
{
  UINT8   bus;
  UINT8   dev;
  UINT8   func;
  UINT16  ven;
  UINT16  did;

  if (node == NULL) {
    return -1;
  }

  /* VGA (03:00) then “other display” (03:80) — covers IGP + some dGPU. */
  if (pm_bios_pci_find_class (0x03, 0x00, &bus, &dev, &func) != 0
      && pm_bios_pci_find_class (0x03, 0x80, &bus, &dev, &func) != 0)
  {
    return -1;
  }

  ven = pm_bios_pci_read16 (bus, dev, func, 0x00);
  did = pm_bios_pci_read16 (bus, dev, func, 0x02);
  if (ven == 0xffffu || did == 0xffffu) {
    return -1;
  }

  node->bus    = PM_METAL_IO_BUS_PCI;
  node->loc[0] = bus;
  node->loc[1] = dev;
  node->loc[2] = func;
  node->loc[3] = ((UINT32)ven << 16) | (UINT32)did;
  return 0;
}

void
pm_metal_boot_harvest_bus_devices (
  VOID
  )
{
  {
    STATIC CONST pm_metal_io_node_t  LoNode = {
      .class = PM_METAL_IO_NET,
      .compat = "loopback",
      .bus = PM_METAL_IO_BUS_PLATFORM
    };

    (VOID)pm_metal_io_dt_add (&LoNode);
    (VOID)pm_metal_net_virtio_detect ();
    (VOID)pm_metal_net_bge_detect ();
  }

  if (pm_metal_audio_virtio_probe () != 0
      && pm_metal_audio_ac97_probe () != 0)
  {
    pm_metal_audio_null_install ();
    {
      STATIC CONST pm_metal_io_node_t  AudioNode = {
        .class = PM_METAL_IO_AUDIO,
        .compat = "null",
        .bus = PM_METAL_IO_BUS_PLATFORM
      };

      (VOID)pm_metal_io_dt_add (&AudioNode);
    }
  }

  (VOID)pm_metal_console_virtio_probe ();
  (VOID)pm_metal_input_virtio_tablet_probe ();
  (VOID)pm_metal_blk_virtio_detect ();
  (VOID)pm_metal_blk_ide_detect ();
}

int
pm_metal_boot_harvest_devices (
  VOID
  )
{
  CONST CHAR8         *fs_compat;
  CONST CHAR8         *random_compat;
  pm_metal_io_node_t   FsNode;
  pm_metal_io_node_t   RandomNode;
  STATIC CONST pm_metal_io_node_t  TimeNode = {
    .class = PM_METAL_IO_TIME,
    .compat = "tsc",
    .bus = PM_METAL_IO_BUS_PLATFORM
  };
  STATIC CONST pm_metal_io_node_t  InputNode = {
    .class = PM_METAL_IO_INPUT,
    .compat = "ps2+com1",
    .bus = PM_METAL_IO_BUS_ISA
  };
  STATIC CONST pm_metal_io_node_t  StreamNode = {
    .class = PM_METAL_IO_STREAM,
    .compat = "uart+ui",
    .bus = PM_METAL_IO_BUS_PLATFORM
  };
  pm_metal_io_node_t  GfxNode;

  pm_metal_boot_port_floor (&fs_compat, &random_compat);

  FsNode = (pm_metal_io_node_t){
    .class = PM_METAL_IO_FS,
    .compat = fs_compat,
    .bus = PM_METAL_IO_BUS_PLATFORM
  };
  RandomNode = (pm_metal_io_node_t){
    .class = PM_METAL_IO_RANDOM,
    .compat = random_compat,
    .bus = PM_METAL_IO_BUS_PLATFORM
  };

  ZeroMem (&GfxNode, sizeof (GfxNode));
  GfxNode.class  = PM_METAL_IO_GFX;
  GfxNode.compat = "framebuffer";
  GfxNode.bus    = PM_METAL_IO_BUS_PLATFORM;
  (VOID)MetalBootHarvestGfxPci (&GfxNode);

  (VOID)pm_metal_io_dt_add (&TimeNode);
  (VOID)pm_metal_io_dt_add (&GfxNode);
  (VOID)pm_metal_io_dt_add (&FsNode);
  (VOID)pm_metal_io_dt_add (&InputNode);
  (VOID)pm_metal_io_dt_add (&StreamNode);
  (VOID)pm_metal_io_dt_add (&RandomNode);

  pm_metal_boot_harvest_bus_devices ();
  return 0;
}
