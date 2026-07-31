/** @file Freestanding float helpers WAMR + µPy libm do not cover.
 *
 * - copysignf: µPy math.c only defines it when !NDEBUG (RELEASE builds omit it)
 * - isinf / nanf / isfinite: needed by formatfloat / modmath; not in WAMR
 */
#include <stdint.h>

#include "math.h"

float copysignf(float x, float y)
{
  union {
    float    f;
    uint32_t i;
  } fx, fy;

  fx.f = x;
  fy.f = y;
  fx.i = (fx.i & 0x7fffffffu) | (fy.i & 0x80000000u);
  return fx.f;
}

double copysign(double x, double y)
{
  union {
    double   d;
    uint64_t i;
  } dx, dy;

  dx.d = x;
  dy.d = y;
  dx.i = (dx.i & 0x7fffffffffffffffull) | (dy.i & 0x8000000000000000ull);
  return dx.d;
}

int isinf(double x)
{
  union {
    double   d;
    uint64_t i;
  } u;

  u.d = x;
  return ((u.i & 0x7fffffffffffffffull) == 0x7ff0000000000000ull);
}

int isfinite(double x)
{
  return !isnan(x) && !isinf(x);
}

float nanf(const char *tagp)
{
  (void)tagp;
  return (float)NAN;
}
