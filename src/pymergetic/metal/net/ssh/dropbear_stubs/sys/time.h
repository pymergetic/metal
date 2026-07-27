/* Freestanding Dropbear sys/time — claim glibc guards; relative types include. */
#ifndef _METAL_DB_SYS_TIME_H
#define _METAL_DB_SYS_TIME_H

#ifndef _SYS_TIME_H
#define _SYS_TIME_H 1
#endif

#include "types.h"

#ifndef __timeval_defined
#define __timeval_defined 1
#endif
#ifndef __struct_timeval_defined
#define __struct_timeval_defined 1
#endif

struct timeval {
  long        tv_sec;
  suseconds_t tv_usec;
};

struct timezone {
  int tz_minuteswest;
  int tz_dsttime;
};

int gettimeofday(struct timeval *tv, void *tz);

#endif
