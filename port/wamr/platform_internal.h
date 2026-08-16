#ifndef _PLATFORM_INTERNAL_H
#define _PLATFORM_INTERNAL_H

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef BH_PLATFORM_METAL
#define BH_PLATFORM_METAL
#endif

#define BH_APPLET_PRESERVED_STACK_SIZE (2 * BH_KB)
#define BH_THREAD_DEFAULT_PRIORITY 0

typedef int korp_tid;
typedef int korp_thread;
typedef int korp_mutex;
typedef int korp_sem;
typedef struct {
    int dummy;
} korp_rwlock;
typedef struct {
    int dummy;
} korp_cond;

#define os_printf pm_metal_wamr_printf
#define os_vprintf pm_metal_wamr_vprintf

int pm_metal_wamr_printf(const char *fmt, ...);
int pm_metal_wamr_vprintf(const char *fmt, va_list ap);

typedef int os_file_handle;
typedef void *os_dir_stream;
typedef int os_raw_file_handle;
typedef int os_poll_file_handle;
typedef unsigned int os_nfds_t;
typedef int os_timespec;

os_file_handle os_get_invalid_handle(void);
int os_getpagesize(void);

#endif
