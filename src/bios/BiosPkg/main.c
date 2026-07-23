/** @file
  metal.elf — Multiboot1 BIOS entry → same floor as EFI. (impl: bios)
**/
#include <pymergetic/metal/boot/port.h>
#include <pymergetic/metal/boot/boot.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/dev/gfx/gfx.h>
#include <pymergetic/metal/fs/esp/esp.h>
#include <runtime/mem/mem.h>
#include <runtime/stack/stack.h>
#include <runtime/run/run.h>
#include <runtime/time/time.h>
#include <pymergetic/metal/util/fourcc.h>

#include "../shim/PmBiosUefi.h"
#include "../shim/Library/MemoryAllocationLib.h"
#include "../shim/Library/IoLib.h"

#include "fb_detect.h"

#define MB1_BOOTLOADER_MAGIC PM_METAL_UTIL_FOURCC(0x2B, 0xAD, 0xB0, 0x02)
#define MB2_BOOTLOADER_MAGIC PM_METAL_UTIL_FOURCC(0x36, 0xD7, 0x62, 0x89)

typedef struct {
  UINT32 TotalSize;
  UINT32 Reserved;
} mb2_info_t;

typedef struct {
  UINT32 Type;
  UINT32 Size;
} mb2_tag_t;

typedef struct {
  UINT32 Type;
  UINT32 Size;
  UINT32 EntrySize;
  UINT32 EntryVersion;
} mb2_mmap_t;

typedef struct {
  UINT64 Addr;
  UINT64 Len;
  UINT32 Type;
  UINT32 Reserved;
} mb2_mmap_entry_t;

typedef struct {
  UINT32 Type;
  UINT32 Size;
  UINT64 Addr;
  UINT32 Pitch;
  UINT32 Width;
  UINT32 Height;
  UINT8 Bpp;
  UINT8 TypeFb;
  UINT16 Reserved;
} mb2_fb_t;

typedef struct {
  UINT32 Flags;
  UINT32 MemLower;
  UINT32 MemUpper;
  UINT32 BootDevice;
  UINT32 Cmdline;
  UINT32 ModsCount;
  UINT32 ModsAddr;
  UINT32 Syms[4];
  UINT32 MmapLength;
  UINT32 MmapAddr;
  UINT32 DrivesLength;
  UINT32 DrivesAddr;
  UINT32 ConfigTable;
  UINT32 BootLoaderName;
  UINT32 ApmTable;
  UINT32 VbeControlInfo;
  UINT32 VbeModeInfo;
  UINT16 VbeMode;
  UINT16 VbeInterfaceSeg;
  UINT16 VbeInterfaceOff;
  UINT16 VbeInterfaceLen;
  UINT64 FramebufferAddr;
  UINT32 FramebufferPitch;
  UINT32 FramebufferWidth;
  UINT32 FramebufferHeight;
  UINT8 FramebufferBpp;
  UINT8 FramebufferType;
} mb1_info_t;

typedef struct {
  UINT32 Size;
  UINT64 Addr;
  UINT64 Len;
  UINT32 Type;
} __attribute__((packed)) mb1_mmap_entry_t;

STATIC UINT64 mClaimMiB;
STATIC UINT64 mMapBytes;
STATIC UINT64 mHoleMiB;
STATIC UINT64 mHeapBytes;
STATIC UINT64 mStackKiB;
STATIC UINT64 mPhysBytes;
STATIC UINTN mCpuCount = 1;
STATIC UINTN mVgaRow;
STATIC UINTN mVgaCol;

/* Multiboot mmap: 1=available, 3=ACPI reclaim, 4=NVS — count as system RAM. */
STATIC VOID
MetalBiosAddPhys (
  UINT32  type,
  UINT64  len
  )
{
  if (type == 1u || type == 3u || type == 4u) {
    mPhysBytes += len;
  }
}

STATIC VOID
Com1Init(VOID)
{
  IoWrite8(0x3F8 + 1, 0x00);
  IoWrite8(0x3F8 + 3, 0x80);
  IoWrite8(0x3F8 + 0, 0x01);
  IoWrite8(0x3F8 + 1, 0x00);
  IoWrite8(0x3F8 + 3, 0x03);
  IoWrite8(0x3F8 + 2, 0xC7);
  IoWrite8(0x3F8 + 4, 0x0B);
}

/* iPXE leaves VGA text mode; Multiboot FB is usually absent on real PXE. */
STATIC VOID
VgaClear(VOID)
{
  volatile UINT16 *vga = (volatile UINT16 *)(UINTN)0xB8000;
  UINTN i;

  for (i = 0; i < 80u * 25u; i++)
    vga[i] = 0x0F20;
  mVgaRow = 0;
  mVgaCol = 0;
}

