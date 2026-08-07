#ifndef PM_METAL_LIBC_MATH_H_
#define PM_METAL_LIBC_MATH_H_
double floor(double x);
double ceil(double x);
double fabs(double x);
double pow(double x, double y);
double exp(double x);
double log(double x);
double sqrt(double x);
double cos(double x);
double sin(double x);
double tan(double x);
double acos(double x);
double asin(double x);
double atan(double x);
double atan2(double y, double x);
double fmod(double x, double y);
double trunc(double x);
double round(double x);
#ifndef NAN
#define NAN __builtin_nan("")
#endif
#ifndef INFINITY
#define INFINITY __builtin_inf()
#endif
#endif
