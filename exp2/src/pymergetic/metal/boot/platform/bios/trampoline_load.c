/* Load embedded ELF64 Metal image (linked as binary blob). Freestanding i386.
 *
 * Multiboot1 info + mmap often sit at/above 1MiB. Copy them to low RAM before
 * overlaying Metal at 16MiB (load window starts at image paddr).
 */
#include <stdint.h>

extern char _binary_metal_elf_start[];
extern char _binary_metal_elf_end[];
extern uint32_t mb_magic;
extern uint32_t mb_info;

#define MB_SAVE_INFO 0x50000u
#define MB_SAVE_MMAP 0x51000u
#define MB_SAVE_INFO_MAX 256u
#define MB_SAVE_MMAP_MAX 0xE000u

typedef struct {
  uint8_t e_ident[16];
  uint16_t e_type;
  uint16_t e_machine;
  uint32_t e_version;
  uint32_t e_entry_lo;
  uint32_t e_entry_hi;
  uint32_t e_phoff_lo;
  uint32_t e_phoff_hi;
  uint32_t e_shoff_lo;
  uint32_t e_shoff_hi;
  uint32_t e_flags;
  uint16_t e_ehsize;
  uint16_t e_phentsize;
  uint16_t e_phnum;
} Elf64_Ehdr32;

typedef struct {
  uint32_t p_type;
  uint32_t p_flags;
  uint32_t p_offset_lo;
  uint32_t p_offset_hi;
  uint32_t p_vaddr_lo;
  uint32_t p_vaddr_hi;
  uint32_t p_paddr_lo;
  uint32_t p_paddr_hi;
  uint32_t p_filesz_lo;
  uint32_t p_filesz_hi;
  uint32_t p_memsz_lo;
  uint32_t p_memsz_hi;
} Elf64_Phdr32;

static void *phys_ptr(uint32_t phys)
{
  return (void *)(uintptr_t)phys;
}

static void cpy(void *dst, const void *src, uint32_t n)
{
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;

  while (n--) {
    *d++ = *s++;
  }
}

static void zero(void *dst, uint32_t n)
{
  uint8_t *d = (uint8_t *)dst;

  while (n--) {
    *d++ = 0;
  }
}

static void preserve_mb1(void)
{
  uint32_t *info;
  uint32_t flags;
  uint32_t mmap_len;
  uint32_t mmap_addr;

  if (mb_magic != 0x2BADB002u || mb_info == 0u) {
    return;
  }

  info = (uint32_t *)phys_ptr(mb_info);
  cpy(phys_ptr(MB_SAVE_INFO), info, MB_SAVE_INFO_MAX);
  mb_info = MB_SAVE_INFO;

  flags = info[0];
  if ((flags & (1u << 6)) == 0u) {
    return;
  }

  mmap_len = info[11];
  mmap_addr = info[12];
  if (mmap_addr == 0u || mmap_len == 0u || mmap_len > MB_SAVE_MMAP_MAX) {
    return;
  }

  cpy(phys_ptr(MB_SAVE_MMAP), phys_ptr(mmap_addr), mmap_len);
  ((uint32_t *)phys_ptr(MB_SAVE_INFO))[12] = MB_SAVE_MMAP;
}

uint32_t trampoline_load_elf64(void)
{
  const uint8_t *base = (const uint8_t *)_binary_metal_elf_start;
  const Elf64_Ehdr32 *eh;
  const Elf64_Phdr32 *ph;
  uint32_t i;

  preserve_mb1();

  eh = (const Elf64_Ehdr32 *)base;
  if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' || eh->e_ident[2] != 'L'
      || eh->e_ident[3] != 'F') {
    return 0;
  }
  if (eh->e_ident[4] != 2) {
    return 0;
  }
  if (eh->e_phoff_hi != 0u) {
    return 0;
  }

  ph = (const Elf64_Phdr32 *)(base + eh->e_phoff_lo);
  for (i = 0; i < eh->e_phnum; i++) {
    const Elf64_Phdr32 *p =
        (const Elf64_Phdr32 *)((const uint8_t *)ph + i * eh->e_phentsize);
    uint8_t *dest;
    uint32_t filesz;
    uint32_t memsz;

    if (p->p_type != 1u) {
      continue;
    }
    if (p->p_paddr_hi != 0u || p->p_offset_hi != 0u) {
      return 0;
    }
    dest = (uint8_t *)phys_ptr(p->p_paddr_lo);
    filesz = p->p_filesz_lo;
    memsz = p->p_memsz_lo;
    cpy(dest, base + p->p_offset_lo, filesz);
    if (memsz > filesz) {
      zero(dest + filesz, memsz - filesz);
    }
  }

  if (eh->e_entry_hi != 0u) {
    return 0;
  }
  {
    uint32_t *bi = (uint32_t *)phys_ptr(0x1000000u);

    if (bi[0] == 0x4C54454Du) {
      bi[4] = mb_magic;
      bi[5] = mb_info;
      return bi[2];
    }
  }
  return eh->e_entry_lo;
}
