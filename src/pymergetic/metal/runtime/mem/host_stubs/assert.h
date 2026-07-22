#ifndef _ASSERT_H
#define _ASSERT_H

#ifdef __cplusplus
extern "C" {
#endif

void assert(int cond);

#if !defined(static_assert)                                       \
    && ((defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L) \
        || (defined(__GNUC__) && __GNUC__ * 100 + __GNUC_MINOR__ >= 406))
#define static_assert _Static_assert
#endif

#ifdef __cplusplus
}
#endif

#endif /* _ASSERT_H */
