#ifndef PM_METAL_FW_MATH_H
#define PM_METAL_FW_MATH_H

static inline __attribute__((unused)) double log(double x) {
    (void)x;
    return 0.0;
}
static inline __attribute__((unused)) double exp(double x) {
    (void)x;
    return 0.0;
}
static inline __attribute__((unused)) double sqrt(double x) {
    (void)x;
    return 0.0;
}
static inline __attribute__((unused)) double cos(double x) {
    (void)x;
    return 0.0;
}
static inline __attribute__((unused)) double sin(double x) {
    (void)x;
    return 0.0;
}
static inline __attribute__((unused)) double tan(double x) {
    (void)x;
    return 0.0;
}
static inline __attribute__((unused)) double pow(double x, double y) {
    (void)x;
    (void)y;
    return 0.0;
}
static inline __attribute__((unused)) double floor(double x) {
    return x;
}
static inline __attribute__((unused)) double ceil(double x) {
    return x;
}
static inline __attribute__((unused)) double fabs(double x) {
    return x < 0.0 ? -x : x;
}
static inline __attribute__((unused)) double fmin(double x, double y) {
    return x < y ? x : y;
}
static inline __attribute__((unused)) double fmax(double x, double y) {
    return x > y ? x : y;
}
static inline __attribute__((unused)) double trunc(double x) {
    return x;
}
static inline __attribute__((unused)) double rint(double x) {
    return x;
}
static inline __attribute__((unused)) float sqrtf(float x) {
    (void)x;
    return 0.0f;
}
static inline __attribute__((unused)) float floorf(float x) {
    return x;
}
static inline __attribute__((unused)) float ceilf(float x) {
    return x;
}
static inline __attribute__((unused)) float fabsf(float x) {
    return x < 0.0f ? -x : x;
}
static inline __attribute__((unused)) float fminf(float x, float y) {
    return x < y ? x : y;
}
static inline __attribute__((unused)) float fmaxf(float x, float y) {
    return x > y ? x : y;
}
static inline __attribute__((unused)) float truncf(float x) {
    return x;
}
static inline __attribute__((unused)) float rintf(float x) {
    return x;
}
#ifndef isnan
#define isnan(x) ((x) != (x))
#endif
#ifndef isinf
#define isinf(x) ((x) != 0 && (x) + (x) == (x))
#endif
#ifndef signbit
#define signbit(x) ((x) < 0)
#endif

#endif
