/** @file
  metal.efi — UEFI entry: claim RAM, mem smoke, run/coro smoke.
**/
#include <Uefi.h>
#include <Pi/PiMultiPhase.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/MpService.h>

#include <mem/mem.h>
#include <stack/stack.h>
#include <time/time.h>
#include <pymergetic/metal/gfx.h>
#include <pymergetic/metal/ui.h>
#include <pymergetic/metal/shell.h>
#include <pymergetic/metal/wasm.h>
#include <pymergetic/metal/esp.h>
#include <pymergetic/metal/util/fourcc.h>
#include <Library/PrintLib.h>

#include "smoke.h"

#define PM_METAL_BS_RESERVE_PAGES  ((UINTN)(16 * 1024 * 1024 / EFI_PAGE_SIZE))
#define PM_METAL_SMOKE_ID          PM_METAL_UTIL_FOURCC ('s', 'm', 'o', 'k')

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
  Entry = Map;
  for (Index = 0; Index < Count; Index++) {
    if (Entry->Type == EfiConventionalMemory && Entry->NumberOfPages > Best) {
      Best = (UINTN)Entry->NumberOfPages;
    }

    Entry = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Entry + DescSize);
  }

  FreePool (Map);
  *LargestPages = Best;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
MetalPrintMemorySummary (
  VOID
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
  UINT64                 TotalPages;
  UINT64                 ConvPages;

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

  TotalPages = 0;
  ConvPages  = 0;
  Count      = MapSize / DescSize;
  Entry      = Map;
  for (Index = 0; Index < Count; Index++) {
    TotalPages += Entry->NumberOfPages;
    if (Entry->Type == EfiConventionalMemory) {
      ConvPages += Entry->NumberOfPages;
    }

    Entry = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Entry + DescSize);
  }

  Print (L"Total memory:        %Lu MiB  (%Lu pages)\r\n",
         (TotalPages * EFI_PAGE_SIZE) / (1024ULL * 1024ULL),
         TotalPages);
  Print (L"Conventional (free): %Lu MiB  (%Lu pages)\r\n",
         (ConvPages * EFI_PAGE_SIZE) / (1024ULL * 1024ULL),
         ConvPages);

  FreePool (Map);
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
  UINTN                 CpuCount;
  VOID                 *p;
  VOID                 *q;
  VOID                 *s;
  UINT32               *w;
  UINT64                ClaimMiB;
  UINT64                MapBytes;
  UINT64                HeapBytes;
  UINT64                HoleMiB;
  UINT64                StackKiB;
  UINTN                 i;
  UINTN                 N;

  CpuCount = MetalEfiCpuCount ();
  Print (L"metal-mem: EFI reports %Lu enabled CPU(s)\r\n", (UINT64)CpuCount);

  Status = MetalLargestConventionalPages (&LargestPages);
  if (EFI_ERROR (Status)) {
    Print (L"metal-mem: conventional scan failed: %r\r\n", Status);
    return Status;
  }

  Print (L"metal-mem: largest conventional hole %Lu MiB\r\n",
         ((UINT64)LargestPages * EFI_PAGE_SIZE) / (1024ULL * 1024ULL));

  if (LargestPages <= PM_METAL_BS_RESERVE_PAGES + 4) {
    Print (L"metal-mem: not enough contiguous conventional RAM\r\n");
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
    Print (L"AllocatePages arena failed: %r\r\n", Status);
    return Status;
  }

  if (pm_metal_mem_init (
        (VOID *)(UINTN)ArenaPa,
        (size_t)ClaimPages * EFI_PAGE_SIZE,
        (unsigned)CpuCount
        ) != 0)
  {
    Print (L"pm_metal_mem_init failed\r\n");
    return EFI_OUT_OF_RESOURCES;
  }

  if (pm_metal_stack_init ((unsigned)CpuCount) != 0) {
    Print (L"metal-mem: stack init failed\r\n");
    return EFI_OUT_OF_RESOURCES;
  }

  ClaimMiB  = (UINT64)(pm_metal_mem_arena_bytes () / (1024 * 1024));
  MapBytes  = (UINT64)pm_metal_mem_map_bytes ();
  HeapBytes = (UINT64)pm_metal_mem_heap_bytes ();
  HoleMiB   = (UINT64)(pm_metal_mem_hole_bytes () / (1024 * 1024));
  N         = pm_metal_mem_n_cpus ();
  StackKiB  = (UINT64)(pm_metal_stack_bytes () / 1024);

  Print (L"metal-mem layout: %Lu MiB claimed  (dual-span, %Lu cpu)\r\n",
         ClaimMiB, (UINT64)N);
  Print (L"|\r\n");
  if (MapBytes < 1024 * 1024) {
    Print (L"+-- MAP   %Lu KiB  stacks / job grants\r\n", MapBytes / 1024);
  } else {
    Print (L"+-- MAP   %Lu MiB  stacks / job grants\r\n",
           MapBytes / (1024 * 1024));
  }

  for (i = 0; i < N; i++) {
    Print (
      (i + 1 < N)
        ? L"|   +-- cpu%Lu stack  %Lu KiB\r\n"
        : L"|   `-- cpu%Lu stack  %Lu KiB\r\n",
      (UINT64)i,
      StackKiB
      );
  }

  Print (L"+-- map_brk\r\n");
  Print (L"+-- HOLE  %Lu MiB\r\n", HoleMiB);
  Print (L"+-- heap_brk\r\n");
  if (HeapBytes < 1024 * 1024) {
    Print (L"`-- HEAP  %Lu KiB  TLSF malloc\r\n", HeapBytes / 1024);
  } else {
    Print (L"`-- HEAP  %Lu MiB  TLSF malloc\r\n",
           HeapBytes / (1024 * 1024));
  }

  p = pm_metal_mem_alloc (128, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  q = pm_metal_mem_alloc (4096, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (p == NULL || q == NULL) {
    Print (L"metal-mem: HEAP alloc failed\r\n");
    return EFI_OUT_OF_RESOURCES;
  }

  SetMem (p, 128, 0xA5);
  SetMem (q, 4096, 0x5A);
  w = (UINT32 *)p;
  if (w[0] != 0xA5A5A5A5) {
    Print (L"metal-mem: HEAP pattern mismatch\r\n");
    return EFI_DEVICE_ERROR;
  }

  pm_metal_mem_free (q);
  pm_metal_mem_free (p);

  s = pm_metal_mem_alloc (64, PM_METAL_MEM_HEAP, PM_METAL_SMOKE_ID);
  if (s == NULL || pm_metal_mem_lookup (PM_METAL_SMOKE_ID) != s) {
    Print (L"metal-mem: publish/lookup failed\r\n");
    return EFI_DEVICE_ERROR;
  }

  SetMem (s, 64, 0x3C);
  pm_metal_mem_free (s);
  if (pm_metal_mem_lookup (PM_METAL_SMOKE_ID) != NULL) {
    Print (L"metal-mem: id not cleared\r\n");
    return EFI_DEVICE_ERROR;
  }

  Print (L"metal-mem: ok\r\n");
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

  Print (L"\r\n");
  Print (L"pymergetic efi — freestanding Metal bring-up\r\n");
  Print (L"\r\n");

  if (pm_metal_esp_init (ImageHandle) != 0) {
    Print (L"metal-esp: unavailable (ESP packages disabled)\r\n");
  }

  Status = MetalPrintMemorySummary ();
  if (EFI_ERROR (Status)) {
    Print (L"GetMemoryMap failed: %r\r\n", Status);
    return Status;
  }

  Status = MetalMemBringUp ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = MetalRunSmoke ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Print (L"\r\nmetal-efi: ok\r\n");

  if (pm_metal_gfx_init () != 0) {
    Print (L"metal-gfx: GOP unavailable (serial-only)\r\n");
  } else if (pm_metal_ui_console_shell () != 0) {
    Print (L"metal-ui: console shell failed\r\n");
  } else {
    INTN  rc;

    Print (L"metal-gfx: ok\r\n");
    Print (L"metal-ui: ok\r\n");
    pm_metal_ui_console_puts ("pymergetic Metal - freestanding bring-up");
    pm_metal_ui_console_puts ("GOP shadow FB + Blt; multi-tab shell + wasm");

    if (pm_metal_wasm_init () != 0) {
      Print (L"metal-wasm: init failed\r\n");
    }

    if (pm_metal_shell_init () != 0) {
      Print (L"metal-shell: init failed\r\n");
    } else {
      Print (L"metal-shell: ok\r\n");
      for (;;) {
        rc = pm_metal_shell_poll ();
        if (rc != 0) {
          break;
        }

        pm_metal_time_msleep (16);
      }
    }

    pm_metal_wasm_fini ();
  }

  gRT->ResetSystem (EfiResetShutdown, EFI_SUCCESS, 0, NULL);
  return EFI_SUCCESS;
}