STATIC VOID
VgaPutc(CHAR8 c)
{
  volatile UINT16 *vga = (volatile UINT16 *)(UINTN)0xB8000;

  if (c == '\r') {
    mVgaCol = 0;
    return;
  }
  if (c == '\n') {
    mVgaCol = 0;
    if (++mVgaRow >= 25u)
      mVgaRow = 0;
    return;
  }
  if (mVgaCol >= 80u) {
    mVgaCol = 0;
    if (++mVgaRow >= 25u)
      mVgaRow = 0;
  }
  vga[mVgaRow * 80u + mVgaCol] = (UINT16)((UINT8)c | 0x0F00);
  mVgaCol++;
}

STATIC VOID
EarlyPuts(CONST CHAR8 *s)
{
  while (s && *s) {
    CHAR8 c = *s++;
    UINTN spins;

    VgaPutc(c);
    for (spins = 0; spins < 100000; spins++) {
      if ((IoRead8(0x3FD) & 0x20) != 0)
	break;
    }
    IoWrite8(0x3F8, (UINT8)c);
  }
}

STATIC INT32
ClaimBest(UINT64 best_addr, UINT64 best_len, VOID **arena_out, UINTN *bytes_out)
{
  /* Keep arena above trampoline (1MiB) + Metal image (4MiB). */
  CONST UINT64 kMinAddr = 0x800000ull;

  if (best_len < 32ull * 1024 * 1024)
    return -1;
  if (best_addr < kMinAddr) {
    UINT64 adj = kMinAddr - best_addr;
    if (adj >= best_len)
      return -1;
    best_addr += adj;
    best_len -= adj;
  } else {
    best_addr += 2ull * 1024 * 1024;
    best_len -= 2ull * 1024 * 1024;
  }
  if (best_len > 16ull * 1024 * 1024)
    best_len -= 16ull * 1024 * 1024;
  best_len &= ~0xfffull;
  if (best_len < 32ull * 1024 * 1024)
    return -1;
  *arena_out = (VOID *)(UINTN)best_addr;
  *bytes_out = (UINTN)best_len;
  return 0;
}

STATIC INT32
ParseMb2(UINT32 magic, VOID *info, VOID **arena_out, UINTN *bytes_out)
{
  mb2_info_t *hdr;
  UINT8 *p;
  UINT8 *end;
  UINT64 best_addr = 0;
  UINT64 best_len = 0;

  *arena_out = NULL;
  *bytes_out = 0;
  if (magic != MB2_BOOTLOADER_MAGIC || info == NULL)
    return -1;

  mPhysBytes = 0;
  hdr = (mb2_info_t *)info;
  p = (UINT8 *)info + 8;
  end = (UINT8 *)info + hdr->TotalSize;
  while (p + sizeof(mb2_tag_t) <= end) {
    mb2_tag_t *tag = (mb2_tag_t *)p;
    if (tag->Type == 0)
      break;
    if (tag->Type == 6) {
      mb2_mmap_t *mm = (mb2_mmap_t *)tag;
      UINT8 *e = p + 16;
      UINT8 *mend = p + tag->Size;
      while (e + mm->EntrySize <= mend) {
	mb2_mmap_entry_t *ent = (mb2_mmap_entry_t *)e;
	MetalBiosAddPhys (ent->Type, ent->Len);
	if (ent->Type == 1 && ent->Len > best_len && ent->Addr >= 0x100000ull) {
	  best_addr = ent->Addr;
	  best_len = ent->Len;
	}
	e += mm->EntrySize;
      }
    } else if (tag->Type == 8) {
      mb2_fb_t *fb = (mb2_fb_t *)tag;
      if (fb->Bpp == 32 && fb->Width >= 320 && fb->Height >= 200) {
	UINT32 ppsl = fb->Pitch / 4;
	if (ppsl == 0)
	  ppsl = fb->Width;
	pm_metal_port_set_framebuffer((VOID *)(UINTN)fb->Addr, fb->Width,
				      fb->Height, ppsl);
      }
    }
    p = (UINT8 *)(((UINTN)p + tag->Size + 7) & ~(UINTN)7);
  }
  return ClaimBest(best_addr, best_len, arena_out, bytes_out);
}

