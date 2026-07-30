/*
 * metal/libc — freestanding ISO C headers for firmware -nostdinc builds.
 * Internal: not a guest surface.
 */
#ifndef PM_METAL_LIBC_STDDEF_H_
#define PM_METAL_LIBC_STDDEF_H_

/* Prefer compiler builtins so LLP64 (UEFI/Windows COFF) and LP64 (ELF) both match. */
#ifdef __SIZE_TYPE__
typedef __SIZE_TYPE__ size_t;
#else
typedef unsigned long size_t;
#endif
#ifdef __PTRDIFF_TYPE__
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#else
typedef long ptrdiff_t;
#endif
#define NULL ((void *)0)
#define offsetof(type, member) ((size_t)&(((type *)0)->member))

#endif /* PM_METAL_LIBC_STDDEF_H_ */
