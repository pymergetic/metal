/*
 * Minimal <intrin.h> for clang --target=x86_64-unknown-windows freestanding.
 * MicroPython py/misc.h includes this under _MSC_VER for CLZ/CTZ/popcount.
 */
#ifndef _PM_METAL_LIBC_INTRIN_H
#define _PM_METAL_LIBC_INTRIN_H

#ifdef __cplusplus
extern "C" {
#endif

static inline unsigned char _BitScanReverse(unsigned long *index, unsigned long mask)
{
	if (mask == 0) {
		return 0;
	}
	*index = (unsigned long)(31 - __builtin_clzl(mask));
	return 1;
}

static inline unsigned char _BitScanReverse64(unsigned long *index, unsigned long long mask)
{
	if (mask == 0) {
		return 0;
	}
	*index = (unsigned long)(63 - __builtin_clzll(mask));
	return 1;
}

static inline unsigned char _BitScanForward(unsigned long *index, unsigned long mask)
{
	if (mask == 0) {
		return 0;
	}
	*index = (unsigned long)__builtin_ctzl(mask);
	return 1;
}

static inline unsigned int __popcnt(unsigned int v)
{
	return (unsigned int)__builtin_popcount(v);
}

#ifdef __cplusplus
}
#endif

#endif /* _PM_METAL_LIBC_INTRIN_H */
