#ifndef PM_METAL_FW_STDIO_H
#define PM_METAL_FW_STDIO_H

#include <stddef.h>
#include <stdarg.h>

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

/* Freestanding stdio has no buffering: stdout/stderr are null stream handles
 * and fflush is a no-op (the zenoh-pico vendored core emits a stray
 * fflush(stdout) in its keyexpr-append helper). The embedded TCC references
 * stderr on its -run half (tccrun.c) and its diagnostics — the stream is
 * ignored by every writer below, see port/lib.c. */
#define stdout ((void *)0)
#define stderr ((void *)0)
#define stdin  ((void *)0)
void fflush(void *stream);

int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int sscanf(const char *str, const char *fmt, ...);
int snprintf(char *str, size_t size, const char *fmt, ...);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);
int putchar(int c);
int puts(const char *s);

/* The printf family the vendored TCC links beyond the µPy core's set
 * (shared/libc/printf.c provides printf/vprintf/putchar/puts/snprintf/
 * vsnprintf; these four are implemented in port/lib.c over the core's
 * vsnprintf + the console card). */
int sprintf(char *str, const char *fmt, ...);
int fprintf(void *stream, const char *fmt, ...);
int vfprintf(void *stream, const char *fmt, va_list ap);
int fputs(const char *s, void *stream);

/*---- arena-backed temp FILE layer (the in-kernel compile path) --------------
 * The firmware seats have no filesystem, but the embedded TCC writes its
 * object files through FILE* (fdopen/fwrite/fputc/fclose in
 * tcc_write_elf_file) and jit.c reads them back with fopen/fread. This layer
 * serves exactly those calls, backed by one growable arena buffer per
 * stream. All bytes come from the boot arena (pm_metal_coop_arena), so
 * they die with the arena — this is compile scratch, not a general fs.
 *
 * open() hands out a small integer fd into the layer's table;
 * fdopen(fd,"wb")/fopen(path,"rb") wrap that fd in a FILE. The only paths
 * that exist are the ones the callers invent (mkstemp-style names from
 * jit.c, tcc_write_elf_file's output name) — nothing enumerates. */
typedef struct pm_metal_fw_FILE {
    int fd;              /* owning temp fd (-1: closed/invalid) */
    long pos;            /* byte cursor (read or write) */
} FILE;

FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
int fclose(FILE *f);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f);
int fputc(int c, FILE *f);
int fgetc(FILE *f);
long ftell(FILE *f);
int fseek(FILE *f, long offset, int whence);
void rewind(FILE *f);

/* freopen backs the embedded TCC's -run machinery (tccrun.c rebinds stdin
 * for the compiled guest). No firmware seat drives -run — the library embed
 * compiles objects only — so the port layer answers it with the temp layer
 * when the path exists, NULL otherwise (the caller refuses, per POSIX). */
FILE *freopen(const char *path, const char *mode, FILE *stream);

#endif
