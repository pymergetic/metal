/** @file
  EFI port ops — ExitBootServices + AP wait/release + runner entry.
**/
#include <pymergetic/metal/boot/port.h>
#include <pymergetic/metal/boot/boot.h>
#include <pymergetic/metal/dev/acpi/acpi.h>
#include <pymergetic/metal/log/log.h>
#include <runtime/run/run.h>

#include <Uefi.h>
#include <Pi/PiMultiPhase.h>
#include <Protocol/MpService.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseLib.h>
#include <Library/CpuLib.h>
#include <Library/SynchronizationLib.h>

STATIC INT32                     mOwned;
STATIC EFI_MP_SERVICES_PROTOCOL  *mMp;
STATIC volatile UINT32           mApRelease;
STATIC volatile UINT32           mApsWaiting;
STATIC UINTN                     mApCount;

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
  (VOID)fb;
  (VOID)width;
  (VOID)height;
  (VOID)ppsl;
}

STATIC int
PortGetFramebuffer (
  VOID     **fb,
  unsigned  *width,
  unsigned  *height,
  unsigned  *ppsl
  )
{
  (VOID)fb;
  (VOID)width;
  (VOID)height;
  (VOID)ppsl;
  return -1;
}

STATIC void
PortReset (
  int  reboot
  )
{
  if (gRT != NULL) {
    gRT->ResetSystem (
           reboot ? EfiResetCold : EfiResetShutdown,
           EFI_SUCCESS,
           0,
           NULL
           );
  }

  /* Some firmwares ignore ResetSystem post-EBS; drive ACPI S5 directly. */
  if (!reboot) {
    pm_metal_acpi_poweroff ();
  }

  for (;;) {
    CpuPause ();
  }
}

STATIC
VOID
EFIAPI
MetalApWaitThenRun (
  IN OUT VOID  *Buffer
  )
{
  UINTN  Cpu;

  (VOID)Buffer;
  if (mMp != NULL) {
    (VOID)mMp->WhoAmI (mMp, &Cpu);
  } else {
    Cpu = 1;
  }

  InterlockedIncrement ((UINT32 *)&mApsWaiting);
  while (mApRelease == 0) {
    CpuPause ();
  }

  pm_metal_run_enter ((unsigned)Cpu);
}

STATIC
VOID
EFIAPI
MetalApEventNop (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  (VOID)Event;
  (VOID)Context;
}

STATIC
EFI_STATUS
MetalStartApsWaiting (
  VOID
  )
{
  EFI_STATUS  Status;
  EFI_EVENT   Done;
  UINTN       Total;
  UINTN       Enabled;

  mMp         = NULL;
  mApRelease  = 0;
  mApsWaiting = 0;
  mApCount    = 0;

  Status = gBS->LocateProtocol (&gEfiMpServiceProtocolGuid, NULL, (VOID **)&mMp);
  if (EFI_ERROR (Status) || mMp == NULL) {
    return EFI_SUCCESS;
  }

  Total = Enabled = 0;
  Status = mMp->GetNumberOfProcessors (mMp, &Total, &Enabled);
  if (EFI_ERROR (Status) || Enabled <= 1) {
    return EFI_SUCCESS;
  }

  mApCount = Enabled - 1;
  /*
   * Non-NULL WaitEvent ⇒ StartupAllAPs returns immediately; APs stay
   * inside MetalApWaitThenRun across EBS until mApRelease.
   */
  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  MetalApEventNop,
                  NULL,
                  &Done
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = mMp->StartupAllAPs (
                  mMp,
                  MetalApWaitThenRun,
                  FALSE,
                  Done,
                  0,
                  NULL,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (Done);
    return Status;
  }

  while (mApsWaiting < (UINT32)mApCount) {
    CpuPause ();
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
MetalExitBootServices (
  EFI_HANDLE  ImageHandle
  )
{
  EFI_STATUS             Status;
  EFI_MEMORY_DESCRIPTOR  *Map;
  UINTN                  MapSize;
  UINTN                  MapKey;
  UINTN                  DescSize;
  UINT32                 DescVer;
  UINTN                  Extra;
  UINTN                  Tries;

  Map   = NULL;
  Extra = 8;
  for (Tries = 0; Tries < 8; Tries++) {
    MapSize = 0;
    Status  = gBS->GetMemoryMap (&MapSize, NULL, &MapKey, &DescSize, &DescVer);
    if (Status != EFI_BUFFER_TOO_SMALL) {
      return Status;
    }

    MapSize += DescSize * Extra;
    if (Map != NULL) {
      FreePool (Map);
      Map = NULL;
    }

    Map = AllocatePool (MapSize);
    if (Map == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    Status = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVer);
    if (EFI_ERROR (Status)) {
      FreePool (Map);
      return Status;
    }

    Status = gBS->ExitBootServices (ImageHandle, MapKey);
    if (!EFI_ERROR (Status)) {
      mOwned = 1;
      gBS    = NULL;
      return EFI_SUCCESS;
    }

    Extra += 8;
  }

  if (Map != NULL) {
    FreePool (Map);
  }

  return EFI_INVALID_PARAMETER;
}

STATIC
VOID
MetalReleaseApsAndRun (
  VOID
  )
{
  pm_metal_run_clear_inboxes ();
  if (pm_metal_boot_seed_init () != 0) {
    pm_metal_log ("metal-boot: seed init failed");
  }

  MemoryFence ();
  mApRelease = 1;
  MemoryFence ();
  pm_metal_run_enter (0);

  PortReset (0);
}

STATIC int
PortTakeoverAndRun (
  VOID      *image_handle,
  unsigned   n_cpus
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  ImageHandle;

  (VOID)n_cpus;
  ImageHandle = (EFI_HANDLE)image_handle;

  Status = MetalStartApsWaiting ();
  if (EFI_ERROR (Status)) {
    pm_metal_logf ("metal-ebs: AP start failed: %r", Status);
    return -1;
  }

  DisableInterrupts ();
  Status = MetalExitBootServices (ImageHandle);
  if (EFI_ERROR (Status)) {
    EnableInterrupts ();
    pm_metal_logf ("metal-ebs: ExitBootServices failed: %r", Status);
    return -1;
  }

  pm_metal_log_ebs_close_uefi ();
  EnableInterrupts ();
  /* UART resume before seed — terminal lives for the init tree. */
  pm_metal_log_attach_uart ();
  MetalReleaseApsAndRun ();
  return 0;
}

CONST pm_metal_port_ops_t  pm_metal_port_ops = {
  PortOwned,
  PortTakeoverAndRun,
  PortReset,
  PortSetFramebuffer,
  PortGetFramebuffer
};
