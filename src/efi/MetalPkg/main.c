/** @file
  metal.efi — sync floor → EBS → seeded init pool (no auto-tests).
**/
#include <Uefi.h>
#include <Pi/PiMultiPhase.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/MpService.h>

#include <runtime/mem/mem.h>
#include <runtime/stack/stack.h>
#include <runtime/run/run.h>
#include <pymergetic/metal/dev/gfx/gfx.h>
#include <pymergetic/metal/fs/esp/esp.h>
#include <pymergetic/metal/boot/port.h>
#include <pymergetic/metal/boot/boot.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/util/fourcc.h>

#define PM_METAL_BS_RESERVE_PAGES  ((UINTN)(16 * 1024 * 1024 / EFI_PAGE_SIZE))
#define PM_METAL_SMOKE_ID          PM_METAL_UTIL_FOURCC ('s', 'm', 'o', 'k')

STATIC UINT64  mClaimMiB;
STATIC UINT64  mMapBytes;
STATIC UINT64  mHoleMiB;
STATIC UINT64  mHeapBytes;
STATIC UINT64  mStackKiB;
STATIC UINTN   mCpuCount;

STATIC
UINTN
MetalEfiCpuCount (
  VOID
  )
{
  EFI_STATUS                 Status;
  EFI_MP_SERVICES_PROTOCOL  *Mp;
  UINTN                      Total;
  UINTN                      Enabled;

  Status = gBS->LocateProtocol (&gEfiMpServiceProtocolGuid, NULL, (VOID **)&Mp);
  if (EFI_ERROR (Status) || Mp == NULL) {
    return 1;
  }

  Total = Enabled = 0;
  Status = Mp->GetNumberOfProcessors (Mp, &Total, &Enabled);
  if (EFI_ERROR (Status) || Enabled == 0) {
    return 1;
  }

  return Enabled;
}

STATIC
EFI_STATUS
MetalLargestConventionalPages (
  OUT UINTN  *LargestPages
  )
{
  EFI_STATUS             Status;
  EFI_MEMORY_DESCRIPTOR  *Map;
  EFI_MEMORY_DESCRIPTOR  *Entry;
  UINTN                  MapSize;
  UINTN                  MapKey;
  UINTN                  DescSize;
  UINT32                 DescVer;
  UINTN                  Index;
  UINTN                  Count;
  UINTN                  Best;

  if (LargestPages == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  MapSize = 0;
  Status  = gBS->GetMemoryMap (&MapSize, NULL, &MapKey, &DescSize, &DescVer);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return Status;
  }

  MapSize += DescSize * 8;
  Map      = AllocatePool (MapSize);
  if (Map == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVer);
  if (EFI_ERROR (Status)) {
    FreePool (Map);
    return Status;
  }

  Best  = 0;
  Count = MapSize / DescSize;
  for (Index = 0; Index < Count; Index++) {
    Entry = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Index * DescSize);
    if (Entry->Type == EfiConventionalMemory && Entry->NumberOfPages > Best) {
      Best = Entry->NumberOfPages;
    }
  }

  FreePool (Map);
  *LargestPages = Best;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
