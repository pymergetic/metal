/** @file
  metal.efi — UEFI entry: banner, claim RAM, coop allocator smoke (docs/COOP_MEMORY.md).
**/
#include <Uefi.h>
#include <Pi/PiMultiPhase.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/MpService.h>

#include "mem/metal_mem.h"
#include "cpu/metal_run.h"
#include "cpu/metal_stack.h"
#include "cpu/metal_coro.h"

/* Leave this much conventional free for Boot Services until ExitBootServices. */
#define METAL_BS_RESERVE_PAGES  ((UINTN)(16 * 1024 * 1024 / EFI_PAGE_SIZE))

/* ADD amount posted to every CPU inbox during runner smoke. */
#define METAL_RUN_SMOKE_ADD  100u

STATIC
VOID
EFIAPI
MetalApProcedure (
  IN OUT VOID  *Buffer
  )
{
  /* AP: leave EFI MP stack, run on homogeneous LOCAL stack. */
  metal_run_enter ((unsigned)(UINTN)Buffer);
}

/**
  Logical CPU count from EFI_MP_SERVICES_PROTOCOL, or 1 if absent.
*/
STATIC
UINTN
MetalEfiCpuCount (
  VOID
  )
{
  EFI_STATUS                 Status;
  EFI_MP_SERVICES_PROTOCOL  *Mp;
  UINTN                      NumberOfProcessors;
  UINTN                      NumberOfEnabledProcessors;

  Mp = NULL;
  Status = gBS->LocateProtocol (&gEfiMpServiceProtocolGuid, NULL, (VOID **)&Mp);
  if (EFI_ERROR (Status) || Mp == NULL) {
    return 1;
  }

  NumberOfProcessors        = 0;
  NumberOfEnabledProcessors = 0;
  Status = Mp->GetNumberOfProcessors (Mp, &NumberOfProcessors, &NumberOfEnabledProcessors);
  if (EFI_ERROR (Status) || NumberOfEnabledProcessors == 0) {
    return 1;
  }

  return NumberOfEnabledProcessors;
}

