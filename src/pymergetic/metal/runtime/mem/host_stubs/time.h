/* Freestanding stub for WAMR libc-wasi locking.h */
#ifndef _TIME_H
#define _TIME_H

/* Block glibc bits/types/struct_timespec.h if anything pulls it. */
#define __timespec_defined 1
#define _STRUCT_TIMESPEC

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int clockid_t;
typedef long time_t;

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

struct timespec {
	time_t tv_sec;
	long tv_nsec;
};

#ifndef TIMER_ABSTIME
#define TIMER_ABSTIME 1
#endif

int clock_gettime(clockid_t clock_id, struct timespec *tp);
int clock_nanosleep(clockid_t clock_id, int flags,
		    const struct timespec *request, struct timespec *remain);
int nanosleep(const struct timespec *req, struct timespec *rem);
int sched_yield(void);

#ifdef __cplusplus
}
#endif

#endif /* _TIME_H */
