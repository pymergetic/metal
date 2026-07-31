/*
 * Freestanding math.h — declarations for:
 *   - WAMR common/math (math.c): floor/sqrt/pow/…
 *   - MicroPython lib/libm: fmodf/powf/logf/…
 *   - Metal py_libm_extra.c: copysignf / isinf (always, even with NDEBUG)
 */
#ifndef _MATH_H
#define _MATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FLT_EVAL_METHOD
#define FLT_EVAL_METHOD 0
#endif

/* µPy libm (newlib-nano fdlibm) uses BSD names; freestanding has no sys/types.h. */
typedef int8_t   __int8_t;
typedef uint8_t  __uint8_t;
typedef int16_t  __int16_t;
typedef uint16_t __uint16_t;
typedef int32_t  __int32_t;
typedef uint32_t __uint32_t;

double sqrt(double x);
double floor(double x);
double ceil(double x);
double fmin(double x, double y);
double fmax(double x, double y);
double rint(double x);
double fabs(double x);
double trunc(double x);
double atan(double x);
double atan2(double y, double x);
double pow(double x, double y);
double scalbn(double x, int n);
float  scalbnf(float x, int n);
double fmod(double x, double y);
double copysign(double x, double y);
double log(double x);
double log10(double x);
double exp(double x);

float sqrtf(float x);
float floorf(float x);
float ceilf(float x);
float fminf(float x, float y);
float fmaxf(float x, float y);
float rintf(float x);
float fabsf(float x);
float truncf(float x);
float fmodf(float x, float y);
float copysignf(float x, float y);
float powf(float x, float y);
float logf(float x);
float log10f(float x);
float expf(float x);
float expm1f(float x);
float nearbyintf(float x);
float roundf(float x);
float nanf(const char *tagp);
float log1pf(float x);
float acoshf(float x);
float asinhf(float x);
float atanhf(float x);
float sinf(float x);
float cosf(float x);
float tanf(float x);
float asinf(float x);
float acosf(float x);
float atanf(float x);
float atan2f(float y, float x);
float sinhf(float x);
float coshf(float x);
float tanhf(float x);
float frexpf(float x, int *exp);
float ldexpf(float x, int exp);
float modff(float x, float *iptr);
float erff(float x);
float erfcf(float x);
float tgammaf(float x);
float lgammaf(float x);

int signbit(double x);
int isnan(double x);
int isinf(double x);
int isfinite(double x);

#ifndef NAN
#define NAN (0.0 / 0.0)
#endif

#ifndef INFINITY
#define INFINITY (1.0 / 0.0)
#endif

/* fpclassify return codes (C99) — MicroPython lib/libm uses these. */
#ifndef FP_NAN
#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4
#endif

#ifdef __cplusplus
}
#endif

#endif /* _MATH_H */
