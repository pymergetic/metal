/* Freestanding stub for WAMR under EDK2. */
#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	int _unused;
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#ifndef EOF
#define EOF (-1)
#endif

int printf(const char *fmt, ...);
int fputs(const char *s, FILE *f);
int fprintf(FILE *f, const char *fmt, ...);
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int snprintf(char *buf, size_t n, const char *fmt, ...);
int fflush(FILE *f);
FILE *fopen(const char *path, const char *mode);
/* POSIX.1-2008 — declared so wasm guests (doom) parse under clangd -I stubs. */
FILE *fmemopen(void *buf, size_t size, const char *mode);
int fclose(FILE *f);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f);
int fseek(FILE *f, long offset, int whence);
long ftell(FILE *f);
int sprintf(char *buf, const char *fmt, ...);
int sscanf(const char *str, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* _STDIO_H */
