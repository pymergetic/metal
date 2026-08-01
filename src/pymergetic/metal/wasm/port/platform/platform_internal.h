/*
 * Freestanding Metal platform for WAMR (BH_PLATFORM_METAL).
 * Interp + libc-builtin only — no WASI / pthread.
 */
#ifndef _PLATFORM_INTERNAL_H
#define _PLATFORM_INTERNAL_H

#ifndef BH_PLATFORM_METAL
#define BH_PLATFORM_METAL
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <limits.h>

#include "errno.h"
#include "ctype.h"
#include "math.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#define BH_APPLET_PRESERVED_STACK_SIZE (2 * BH_KB)

#define BH_THREAD_DEFAULT_PRIORITY 7

typedef int korp_tid;
typedef int korp_thread;
typedef unsigned int korp_sem;

/* Busy-wait mutex — AtomicU32 layout matches mem/lock Spin. */
typedef struct {
  volatile uint32_t state;
} korp_mutex;

typedef struct {
  int dummy;
} korp_cond;

typedef struct {
  int dummy;
} korp_rwlock;

typedef int os_file_handle;
typedef void *os_dir_stream;
typedef int os_raw_file_handle;

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

/* Skip WAMR clang stdatomic.h path (needs full stdint least/fast + wchar). */
#define os_memory_order_acquire __ATOMIC_ACQUIRE
#define os_memory_order_release __ATOMIC_RELEASE
#define os_memory_order_seq_cst __ATOMIC_SEQ_CST
#define os_atomic_thread_fence __atomic_thread_fence

#endif /* _PLATFORM_INTERNAL_H */
