#ifndef PM_METAL_FW_SYS_TIME_H
#define PM_METAL_FW_SYS_TIME_H

/* sys/time.h for the firmware seats. tcc.h includes <sys/time.h>
 * unconditionally on non-_WIN32 hosts; without this shadow the SYSTEM
 * header wins and its sys/select.h redefines struct timespec against
 * fwinc/time.h (compile error, verified). This shadow carries only what
 * the compiled TCC set parses: the timeval shape. struct timespec stays
 * owned by fwinc/time.h — one definition, no glibc leak. */

struct timeval {
    long tv_sec;
    long tv_usec;
};

#endif
