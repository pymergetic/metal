#ifndef PM_METAL_FW_STDIO_H
#define PM_METAL_FW_STDIO_H

#include <stddef.h>
#include <stdarg.h>

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

/* Freestanding stdio has no FILE or buffering: stdout is a null stream handle and
 * fflush is a no-op (the zenoh-pico vendored core emits a stray fflush(stdout) in
 * its keyexpr-append helper). */
#define stdout ((void *)0)
void fflush(void *stream);

int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int sscanf(const char *str, const char *fmt, ...);
int snprintf(char *str, size_t size, const char *fmt, ...);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);
int putchar(int c);
int puts(const char *s);

#endif
