#ifndef PM_METAL_FW_TIME_H
#define PM_METAL_FW_TIME_H

typedef long time_t;
typedef long clock_t;

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

/* struct tm + localtime for the vendored TCC (tccpp.c's __DATE__/__TIME__
 * builtins). There is no RTC read on this path: the one static broken-down
 * time serves every call, so those builtins expand to the epoch — a
 * documented fill difference, not a stub the proves can't see. */
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

time_t time(time_t *t);
struct tm *localtime(const time_t *timep);

#endif