MetalMemBringUp (
  VOID
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  ArenaPa;
  UINTN                 LargestPages;
  UINTN                 ClaimPages;
  VOID                 *p;
  VOID                 *q;
  VOID                 *s;
  UINT32               *w;

  mCpuCount = MetalEfiCpuCount ();

  Status = MetalLargestConventionalPages (&LargestPages);
  if (EFI_ERROR (Status)) {
    pm_metal_logf ("metal-mem: conventional scan failed: %r", Status);
    return Status;
  }

  if (LargestPages <= PM_METAL_BS_RESERVE_PAGES + 4) {
    pm_metal_log ("metal-mem: not enough contiguous conventional RAM");
    return EFI_OUT_OF_RESOURCES;
  }

  ClaimPages = LargestPages - PM_METAL_BS_RESERVE_PAGES;
  ClaimPages = (ClaimPages / 4) * 4;
  if (ClaimPages < 4) {
    return EFI_OUT_OF_RESOURCES;
  }

  ArenaPa = 0;
  Status  = gBS->AllocatePages (
                   AllocateAnyPages,
                   EfiLoaderData,
                   ClaimPages,
                   &ArenaPa
                   );
  if (EFI_ERROR (Status)) {
    pm_metal_logf ("AllocatePages arena failed: %r", Status);
    return Status;
  }

  if (pm_metal_mem_init (
        (VOID *)(UINTN)ArenaPa,
        (size_t)ClaimPages * EFI_PAGE_SIZE,
        (unsigned)mCpuCount
        ) != 0)
  {
    pm_metal_log ("pm_metal_mem_init failed");
    return EFI_OUT_OF_RESOURCES;
  }

  if (pm_metal_stack_init ((unsigned)mCpuCount) != 0) {
    pm_metal_log ("metal-mem: stack init failed");
    return EFI_OUT_OF_RESOURCES;
  }

  mClaimMiB  = (UINT64)(pm_metal_mem_arena_bytes () / (1024 * 1024));
  mMapBytes  = (UINT64)pm_metal_mem_map_bytes ();
  mHeapBytes = (UINT64)pm_metal_mem_heap_bytes ();
  mHoleMiB   = (UINT64)(pm_metal_mem_hole_bytes () / (1024 * 1024));
  mCpuCount  = pm_metal_mem_n_cpus ();
  mStackKiB  = (UINT64)(pm_metal_stack_bytes () / 1024);

  /* Quiet heap sanity — failures only. */
  p = pm_metal_mem_alloc (128, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  q = pm_metal_mem_alloc (4096, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (p == NULL || q == NULL) {
    pm_metal_log ("metal-mem: HEAP alloc failed");
    return EFI_OUT_OF_RESOURCES;
  }

  SetMem (p, 128, 0xA5);
  SetMem (q, 4096, 0x5A);
  w = (UINT32 *)p;
  if (w[0] != 0xA5A5A5A5) {
    pm_metal_log ("metal-mem: HEAP pattern mismatch");
    return EFI_DEVICE_ERROR;
  }

  pm_metal_mem_free (q);
  pm_metal_mem_free (p);

  s = pm_metal_mem_alloc (64, PM_METAL_MEM_HEAP, PM_METAL_SMOKE_ID);
  if (s == NULL || pm_metal_mem_lookup (PM_METAL_SMOKE_ID) != s) {
    pm_metal_log ("metal-mem: publish/lookup failed");
    return EFI_DEVICE_ERROR;
  }

  SetMem (s, 64, 0x3C);
  pm_metal_mem_free (s);
  if (pm_metal_mem_lookup (PM_METAL_SMOKE_ID) != NULL) {
    pm_metal_log ("metal-mem: id not cleared");
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  (VOID)SystemTable;
  pm_metal_log_init ();

  (VOID)pm_metal_esp_init (ImageHandle);

  Status = MetalMemBringUp ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (pm_metal_boot_harvest_devices () != 0) {
    pm_metal_log ("metal-boot: device harvest failed");
    return EFI_DEVICE_ERROR;
  }

  if (pm_metal_gfx_harvest () != 0) {
    pm_metal_log ("metal-gfx: GOP harvest failed");
    return EFI_UNSUPPORTED;
  }

  if (pm_metal_run_init ((unsigned)mCpuCount) != 0) {
    pm_metal_log ("metal-run: init failed");
    return EFI_OUT_OF_RESOURCES;
  }

  /* RAM-cache while ESP/SimpleFileSystem still live. */
  if (pm_metal_esp_ready ()) {
    (VOID)pm_metal_esp_preload ("mods/tests/async_fs.txt");
    (VOID)pm_metal_esp_preload ("mods/tests/autotest");
    (VOID)pm_metal_esp_preload ("metal/net.conf");
  }

  pm_metal_boot_print_floor_tree (
    mClaimMiB,
    mMapBytes,
    mHoleMiB,
    mHeapBytes,
    mStackKiB,
    (unsigned)mCpuCount
    );

  if (pm_metal_port_takeover_and_run (ImageHandle, (unsigned)mCpuCount) != 0) {
    pm_metal_log ("metal-ebs: failed - no fallback (owned path required)");
    return EFI_DEVICE_ERROR;
  }

  gRT->ResetSystem (EfiResetShutdown, EFI_SUCCESS, 0, NULL);
  return EFI_SUCCESS;
}
