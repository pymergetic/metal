/** @file
  ExitBootServices + AP wait/release + runner entry. (impl: efi)
**/
#include <pymergetic/metal/efi/efi_run.h>
#include <pymergetic/metal/efi/boot.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/console/console.h>
#include <pymergetic/metal/shell/shell.h>
#include <pymergetic/metal/wasm/wasm.h>
#include <pymergetic/metal/net/net_ops.h>
#include <pymergetic/metal/audio/audio_ops.h>
#include <pymergetic/metal/blk/blk.h>
#include <mem/mem.h>
#include <run/run.h>
#include <time/time.h>

#include <Uefi.h>
#include <Pi/PiMultiPhase.h>
#include <Protocol/MpService.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/CpuLib.h>
#include <Library/SynchronizationLib.h>

STATIC INT32                     mOwned;
STATIC EFI_MP_SERVICES_PROTOCOL  *mMp;
STATIC volatile UINT32           mApRelease;
STATIC volatile UINT32           mApsWaiting;
STATIC UINTN                     mApCount;

int
pm_metal_efi_owned (
  VOID
  )
{
  return mOwned ? 1 : 0;
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

  if (gRT != NULL) {
    gRT->ResetSystem (EfiResetShutdown, EFI_SUCCESS, 0, NULL);
  }

  for (;;) {
    CpuPause ();
  }
}

EFI_STATUS
pm_metal_efi_exit_boot_and_run (
  EFI_HANDLE        ImageHandle,
  EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  (VOID)SystemTable;

  Status = MetalStartApsWaiting ();
  if (EFI_ERROR (Status)) {
    pm_metal_logf ("metal-ebs: AP start failed: %r", Status);
    return Status;
  }

  DisableInterrupts ();
  Status = MetalExitBootServices (ImageHandle);
  if (EFI_ERROR (Status)) {
    EnableInterrupts ();
    pm_metal_logf ("metal-ebs: ExitBootServices failed: %r", Status);
    return Status;
  }

  pm_metal_log_ebs_close_uefi ();
  /* UART resume before seed — terminal lives for the init tree. */
  pm_metal_log_attach_uart ();
  MetalReleaseApsAndRun ();
  return EFI_SUCCESS;
}
