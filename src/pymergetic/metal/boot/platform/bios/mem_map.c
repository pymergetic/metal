#if defined(PM_METAL_BOOT_TARGET_BIOS)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_EFI)
#error "boot/bios/mem_map.c built with PM_METAL_BOOT_TARGET_EFI"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

#include <stddef.h>
#include <stdint.h>

#include <pymergetic/metal/boot/platform/mem_map.h>

#define MB1_BOOTLOADER_MAGIC 0x2BADB002u
#define MB2_BOOTLOADER_MAGIC 0x36D76289u
#define MAX_REGIONS 128u

extern char __pm_metal_image_base[];
extern char __pm_metal_image_end[];

typedef struct {
  uint32_t total_size;
  uint32_t reserved;
} mb2_info_t;

typedef struct {
  uint32_t type;
  uint32_t size;
} mb2_tag_t;

typedef struct {
  uint32_t type;
  uint32_t size;
  uint32_t entry_size;
  uint32_t entry_version;
} mb2_mmap_t;

typedef struct {
  uint64_t addr;
  uint64_t len;
  uint32_t type;
  uint32_t reserved;
} mb2_mmap_entry_t;

typedef struct {
  uint32_t flags;
  uint32_t mem_lower;
  uint32_t mem_upper;
  uint32_t boot_device;
  uint32_t cmdline;
  uint32_t mods_count;
  uint32_t mods_addr;
  uint32_t syms[4];
  uint32_t mmap_length;
  uint32_t mmap_addr;
} mb1_info_t;

typedef struct {
  uint32_t size;
  uint64_t addr;
  uint64_t len;
  uint32_t type;
} __attribute__((packed)) mb1_mmap_entry_t;

static pm_metal_boot_mem_region_t g_regions[MAX_REGIONS];
static uint32_t g_n_regions;
static int g_ready;

static uint32_t map_mb_type(uint32_t t)
{
  if (t == 1u) {
    return (uint32_t)PM_METAL_BOOT_MEM_AVAILABLE;
  }
  if (t == 3u) {
    return (uint32_t)PM_METAL_BOOT_MEM_ACPI_RECLAIM;
  }
  if (t == 4u) {
    return (uint32_t)PM_METAL_BOOT_MEM_ACPI_NVS;
  }
  if (t == 2u) {
    return (uint32_t)PM_METAL_BOOT_MEM_RESERVED;
  }
  return (uint32_t)PM_METAL_BOOT_MEM_OTHER;
}

static int32_t push_region(uint64_t addr, uint64_t len, uint32_t mb_type)
{
  if (len == 0 || g_n_regions >= MAX_REGIONS) {
    return -1;
  }
  g_regions[g_n_regions].addr = addr;
  g_regions[g_n_regions].len = len;
  g_regions[g_n_regions].type = map_mb_type(mb_type);
  g_regions[g_n_regions].reserved = 0;
  g_n_regions++;
  return 0;
}

static int32_t ingest_mb2(const void *info)
{
  const mb2_info_t *hdr = (const mb2_info_t *)info;
  const uint8_t *p;
  const uint8_t *end;

  if (hdr == NULL || hdr->total_size < 8u) {
    return -1;
  }
  p = (const uint8_t *)info + 8;
  end = (const uint8_t *)info + hdr->total_size;
  while (p + sizeof(mb2_tag_t) <= end) {
    const mb2_tag_t *tag = (const mb2_tag_t *)p;

    if (tag->type == 0u) {
      break;
    }
    if (tag->type == 6u && tag->size >= 16u) {
      const mb2_mmap_t *mm = (const mb2_mmap_t *)tag;
      const uint8_t *e = p + 16;
      const uint8_t *mend = p + tag->size;

      if (mm->entry_size < sizeof(mb2_mmap_entry_t)) {
        return -1;
      }
      while (e + mm->entry_size <= mend) {
        const mb2_mmap_entry_t *ent = (const mb2_mmap_entry_t *)e;

        if (push_region(ent->addr, ent->len, ent->type) != 0) {
          return -1;
        }
        e += mm->entry_size;
      }
    }
    p = (const uint8_t *)((((uintptr_t)p + tag->size + 7u) & ~(uintptr_t)7u));
  }
  return (g_n_regions > 0u) ? 0 : -1;
}

