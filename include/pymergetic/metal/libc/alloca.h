/* Freestanding alloca — Metal libc. */
#ifndef PM_METAL_LIBC_ALLOCA_H_
#define PM_METAL_LIBC_ALLOCA_H_

#define alloca(n) __builtin_alloca(n)

#endif /* PM_METAL_LIBC_ALLOCA_H_ */