/**
  Largest contiguous conventional free region (pages). AllocateAnyPages needs
  one hole; summing all conventional pages overstates what we can claim.
*/
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
  Map     = NULL;
  Status  = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVer);
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
  Map     = NULL;
  Status  = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVer);
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

  ArenaPa  = 0;
  CpuCount = MetalEfiCpuCount ();
  Print (L"metal-mem: EFI reports %Lu enabled CPU(s)\r\n", (UINT64)CpuCount);

  Status = MetalLargestConventionalPages (&LargestPages);
  if (EFI_ERROR (Status)) {
    Print (L"metal-mem: conventional scan failed: %r\r\n", Status);
    return Status;
  }

  Print (L"metal-mem: largest conventional hole %Lu MiB\r\n",
         ((UINT64)LargestPages * EFI_PAGE_SIZE) / (1024ULL * 1024ULL));

  if (LargestPages <= METAL_BS_RESERVE_PAGES + 4) {
    Print (L"metal-mem: not enough contiguous conventional RAM\r\n");
    return EFI_OUT_OF_RESOURCES;
  }

  /* Claim the biggest hole minus BS reserve; trim to multiple of 4 pages. */
  ClaimPages = LargestPages - METAL_BS_RESERVE_PAGES;
  ClaimPages = (ClaimPages / 4) * 4;
  if (ClaimPages < 4) {
    Print (L"metal-mem: claim too small after reserve\r\n");
    return EFI_OUT_OF_RESOURCES;
  }

  Status = gBS->AllocatePages (
                  AllocateAnyPages,
                  EfiLoaderData,
                  ClaimPages,
                  &ArenaPa
                  );
  if (EFI_ERROR (Status)) {
    Print (L"AllocatePages arena failed: %r (%Lu MiB)\r\n",
           Status,
           ((UINT64)ClaimPages * EFI_PAGE_SIZE) / (1024ULL * 1024ULL));
    return Status;
  }

  if (metal_mem_init (
        (VOID *)(UINTN)ArenaPa,
        (size_t)ClaimPages * EFI_PAGE_SIZE,
        (unsigned)CpuCount
        ) != 0)
  {
    Print (L"metal_mem_init failed (cpus=%Lu claim=%Lu MiB)\r\n",
           (UINT64)CpuCount,
           ((UINT64)ClaimPages * EFI_PAGE_SIZE) / (1024ULL * 1024ULL));
    return EFI_OUT_OF_RESOURCES;
  }

  /* Homogeneous LOCAL stacks so the layout tree can show them. */
  if (metal_stack_init ((unsigned)CpuCount) != 0) {
    Print (L"metal-mem: stack init failed\r\n");
    return EFI_OUT_OF_RESOURCES;
  }

  {
    UINT64  ClaimMiB;
    UINT64  MapBytes;
    UINT64  HeapBytes;
    UINT64  HoleMiB;
    UINT64  StackKiB;
    UINTN   i;
    UINTN   N;

    ClaimMiB  = (UINT64)(metal_mem_arena_bytes () / (1024 * 1024));
    MapBytes  = (UINT64)metal_mem_map_bytes ();
    HeapBytes = (UINT64)metal_mem_heap_bytes ();
    HoleMiB   = (UINT64)(metal_mem_hole_bytes () / (1024 * 1024));
    N         = metal_mem_n_cpus ();
    StackKiB  = (UINT64)(metal_stack_bytes () / 1024);

    Print (L"metal-mem layout: %Lu MiB claimed  (dual-span, %Lu cpu)\r\n",
           ClaimMiB, (UINT64)N);
    Print (L"|\r\n");
    if (MapBytes < 1024 * 1024) {
      Print (L"+-- MAP   %Lu KiB  stacks / job grants\r\n",
             MapBytes / 1024);
    } else {
      Print (L"+-- MAP   %Lu MiB  stacks / job grants\r\n",
             MapBytes / (1024 * 1024));
    }

    for (i = 0; i < N; i++) {
      if (i + 1 < N) {
        Print (L"|   +-- cpu%Lu stack  %Lu KiB\r\n", (UINT64)i, StackKiB);
      } else {
        Print (L"|   `-- cpu%Lu stack  %Lu KiB\r\n", (UINT64)i, StackKiB);
      }
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
  }

  /* HEAP smoke — anonymous malloc-like */
  p = metal_alloc (128, METAL_MEM_HEAP, METAL_ID_NONE);
  q = metal_alloc (4096, METAL_MEM_HEAP, METAL_ID_NONE);
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

  metal_free (q);
  metal_free (p);

  /* Published id + lookup on heap */
#define METAL_SMOKE_ID  0x4d544c01u

  s = metal_alloc (64, METAL_MEM_HEAP, METAL_SMOKE_ID);
  if (s == NULL) {
    Print (L"metal-mem: publish alloc failed\r\n");
    return EFI_OUT_OF_RESOURCES;
  }

  if (metal_lookup (METAL_SMOKE_ID) != s) {
    Print (L"metal-mem: lookup mismatch\r\n");
    return EFI_DEVICE_ERROR;
  }

  SetMem (s, 64, 0x3C);
  metal_free (s);
  if (metal_lookup (METAL_SMOKE_ID) != NULL) {
    Print (L"metal-mem: id not cleared\r\n");
    return EFI_DEVICE_ERROR;
  }

  Print (L"metal-mem: ok\r\n");
  return EFI_SUCCESS;
}

typedef struct {
  UINT32  step;
  UINT32  acc;
} MetalCoroAddState;

STATIC
metal_coro_status_t
MetalCoroAddFn (
  VOID  *State,
  VOID  *In,
  VOID  *Out
  )
{
  MetalCoroAddState  *st;
  UINT32             *in_v;
  UINT32             *out_v;

  st   = (MetalCoroAddState *)State;
  in_v = (UINT32 *)In;
  out_v = (UINT32 *)Out;
  if (st == NULL || in_v == NULL || out_v == NULL) {
    return METAL_CORO_ERROR;
  }

  if (st->step == 0) {
    st->acc  = *in_v;
    st->step = 1;
    return METAL_CORO_WAIT; /* stackless yield point */
  }

  *out_v = st->acc + METAL_RUN_SMOKE_ADD;
  return METAL_CORO_DONE;
}

