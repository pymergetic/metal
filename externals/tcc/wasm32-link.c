#ifdef TARGET_DEFS_ONLY

/* WASM has no ELF relocations — stub the types tccgen/tccelf expect. */
#define EM_TCC_TARGET  0
#define R_DATA_PTR     1
#define R_DATA_8       2
#define R_DATA_16      3
#define R_DATA_32      4
#define R_DATA_64      5
#define R_JMP_SLOT     6
#define R_GOTPCREL     7
#define R_COPY         8
#define R_RELATIVE     9
#define R_IRELATIVE    10
#define R_GLOB_DAT     11
#define R_NUM          12

#define ELF_START_ADDR  0x1000
#define ELF_PAGE_SIZE   0x1000
#define PCRELATIVE_DLLPLT 0
#define RELOCATE_DLLPLT   0

#else

/* Stub ELF relocation functions — WASM generates no ELF output. */
ST_FUNC int code_reloc(int reloc_type) { (void)reloc_type; return 0; }
ST_FUNC int gotplt_entry_type(int reloc_type) { (void)reloc_type; return 0; }
ST_FUNC unsigned create_plt_entry(TCCState *s1, unsigned got_offset, struct sym_attr *attr) { (void)s1; (void)got_offset; (void)attr; return 0; }
ST_FUNC void relocate(TCCState *s1, ElfW_Rel *rel, int type, unsigned char *ptr, addr_t addr, addr_t val) { (void)s1; (void)rel; (void)type; (void)ptr; (void)addr; (void)val; }
ST_FUNC void relocate_plt(TCCState *s1) { (void)s1; }

#endif
