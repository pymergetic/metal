/** @file
  Shared device harvest: platform DT floor + bus probes.
  Port hooks supply only the floor deltas (fs/random compat, input prep).
**/
#include <pymergetic/metal/boot/boot.h>
#include <pymergetic/metal/bus/io/io.h>
#include <pymergetic/metal/dev/net/net_ops.h>
#include <pymergetic/metal/dev/audio/audio_ops.h>
#include <pymergetic/metal/dev/console/console.h>
#include <pymergetic/metal/dev/blk/blk.h>

#include <Uefi.h>

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
  STATIC CONST pm_metal_io_node_t  GfxNode = {
    .class = PM_METAL_IO_GFX,
    .compat = "framebuffer",
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

  (VOID)pm_metal_io_dt_add (&TimeNode);
  (VOID)pm_metal_io_dt_add (&GfxNode);
  (VOID)pm_metal_io_dt_add (&FsNode);
  (VOID)pm_metal_io_dt_add (&InputNode);
  (VOID)pm_metal_io_dt_add (&StreamNode);
  (VOID)pm_metal_io_dt_add (&RandomNode);

  pm_metal_boot_harvest_bus_devices ();
  return 0;
}
