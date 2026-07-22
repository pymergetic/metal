/* Load embedded ELF64 Metal image (linked as binary blob). 32-bit C.
 *
 * Multiboot1 info + mmap often sit at/above 1MiB (QEMU places them after the
 * loaded image). Copy them to low RAM before overlaying Metal at 1MiB.
 */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
/* Pointer-width integer for phys→ptr casts (clangd may parse this as LP64). */
typedef __UINTPTR_TYPE__ uptr;

extern char _binary_metal_elf_start[];
extern char _binary_metal_elf_end[];
extern u32 mb_magic;
extern u32 mb_info;

/* Safe low-RAM scratch (below trampoline stack at 0x90000 and Metal at 1MiB). */
#define MB_SAVE_INFO 0x50000u
#define MB_SAVE_MMAP 0x51000u
#define MB_SAVE_INFO_MAX 256u
#define MB_SAVE_MMAP_MAX 0xE000u

typedef struct {
	u8 e_ident[16];
	u16 e_type;
	u16 e_machine;
	u32 e_version;
	u32 e_entry_lo;
	u32 e_entry_hi;
	u32 e_phoff_lo;
	u32 e_phoff_hi;
	u32 e_shoff_lo;
	u32 e_shoff_hi;
	u32 e_flags;
	u16 e_ehsize;
	u16 e_phentsize;
	u16 e_phnum;
} Elf64_Ehdr32;

typedef struct {
	u32 p_type;
	u32 p_flags;
	u32 p_offset_lo;
	u32 p_offset_hi;
	u32 p_vaddr_lo;
	u32 p_vaddr_hi;
	u32 p_paddr_lo;
	u32 p_paddr_hi;
	u32 p_filesz_lo;
	u32 p_filesz_hi;
	u32 p_memsz_lo;
	u32 p_memsz_hi;
} Elf64_Phdr32;

static void *
phys_ptr(u32 phys)
{
	return (void *)(uptr)phys;
}

static void
cpy(void *dst, const void *src, u32 n)
{
	u8 *d = (u8 *)dst;
	const u8 *s = (const u8 *)src;
	while (n--)
		*d++ = *s++;
}

static void
zero(void *dst, u32 n)
{
	u8 *d = (u8 *)dst;
	while (n--)
		*d++ = 0;
}

/* Snapshot Multiboot1 info (+ mmap) out of the Metal load window. */
static void
preserve_mb1(void)
{
	u32 *info;
	u32 flags;
	u32 mmap_len;
	u32 mmap_addr;

	if (mb_magic != 0x2BADB002u || mb_info == 0)
		return;

	info = (u32 *)phys_ptr(mb_info);
	cpy(phys_ptr(MB_SAVE_INFO), info, MB_SAVE_INFO_MAX);
	mb_info = MB_SAVE_INFO;

	flags = info[0];
	if ((flags & (1u << 6)) == 0)
		return;

	mmap_len = info[11];  /* mmap_length */
	mmap_addr = info[12]; /* mmap_addr */
	if (mmap_addr == 0 || mmap_len == 0 || mmap_len > MB_SAVE_MMAP_MAX)
		return;

	cpy(phys_ptr(MB_SAVE_MMAP), phys_ptr(mmap_addr), mmap_len);
	((u32 *)phys_ptr(MB_SAVE_INFO))[12] = MB_SAVE_MMAP;
}

/* Returns 32-bit low half of e_entry (Metal linked below 4GiB). */
u32
trampoline_load_elf64(void)
{
	const u8 *base = (const u8 *)_binary_metal_elf_start;
	const Elf64_Ehdr32 *eh;
	const Elf64_Phdr32 *ph;
	u32 i;

	preserve_mb1();

	eh = (const Elf64_Ehdr32 *)base;
	if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E'
	    || eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F')
		return 0;
	if (eh->e_ident[4] != 2) /* ELFCLASS64 */
		return 0;
	if (eh->e_phoff_hi != 0)
		return 0;

	ph = (const Elf64_Phdr32 *)(base + eh->e_phoff_lo);
	for (i = 0; i < eh->e_phnum; i++) {
		const Elf64_Phdr32 *p = (const Elf64_Phdr32 *)((const u8 *)ph
							     + i * eh->e_phentsize);
		u8 *dest;
		u32 filesz;
		u32 memsz;

		if (p->p_type != 1) /* PT_LOAD */
			continue;
		if (p->p_paddr_hi != 0 || p->p_offset_hi != 0)
			return 0;
		dest = (u8 *)phys_ptr(p->p_paddr_lo);
		filesz = p->p_filesz_lo;
		memsz = p->p_memsz_lo;
		cpy(dest, base + p->p_offset_lo, filesz);
		if (memsz > filesz)
			zero(dest + filesz, memsz - filesz);
	}

	if (eh->e_entry_hi != 0)
		return 0;
	/* Prefer 64-bit entry from .bootinfo at Metal paddr ('METL' + ptr). */
	{
		u32 *bi = (u32 *)phys_ptr(0x400000u);
		if (bi[0] == 0x4C54454D) {
			/* Fill Multiboot1 handoff slots for entry64. */
			bi[4] = mb_magic; /* metal_boot_magic */
			bi[5] = mb_info;  /* metal_boot_info */
			return bi[2]; /* low 32 bits of entry64 */
		}
	}
	return eh->e_entry_lo;
}