static int32_t ingest_mb1(const void *info)
{
  const mb1_info_t *hdr = (const mb1_info_t *)info;

  if (hdr == NULL) {
    return -1;
  }
  if ((hdr->flags & (1u << 6)) != 0u && hdr->mmap_addr != 0u && hdr->mmap_length != 0u) {
    const uint8_t *p = (const uint8_t *)(uintptr_t)hdr->mmap_addr;
    const uint8_t *end = p + hdr->mmap_length;

    while (p + sizeof(mb1_mmap_entry_t) <= end) {
      const mb1_mmap_entry_t *ent = (const mb1_mmap_entry_t *)p;
      uint32_t esz = ent->size + 4u;

      if (esz < sizeof(mb1_mmap_entry_t) || p + esz > end) {
        break;
      }
      if (push_region(ent->addr, ent->len, ent->type) != 0) {
        return -1;
      }
      p += esz;
    }
  }
  if (g_n_regions == 0u && (hdr->flags & (1u << 0)) != 0u) {
    /* Multiboot mem_lower/mem_upper when mmap tag absent. */
    if (push_region(0x100000ull, (uint64_t)hdr->mem_upper * 1024ull, 1u) != 0) {
      return -1;
    }
  }
  return (g_n_regions > 0u) ? 0 : -1;
}

int32_t pm_metal_boot_bios_mem_map_ingest(uint32_t magic, const void *info)
{
  g_n_regions = 0;
  g_ready = 0;
  if (magic == MB2_BOOTLOADER_MAGIC) {
    if (ingest_mb2(info) != 0) {
      return -1;
    }
  } else if (magic == MB1_BOOTLOADER_MAGIC) {
    if (ingest_mb1(info) != 0) {
      return -1;
    }
  } else {
    return -1;
  }
  g_ready = 1;
  return 0;
}

static int32_t bios_mem_map_get(pm_metal_boot_mem_region_t *out, uint32_t max, uint32_t *n_out)
{
  uint32_t n;
  uint32_t i;

  if (out == NULL || n_out == NULL || !g_ready) {
    return -1;
  }
  n = g_n_regions;
  if (n > max) {
    n = max;
  }
  for (i = 0; i < n; i++) {
    out[i] = g_regions[i];
  }
  *n_out = n;
  return 0;
}

static uintptr_t bios_mem_map_image_base(void)
{
  return (uintptr_t)__pm_metal_image_base;
}

static uintptr_t bios_mem_map_image_end(void)
{
  return (uintptr_t)__pm_metal_image_end;
}

#define BIOS_CLAIM_MIN_BYTES (2u * 1024u * 1024u)
#define BIOS_PAGE 4096ull

static int32_t bios_claim_arena(uint8_t **base_out, size_t *bytes_out)
{
  pm_metal_boot_mem_region_t regs[MAX_REGIONS];
  uint32_t n = 0;
  uint32_t i;
  uint64_t img_end;
  uint64_t best_addr = 0;
  uint64_t best_len = 0;

  if (base_out == NULL || bytes_out == NULL) {
    return -1;
  }
  if (bios_mem_map_get(regs, MAX_REGIONS, &n) != 0 || n == 0u) {
    return -1;
  }
  img_end = ((uint64_t)bios_mem_map_image_end() + (BIOS_PAGE - 1ull)) & ~(BIOS_PAGE - 1ull);

  for (i = 0; i < n; i++) {
    uint64_t start;
    uint64_t end;
    uint64_t len;

    if (regs[i].type != (uint32_t)PM_METAL_BOOT_MEM_AVAILABLE || regs[i].len == 0u) {
      continue;
    }
    end = regs[i].addr + regs[i].len;
    start = regs[i].addr;
    if (start < img_end) {
      start = img_end;
    }
    start = (start + (BIOS_PAGE - 1ull)) & ~(BIOS_PAGE - 1ull);
    if (start >= end) {
      continue;
    }
    len = end - start;
    len &= ~(BIOS_PAGE - 1ull);
    if (len < (uint64_t)BIOS_CLAIM_MIN_BYTES) {
      continue;
    }
    if (len > best_len) {
      best_addr = start;
      best_len = len;
    }
  }
  if (best_len < (uint64_t)BIOS_CLAIM_MIN_BYTES || best_addr > (uint64_t)UINTPTR_MAX) {
    return -1;
  }
  if (best_len > (uint64_t)SIZE_MAX) {
    best_len = (uint64_t)SIZE_MAX;
    best_len &= ~(BIOS_PAGE - 1ull);
  }
  *base_out = (uint8_t *)(uintptr_t)best_addr;
  *bytes_out = (size_t)best_len;
  return 0;
}

static const pm_metal_boot_mem_map_ops_t g_ops = {
  .get = bios_mem_map_get,
  .image_base = bios_mem_map_image_base,
  .image_end = bios_mem_map_image_end,
  .claim_arena = bios_claim_arena,
};

const pm_metal_boot_mem_map_ops_t *pm_metal_boot_mem_map_ops(void)
{
  return &g_ops;
}
