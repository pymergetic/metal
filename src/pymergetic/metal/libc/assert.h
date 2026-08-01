#ifndef PM_METAL_LIBC_ASSERT_H_
#define PM_METAL_LIBC_ASSERT_H_

/* Default no-op; callers (e.g. tlsf _port) may #undef and remap. */
#ifndef assert
#define assert(expr) ((void)0)
#endif

#ifndef static_assert
#define static_assert _Static_assert
#endif

#endif /* PM_METAL_LIBC_ASSERT_H_ */
