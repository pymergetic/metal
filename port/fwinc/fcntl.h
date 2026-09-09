#ifndef PM_METAL_FW_FCNTL_H
#define PM_METAL_FW_FCNTL_H

/* fcntl.h for the firmware seats: only the open() flags TCC's I/O layer
 * uses (libtcc.c _tcc_open: O_RDONLY|O_BINARY; tccelf.c
 * tcc_write_elf_file: O_WRONLY|O_CREAT|O_TRUNC|O_BINARY). Everything
 * routes to the arena-backed temp-file layer in port/lib.c — see
 * fwinc/stdio.h for the contract. */
#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef O_RDONLY
#define O_RDONLY 0
#endif
#ifndef O_WRONLY
#define O_WRONLY 1
#endif
#ifndef O_CREAT
#define O_CREAT 0100
#endif
#ifndef O_TRUNC
#define O_TRUNC 01000
#endif

/* open is variadic like POSIX (TCC calls the 2-arg form; mode is ignored by
 * the arena-backed temp layer). */
int open(const char *path, int flags, ...);
int close(int fd);

#endif
