#ifndef PM_METAL_LIBC_STDIO_H_
#define PM_METAL_LIBC_STDIO_H_

#include <stdarg.h>
#include <stddef.h>

int printf(const char *fmt, ...);
int snprintf(char *dst, size_t dst_cap, const char *fmt, ...);
int vsnprintf(char *dst, size_t dst_cap, const char *fmt, va_list ap);

#endif /* PM_METAL_LIBC_STDIO_H_ */
