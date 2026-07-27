/*
 * Freestanding EFI platform for WAMR (BH_PLATFORM_METAL_EFI).
 * No foreign RTOS headers — EDK2 + host_stubs only.
 */
#ifndef _PLATFORM_INTERNAL_H
#define _PLATFORM_INTERNAL_H

#ifndef BH_PLATFORM_METAL_EFI
#define BH_PLATFORM_METAL_EFI
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <limits.h>
/* Re-exported for WAMR TUs that expect libc via platform_internal.h. */
#include "../../runtime/mem/host_stubs/errno.h"     /* IWYU pragma: export */
#include "../../runtime/mem/host_stubs/ctype.h"     /* IWYU pragma: export */
#include "../../runtime/mem/host_stubs/string.h"    /* IWYU pragma: export */
#include "../../runtime/mem/host_stubs/arpa/inet.h" /* IWYU pragma: export */
#include "../../runtime/mem/host_stubs/stdlib.h"    /* IWYU pragma: export */
#include "../../runtime/mem/host_stubs/stdio.h"     /* IWYU pragma: export */
#include "../../runtime/mem/host_stubs/math.h"      /* IWYU pragma: export */
#include "../../runtime/mem/host_stubs/time.h"      /* IWYU pragma: export */
#include "../../runtime/mem/host_stubs/poll.h"      /* IWYU pragma: export */
#include "../../runtime/mem/host_stubs/pthread.h"   /* IWYU pragma: export */
#include "../../runtime/mem/host_stubs/assert.h"    /* IWYU pragma: export */
#include "../../runtime/mem/host_stubs/sys/ioctl.h" /* IWYU pragma: export */
#include "../../runtime/slot/spin.h"

#ifndef FIONREAD
#define FIONREAD 0x541B
#endif

#define BH_APPLET_PRESERVED_STACK_SIZE (2 * BH_KB)

/* Default thread priority (unused; single-thread stubs) */
#define BH_THREAD_DEFAULT_PRIORITY 7

/* Single-thread stub types */
typedef int             korp_tid;
/* Real cross-AP lock (StartupAllAPs runs genuine parallel cores) -- WAMR's
 * shared globals (loading_module_list_lock, registered_module_list_lock,
 * etc.) rely on os_mutex_* actually excluding, not the single-thread stub
 * it looks like at a glance. */
typedef pm_metal_spin_t korp_mutex;
typedef unsigned int    korp_sem;
typedef int             korp_thread;
typedef pthread_cond_t  korp_cond;

typedef struct {
  int dummy;
} korp_rwlock;

/* WASI stdio handles: 0=stdin, 1=stdout, 2=stderr */
typedef int   os_file_handle;
typedef void *os_dir_stream;
typedef int   os_raw_file_handle;

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

static inline os_file_handle os_get_invalid_handle(void)
{
  return -1;
}

static inline int os_getpagesize(void)
{
  return 4096;
}

#endif /* _PLATFORM_INTERNAL_H */
