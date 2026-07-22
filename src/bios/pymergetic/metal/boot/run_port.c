/** @file
  BIOS port ops — takeover + Multiboot LFB stash + reset.
**/
#include <pymergetic/metal/boot/port.h>
#include <pymergetic/metal/boot/boot.h>
#include <pymergetic/metal/dev/acpi/acpi.h>
#include <pymergetic/metal/log/log.h>
#include <runtime/run/run.h>

#include <PmBiosUefi.h>
#include <Library/BaseLib.h>
#include <Library/CpuLib.h>
#include <Library/IoLib.h>
#include <Library/SynchronizationLib.h>

STATIC INT32           mOwned;
STATIC volatile UINT32 mApRelease;
STATIC volatile UINT32 mApsWaiting;
STATIC UINTN           mApCount;

STATIC VOID     *mFb;
STATIC unsigned  mFbW;
STATIC unsigned  mFbH;
STATIC unsigned  mFbPpsl;

STATIC int
PortOwned (
  VOID
  )
{
  return mOwned ? 1 : 0;
}

STATIC void
PortSetFramebuffer (
  VOID      *fb,
  unsigned   width,
  unsigned   height,
  unsigned   ppsl
  )
{
  mFb     = fb;
  mFbW    = width;
  mFbH    = height;
  mFbPpsl = ppsl ? ppsl : width;
}

STATIC int
PortGetFramebuffer (
  VOID     **fb,
  unsigned  *width,
  unsigned  *height,
  unsigned  *ppsl
  )
{
  if (mFb == NULL) {
    return -1;
  }

  if (fb != NULL) {
    *fb = mFb;
  }

  if (width != NULL) {
    *width = mFbW;
  }

  if (height != NULL) {
    *height = mFbH;
  }

  if (ppsl != NULL) {
    *ppsl = mFbPpsl;
  }

  return 0;
}

STATIC void
PortReset (
  int  reboot
  )
{
  if (reboot) {
    /* Pulse CPU reset via keyboard controller; else halt. */
    IoWrite8 (0x64, 0xFE);
    CpuDeadLoop ();
  }

  /*
   * Best-effort power off, then halt. QEMU hits isa-debug-exit first;
   * real PCs need FADT PM1 + \_S5_ (hardcoded ports are QEMU-only).
   */
  IoWrite8 (0x501, 0x00);           /* QEMU isa-debug-exit */
  pm_metal_acpi_poweroff ();
  IoWrite16 (0x604, 0x2000);        /* QEMU/PIIX ACPI fallback */
  IoWrite16 (0xB004, 0x2000);       /* Bochs/older ACPI */
  CpuDeadLoop ();
}

STATIC int
PortTakeoverAndRun (
  VOID      *image_handle,
  unsigned   n_cpus
  )
{
  (VOID)image_handle;
  (VOID)n_cpus;

  mOwned      = 1;
  mApRelease  = 0;
  mApsWaiting = 0;
  mApCount    = 0; /* SMP MADT bring-up can fill this later */

  pm_metal_log_ebs_close_uefi ();
  pm_metal_log_attach_uart ();

  pm_metal_run_clear_inboxes ();
  if (pm_metal_boot_seed_init () != 0) {
    pm_metal_log ("metal-boot: seed init failed");
  }

  MemoryFence ();
  mApRelease = 1;
  MemoryFence ();
  pm_metal_run_enter (0);

  PortReset (0);
  return 0;
}

CONST pm_metal_port_ops_t  pm_metal_port_ops = {
  PortOwned,
  PortTakeoverAndRun,
  PortReset,
  PortSetFramebuffer,
  PortGetFramebuffer
};
