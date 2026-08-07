#ifndef PM_METAL_LIBC_STDARG_H_
#define PM_METAL_LIBC_STDARG_H_

typedef __builtin_va_list va_list;

#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)         __builtin_va_end(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_copy(dst, src)  __builtin_va_copy(dst, src)

#endif /* PM_METAL_LIBC_STDARG_H_ */
