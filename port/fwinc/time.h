#ifndef PM_METAL_FW_TIME_H
#define PM_METAL_FW_TIME_H

typedef long time_t;
typedef long clock_t;

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

#endif
