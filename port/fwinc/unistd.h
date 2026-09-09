#ifndef PM_METAL_FW_UNISTD_H
#define PM_METAL_FW_UNISTD_H

#include <stddef.h>

typedef long ssize_t;

/* unlink for the arena-backed temp-file layer (TCC's
 * tcc_write_elf_file unlinks the output name first — no-op semantics
 * here, the layer's name table handles overwrite). read/write/lseek
 * serve the fd face the temp layer hands out; see fwinc/stdio.h. */
int unlink(const char *path);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
long lseek(int fd, long offset, int whence);

/* The embedded TCC's remaining unistd surface: getcwd feeds the debug-info
 * and coverage paths' relative-path prefix (a firmware compile has no cwd —
 * the empty string keeps names as-written); environ backs the -run machinery
 * (envp is handed to the compiled guest's main and never dereferenced by
 * the kernel — one terminating NULL entry). Both live in port/lib.c. */
char *getcwd(char *buf, size_t size);
extern char **environ;

#endif
