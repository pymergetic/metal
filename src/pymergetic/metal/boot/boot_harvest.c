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

#include <stdint.h>
#include <string.h>

static int32_t MetalBootHarvestGfxPci(pm_metal_io_node_t *node)
{
  uint8_t  bus;
  uint8_t  dev;
  uint8_t  func;
  uint16_t ven;
  uint16_t did;

  if (node == NULL) {
    return -1;
  }

  /* VGA (03:00) then “other display” (03:80) — covers IGP + some dGPU. */
  if (pm_bios_pci_find_class(0x03, 0x00, &bus, &dev, &func) != 0 &&
      pm_bios_pci_find_class(0x03, 0x80, &bus, &dev, &func) != 0) {
    return -1;
  }

  ven = pm_bios_pci_read16(bus, dev, func, 0x00);
  did = pm_bios_pci_read16(bus, dev, func, 0x02);
  if (ven == 0xffffu || did == 0xffffu) {
    return -1;
  }

  node->bus    = PM_METAL_IO_BUS_PCI;
  node->loc[0] = bus;
  node->loc[1] = dev;
  node->loc[2] = func;
  node->loc[3] = ((uint32_t)ven << 16) | (uint32_t)did;
  return 0;
}

void pm_metal_boot_harvest_bus_devices(void)
{
  {
    static const pm_metal_io_node_t LoNode = { .class  = PM_METAL_IO_NET,
                                               .compat = "loopback",
                                               .bus    = PM_METAL_IO_BUS_PLATFORM };

    (void)pm_metal_io_dt_add(&LoNode);
    (void)pm_metal_net_virtio_detect();
    (void)pm_metal_net_bge_detect();
  }

  if (pm_metal_audio_virtio_probe() != 0 && pm_metal_audio_ac97_probe() != 0) {
    pm_metal_audio_null_install();
    {
      static const pm_metal_io_node_t AudioNode = { .class  = PM_METAL_IO_AUDIO,
                                                    .compat = "null",
                                                    .bus    = PM_METAL_IO_BUS_PLATFORM };

      (void)pm_metal_io_dt_add(&AudioNode);
    }
  }

  (void)pm_metal_console_virtio_probe();
  (void)pm_metal_input_virtio_tablet_probe();
  (void)pm_metal_blk_virtio_detect();
  (void)pm_metal_blk_ide_detect();
}

int pm_metal_boot_harvest_devices(void)
{
  const char                     *fs_compat;
  const char                     *random_compat;
  pm_metal_io_node_t              FsNode;
  pm_metal_io_node_t              RandomNode;
  static const pm_metal_io_node_t TimeNode   = { .class  = PM_METAL_IO_TIME,
                                                 .compat = "tsc",
                                                 .bus    = PM_METAL_IO_BUS_PLATFORM };
  static const pm_metal_io_node_t InputNode  = { .class  = PM_METAL_IO_INPUT,
                                                 .compat = "ps2+com1",
                                                 .bus    = PM_METAL_IO_BUS_ISA };
  static const pm_metal_io_node_t StreamNode = { .class  = PM_METAL_IO_STREAM,
                                                 .compat = "uart+ui",
                                                 .bus    = PM_METAL_IO_BUS_PLATFORM };
  pm_metal_io_node_t              GfxNode;

  pm_metal_boot_port_floor(&fs_compat, &random_compat);

  FsNode     = (pm_metal_io_node_t){ .class  = PM_METAL_IO_FS,
                                     .compat = fs_compat,
                                     .bus    = PM_METAL_IO_BUS_PLATFORM };
  RandomNode = (pm_metal_io_node_t){ .class  = PM_METAL_IO_RANDOM,
                                     .compat = random_compat,
                                     .bus    = PM_METAL_IO_BUS_PLATFORM };

  memset(&GfxNode, 0, sizeof(GfxNode));
  GfxNode.class  = PM_METAL_IO_GFX;
  GfxNode.compat = "framebuffer";
  GfxNode.bus    = PM_METAL_IO_BUS_PLATFORM;
  (void)MetalBootHarvestGfxPci(&GfxNode);

  (void)pm_metal_io_dt_add(&TimeNode);
  (void)pm_metal_io_dt_add(&GfxNode);
  (void)pm_metal_io_dt_add(&FsNode);
  (void)pm_metal_io_dt_add(&InputNode);
  (void)pm_metal_io_dt_add(&StreamNode);
  (void)pm_metal_io_dt_add(&RandomNode);

  pm_metal_boot_harvest_bus_devices();
  return 0;
}
