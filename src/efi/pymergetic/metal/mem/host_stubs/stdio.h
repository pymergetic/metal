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
int fprintf(FILE *f, const char *fmt, ...);
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int snprintf(char *buf, size_t n, const char *fmt, ...);
int fflush(FILE *f);

#ifdef __cplusplus
}
#endif

#endif /* _STDIO_H */
