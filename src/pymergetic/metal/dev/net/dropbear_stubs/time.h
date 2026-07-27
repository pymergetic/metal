#ifndef _METAL_DB_TIME_H
#define _METAL_DB_TIME_H
/* Claim the usual libc guard so host_stubs/time.h cannot replace us later. */
#ifndef _TIME_H
#define _TIME_H
#endif
#include <stdint.h>
typedef int64_t time_t;
typedef int clockid_t;
typedef long clock_t;
struct timespec { time_t tv_sec; long tv_nsec; };
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; };
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
time_t time(time_t *t);
struct tm *gmtime(const time_t *timer);
struct tm *localtime(const time_t *timer);
clock_t clock(void);
int clock_gettime(clockid_t clk_id, struct timespec *tp);
int nanosleep(const struct timespec *req, struct timespec *rem);
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm);
#endif
