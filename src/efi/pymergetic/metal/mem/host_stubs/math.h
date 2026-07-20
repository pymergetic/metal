/*
 * Freestanding stub — declarations for WAMR common/math (math.c).
 * Implementations come from external/wamr/.../common/math/math.c.
 */
#ifndef _MATH_H
#define _MATH_H

#ifdef __cplusplus
extern "C" {
#endif

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

float sqrtf(float x);
float floorf(float x);
float ceilf(float x);
float fminf(float x, float y);
float fmaxf(float x, float y);
float rintf(float x);
float fabsf(float x);
float truncf(float x);

int signbit(double x);
int isnan(double x);

#ifndef NAN
#define NAN (0.0 / 0.0)
#endif

#ifndef INFINITY
#define INFINITY (1.0 / 0.0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* _MATH_H */