/**
  Start a runner on every enabled CPU (EFI MP APs + BSP), pump test
  messages through SHARED inboxes, join, check sums.
*/
STATIC
EFI_STATUS
MetalRunSmoke (
  VOID
  )
{
  EFI_STATUS                 Status;
  EFI_MP_SERVICES_PROTOCOL  *Mp;
  UINTN                      N;
  UINTN                      i;

  N = metal_mem_n_cpus ();
  if (N == 0) {
    return EFI_DEVICE_ERROR;
  }

  if (metal_run_init ((unsigned)N) != 0) {
    Print (L"metal-run: init failed\r\n");
    return EFI_OUT_OF_RESOURCES;
  }

  Print (L"metal-run: %Lu inbox(es) ready\r\n", (UINT64)N);

  /* Per-job coros (private map grant) + classic ADD, then STOP. */
  for (i = 0; i < N; i++) {
    metal_coro_t  *coro;
    UINT32        *in_v;

    coro = metal_coro_create (
             MetalCoroAddFn,
             sizeof (MetalCoroAddState),
             sizeof (UINT32),
             sizeof (UINT32),
             4096
             );
    if (coro == NULL) {
      Print (L"metal-coro: create cpu%Lu failed\r\n", (UINT64)i);
      return EFI_OUT_OF_RESOURCES;
    }

    in_v  = (UINT32 *)metal_coro_in (coro);
    *in_v = (UINT32)i;
    if (metal_coro_spawn (coro, (unsigned)i) != 0) {
      Print (L"metal-coro: spawn cpu%Lu failed\r\n", (UINT64)i);
      return EFI_OUT_OF_RESOURCES;
    }

    /* Second resume after WAIT: post CORO again so it can finish. */
    if (metal_run_post_ex ((unsigned)i, METAL_MSG_CORO, 0, (UINTN)(VOID *)coro) != 0) {
      Print (L"metal-coro: re-post cpu%Lu failed\r\n", (UINT64)i);
      return EFI_OUT_OF_RESOURCES;
    }

    if (metal_run_post ((unsigned)i, METAL_MSG_ADD, METAL_RUN_SMOKE_ADD) != 0) {
      Print (L"metal-run: post ADD cpu%Lu failed\r\n", (UINT64)i);
      return EFI_OUT_OF_RESOURCES;
    }

    if (metal_run_post ((unsigned)i, METAL_MSG_STOP, 0) != 0) {
      Print (L"metal-run: post STOP cpu%Lu failed\r\n", (UINT64)i);
      return EFI_OUT_OF_RESOURCES;
    }
  }

  /*
    APs first (blocking StartupThisAP): each drains its inbox on that core.
    Then BSP runs cpu0. Parallel join can come later; smoke needs every core.
  */
  Mp     = NULL;
  Status = gBS->LocateProtocol (&gEfiMpServiceProtocolGuid, NULL, (VOID **)&Mp);
  if (!EFI_ERROR (Status) && Mp != NULL && N > 1) {
    for (i = 1; i < N; i++) {
      Status = Mp->StartupThisAP (
                     Mp,
                     MetalApProcedure,
                     i,
                     NULL,
                     0,
                     (VOID *)(UINTN)i,
                     NULL
                     );
      if (EFI_ERROR (Status)) {
        Print (L"metal-run: StartupThisAP(%Lu) failed: %r\r\n", (UINT64)i, Status);
        return Status;
      }

      Print (L"metal-run: cpu%Lu enter/leave ok\r\n", (UINT64)i);
    }
  } else if (N > 1) {
    Print (L"metal-run: MP protocol missing; cannot start APs\r\n");
    return EFI_UNSUPPORTED;
  }

  metal_run_enter (0);
  Print (L"metal-run: cpu0 enter/leave ok\r\n");

  if (metal_run_check ((unsigned)N, METAL_RUN_SMOKE_ADD) != 0) {
    Print (L"metal-run: check failed (expect ADD=%u per cpu)\r\n", METAL_RUN_SMOKE_ADD);
    return EFI_DEVICE_ERROR;
  }

  for (i = 0; i < N; i++) {
    Print (L"metal-run: cpu%Lu  done=%u  sum=%u\r\n",
           (UINT64)i,
           metal_run_done ((unsigned)i),
           metal_run_sum ((unsigned)i));
  }

  Print (L"metal-coro: ok\r\n");
  Print (L"metal-run: ok\r\n");
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

  (VOID)ImageHandle;
  (VOID)SystemTable;

  Print (L"\r\n");
  Print (L"pymergetic efi — freestanding Metal bring-up\r\n");
  Print (L"\r\n");

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

  gRT->ResetSystem (EfiResetShutdown, EFI_SUCCESS, 0, NULL);
  return EFI_SUCCESS;
}
