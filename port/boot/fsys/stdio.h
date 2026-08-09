#ifndef METAL_FREESTANDING_STDIO_H
#define METAL_FREESTANDING_STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef struct {
    int unused;
} FILE;

extern FILE *stdout;
extern FILE *stderr;
extern FILE *stdin;

int printf(const char *fmt, ...);
int snprintf(char *buf, size_t n, const char *fmt, ...);
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int putchar(int c);
int puts(const char *s);

#endif
