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

int clock_gettime(clockid_t clock_id, struct timespec *tp);

#ifdef __cplusplus
}
#endif

#endif /* _TIME_H */
