#ifndef PM_METAL_FW_SYS_MMAN_H
#define PM_METAL_FW_SYS_MMAN_H

/* sys/mman.h for the firmware seats. tccrun.c (compiled on every seat whose
 * backend is native — BIOS x86_64, RV1106 armv7) includes <sys/mman.h>
 * unconditionally outside _WIN32 and calls mprotect from tcc_relocate's
 * page-protection pass. Without this shadow the SYSTEM header wins and the
 * freestanding build silently depends on glibc layouts. The constants are
 * the POSIX values; the port layer implements mprotect as a no-op (arena
 * memory — see port/lib.c) so the shape here only has to parse and link. */

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANON      0x20
#define MAP_ANONYMOUS MAP_ANON
#define MAP_FAILED    ((void *)-1)

int mprotect(void *addr, unsigned long len, int prot);
void *mmap(void *addr, unsigned long len, int prot, int flags, int fd, long off);
int munmap(void *addr, unsigned long len);

#endif
