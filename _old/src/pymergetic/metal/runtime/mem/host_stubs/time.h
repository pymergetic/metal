/* Freestanding stub for WAMR libc-wasi locking.h */
#ifndef _TIME_H
#define _TIME_H

/* Block glibc bits/types/struct_timespec.h if anything pulls it. */
#define __timespec_defined 1
#define _STRUCT_TIMESPEC

#include <stddef.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int  clockid_t;
typedef long time_t;
typedef long clock_t;

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

struct timespec {
  time_t tv_sec;
  long   tv_nsec;
};

struct tm {
  int tm_sec;
  int tm_min;
  int tm_hour;
  int tm_mday;
  int tm_mon;
  int tm_year;
  int tm_wday;
  int tm_yday;
  int tm_isdst;
};

#ifndef TIMER_ABSTIME
#define TIMER_ABSTIME 1
#endif

int        clock_gettime(clockid_t clock_id, struct timespec *tp);
int        clock_nanosleep(clockid_t              clock_id,
                           int                    flags,
                           const struct timespec *request,
                           struct timespec       *remain);
int        nanosleep(const struct timespec *req, struct timespec *rem);
int        sched_yield(void);
clock_t    clock(void);
time_t     time(time_t *t);
struct tm *gmtime(const time_t *timer);
struct tm *localtime(const time_t *timer);
size_t     strftime(char *s, size_t max, const char *format, const struct tm *tm);
clock_t    clock(void);
time_t     time(time_t *t);
struct tm *gmtime(const time_t *timer);
struct tm *localtime(const time_t *timer);
size_t     strftime(char *s, size_t max, const char *format, const struct tm *tm);

#ifdef __cplusplus
}
#endif

#endif /* _TIME_H */
