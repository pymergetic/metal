#ifndef _ASSERT_H
#define _ASSERT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Implementation in libc_wamr.c — keep a real function for the linker. */
__attribute__((noreturn)) void pm_metal_assert_fail(void);

/*
 * Expression-friendly (µPy uses assert() inside comma-exprs in obj.h).
 * Accepts pointers/scalars like ISO assert.
 */
#define assert(cond) ((void)((cond) ? 0 : (pm_metal_assert_fail(), 0)))

#if !defined(static_assert) && ((defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L) || \
                                (defined(__GNUC__) && __GNUC__ * 100 + __GNUC_MINOR__ >= 406))
#define static_assert _Static_assert
#endif

#ifdef __cplusplus
}
#endif

#endif /* _ASSERT_H */