STATIC INT32
ParseMb1(UINT32 magic, VOID *info, VOID **arena_out, UINTN *bytes_out)
{
  mb1_info_t *hdr;
  UINT64 best_addr = 0;
  UINT64 best_len = 0;

  *arena_out = NULL;
  *bytes_out = 0;
  if (magic != MB1_BOOTLOADER_MAGIC || info == NULL)
    return -1;

  mPhysBytes = 0;
  hdr = (mb1_info_t *)info;

  if ((hdr->Flags & (1u << 12)) != 0 && hdr->FramebufferBpp == 32
      && hdr->FramebufferWidth >= 320 && hdr->FramebufferHeight >= 200) {
    UINT32 ppsl = hdr->FramebufferPitch / 4;
    if (ppsl == 0)
      ppsl = hdr->FramebufferWidth;
    pm_metal_port_set_framebuffer((VOID *)(UINTN)hdr->FramebufferAddr,
				  hdr->FramebufferWidth, hdr->FramebufferHeight,
				  ppsl);
  }

  if ((hdr->Flags & (1u << 6)) != 0 && hdr->MmapAddr != 0 && hdr->MmapLength != 0) {
    UINT8 *p = (UINT8 *)(UINTN)hdr->MmapAddr;
    UINT8 *end = p + hdr->MmapLength;
    while (p + sizeof(mb1_mmap_entry_t) <= end) {
      mb1_mmap_entry_t *ent = (mb1_mmap_entry_t *)p;
      UINT32 esz = ent->Size + 4;
      if (esz < sizeof(mb1_mmap_entry_t) || p + esz > end)
	break;
      MetalBiosAddPhys (ent->Type, ent->Len);
      if (ent->Type == 1 && ent->Len > best_len && ent->Addr >= 0x100000ull) {
	best_addr = ent->Addr;
	best_len = ent->Len;
      }
      p += esz;
    }
  }
  if (best_len == 0 && (hdr->Flags & (1u << 0)) != 0) {
    best_addr = 0x100000ull;
    best_len = (UINT64)hdr->MemUpper * 1024ull;
    if (mPhysBytes == 0) {
      mPhysBytes = 1024ull * 1024ull + best_len;
    }
  }

  return ClaimBest(best_addr, best_len, arena_out, bytes_out);
}

STATIC INT32
EnsureFramebuffer(VOID)
{
  VOID *fb;
  unsigned w, h, ppsl;

  /*
   * Same model as blk/net: every detector runs.
   * Multiboot may already have registered an LFB during parse.
   * Bochs and VESA each register if they find hardware/firmware.
   */
  (VOID)pm_bios_fb_bochs_detect();
  (VOID)pm_bios_fb_vesa_detect();

  if (pm_metal_port_get_framebuffer(&fb, &w, &h, &ppsl) == 0)
    return 0;
  return -1;
}

STATIC INT32
MetalMemBringUp(VOID *arena, UINTN bytes)
{
  UINTN shim = 4u * 1024 * 1024;

  if (bytes <= shim + (32u * 1024 * 1024))
    return -1;
  pm_bios_shim_set_heap(arena, shim);
  arena = (UINT8 *)arena + shim;
  bytes -= shim;

  if (pm_metal_mem_init(arena, bytes, (unsigned)mCpuCount) != 0)
    return -1;
  if (mPhysBytes != 0) {
    pm_metal_mem_set_phys_bytes ((size_t)mPhysBytes);
  }
  if (pm_metal_stack_init((unsigned)mCpuCount) != 0)
    return -1;

  mClaimMiB = (UINT64)(pm_metal_mem_arena_bytes() / (1024 * 1024));
  mMapBytes = (UINT64)pm_metal_mem_map_bytes();
  mHeapBytes = (UINT64)pm_metal_mem_heap_bytes();
  mHoleMiB = (UINT64)(pm_metal_mem_hole_bytes() / (1024 * 1024));
  mCpuCount = pm_metal_mem_n_cpus();
  mStackKiB = (UINT64)(pm_metal_stack_bytes() / 1024);
  return 0;
}

void
pm_metal_bios_main(UINT32 magic, VOID *mb_info)
{
  VOID *arena;
  UINTN bytes;

  Com1Init();
  VgaClear();
  EarlyPuts("metal-bios: entry\r\n");

  if (ParseMb2(magic, mb_info, &arena, &bytes) != 0
      && ParseMb1(magic, mb_info, &arena, &bytes) != 0) {
    EarlyPuts("metal-bios: multiboot parse/mem failed\r\n");
    pm_metal_port_reset(0);
  }

  pm_metal_log_init();
  /* BIOS Print() is COM1 — close UEFI viewport so UART attach is not doubled. */
  pm_metal_log_ebs_close_uefi();
  pm_metal_log_attach_uart();
  (VOID)pm_metal_esp_init(NULL);

  if (MetalMemBringUp(arena, bytes) != 0) {
    EarlyPuts("metal-bios: mem bring-up failed\r\n");
    pm_metal_port_reset(0);
  }

  /* Calibrate TSC before IDE/harvest waits (must not hang in PIT). */
  pm_metal_time_init();

  if (EnsureFramebuffer() != 0) {
    EarlyPuts("metal-bios: no framebuffer (COM1 only)\r\n");
  }

  if (pm_metal_boot_harvest_devices() != 0) {
    EarlyPuts("metal-bios: device harvest failed\r\n");
    pm_metal_port_reset(0);
  }

  if (pm_metal_gfx_harvest() != 0) {
    EarlyPuts("metal-bios: gfx harvest skipped\r\n");
  }

  if (pm_metal_run_init((unsigned)mCpuCount) != 0) {
    EarlyPuts("metal-bios: run init failed\r\n");
    pm_metal_port_reset(0);
  }

  pm_metal_boot_print_floor_tree(mClaimMiB, mMapBytes, mHoleMiB, mHeapBytes,
				 mStackKiB, (unsigned)mCpuCount);

  pm_metal_port_takeover_and_run(NULL, (unsigned)mCpuCount);
}
