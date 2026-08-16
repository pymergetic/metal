#ifndef PM_METAL_FW_STDIO_H
#define PM_METAL_FW_STDIO_H

#include <stddef.h>
#include <stdarg.h>

int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int sscanf(const char *str, const char *fmt, ...);
int snprintf(char *str, size_t size, const char *fmt, ...);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);
int putchar(int c);
int puts(const char *s);

#endif
