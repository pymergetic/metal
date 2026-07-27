/*
 * EFI / freestanding WAMR platform — vmcore APIs.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "platform_internal.h"
#include "platform_api_vmcore.h"
#include "platform_api_extension.h"
#include "efi_wamr_internal.h"

#include <pymergetic/metal/runtime/mem/mem.h>
#include <runtime/stack/stack.h>
#include <runtime/time/time.h>

#include <stdarg.h>
#include <stdio.h>

int bh_platform_init(void)
{
  return BHT_OK;
}

void bh_platform_destroy(void) {}

void *os_malloc(unsigned size)
{
  return pm_metal_mem_alloc((size_t)size, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
}

void *os_realloc(void *ptr, unsigned size)
{
  return pm_metal_mem_realloc(ptr, (size_t)size);
}

void os_free(void *ptr)
{
  pm_metal_mem_free(ptr);
}

int os_dumps_proc_mem_info(char *out, unsigned int size)
{
  if (out == NULL || size == 0)
    return BHT_ERROR;
  out[0] = '\0';
  return BHT_ERROR;
}

int os_printf(const char *format, ...)
{
  va_list ap;
  int     ret;

  va_start(ap, format);
  ret = os_vprintf(format, ap);
  va_end(ap);
  return ret;
}

int os_vprintf(const char *format, va_list ap)
{
  char buf[512];
  int  n;

  if (format == NULL)
    return 0;

  /* C %s semantics (see mem/libc.c vsnprintf) — not EDK2 vsnprintf. */
  n = vsnprintf(buf, sizeof(buf), format, ap);
  if (n < 0)
    return n;
  if ((uintptr_t)n >= sizeof(buf))
    n = (int)sizeof(buf) - 1;
  buf[n] = '\0';

  /*
     * Post-EBS ConOut/Print is unusable. feed_stdout(also_serial=1) logs via
     * UART when owned; pre-EBS still uses Print inside emit_line.
     */
  efi_wamr_feed_stdout((const char *)buf, (size_t)n, 1);
  return n;
}

uint64 os_time_get_boot_us(void)
{
  return (uint64)pm_metal_time_mono_us();
}

uint64 os_time_thread_cputime_us(void)
{
  return os_time_get_boot_us();
}

korp_tid os_self_thread(void)
{
  return (korp_tid)1;
}

uint8 *os_thread_get_stack_boundary(void)
{
  /*
   * WAMR calls this on the stack it wants bounded (fresh every AOT/interp
   * call, see aot_call_function/wasm_runtime.c) -- never cache the result.
   * Returning NULL (the old behavior) silently disabled WAMR's native
   * stack overflow guard on every runner, so a deep guest recursion (e.g.
   * doom's BSP walk under the AOT backend, which uses real native call
   * frames unlike the interpreter) corrupted whatever heap memory sat
   * below the runner's stack instead of taking a clean trap -- surfacing
   * later as an unrelated-looking #GP.
   */
  return (uint8 *)pm_metal_stack_base(pm_metal_mem_cpu());
}

void os_thread_jit_write_protect_np(bool enabled)
{
  (void)enabled;
}

int os_mutex_init(korp_mutex *mutex)
{
  if (mutex == NULL)
    return BHT_ERROR;
  pm_metal_spin_init(mutex);
  return BHT_OK;
}

int os_mutex_destroy(korp_mutex *mutex)
{
  (void)mutex;
  return BHT_OK;
}

int os_mutex_lock(korp_mutex *mutex)
{
  if (mutex == NULL)
    return BHT_ERROR;
  pm_metal_spin_lock(mutex);
  return BHT_OK;
}

int os_mutex_unlock(korp_mutex *mutex)
{
  if (mutex == NULL)
    return BHT_ERROR;
  pm_metal_spin_unlock(mutex);
  return BHT_OK;
}

void *os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file)
{
  uint8_t  *raw;
  uintptr_t aligned;
  size_t    total;

  (void)hint;
  (void)prot;
  (void)flags;
  (void)file;

  if (size == 0 || size >= UINT32_MAX)
    return NULL;

  /*
   * The AOT loader treats this like real POSIX mmap(2): it lays out
   * .rodata.cst16 etc. at page-aligned offsets from the returned base,
   * then loads 16-byte SSE constants out of that buffer with *aligned*
   * instructions (movaps) that #GP -- not #PF -- on a misaligned address.
   * A plain heap allocation only guarantees a small fixed alignment, and
   * an unlucky TLSF layout (far likelier once multiple APs allocate
   * concurrently under -smp>1) handed back a base that wasn't 16-byte
   * aligned, so this surfaced as a "random" #GP deep in unrelated-looking
   * AOT code. Over-allocate and round up to a page boundary instead, and
   * stash the real heap pointer just before the aligned one so os_munmap
   * can free it back regardless of allocation/free order.
   */
  total = size + PM_METAL_MEM_PAGE_SIZE + sizeof(void *);
  raw   = (uint8_t *)pm_metal_mem_alloc(total, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (raw == NULL)
    return NULL;

  aligned = ((uintptr_t)raw + sizeof(void *) + PM_METAL_MEM_PAGE_SIZE - 1)
            & ~(uintptr_t)(PM_METAL_MEM_PAGE_SIZE - 1);
  ((void **)aligned)[-1] = raw;

  memset((void *)aligned, 0, size);
  return (void *)aligned;
}

void *os_mremap(void *old_addr, size_t old_size, size_t new_size)
{
  return os_mremap_slow(old_addr, old_size, new_size);
}

void os_munmap(void *addr, size_t size)
{
  (void)size;
  if (addr == NULL)
    return;
  pm_metal_mem_free(((void **)addr)[-1]);
}

int os_mprotect(void *addr, size_t size, int prot)
{
  (void)addr;
  (void)size;
  (void)prot;
  return 0;
}

void os_dcache_flush(void) {}

void os_icache_flush(void *start, size_t len)
{
  (void)start;
  (void)len;
}

os_raw_file_handle os_invalid_raw_handle(void)
{
  return -1;
}

__wasi_errno_t os_clock_res_get(__wasi_clockid_t clock_id, __wasi_timestamp_t *resolution)
{
  if (resolution == NULL)
    return __WASI_EINVAL;

  switch (clock_id) {
  case __WASI_CLOCK_REALTIME:
  case __WASI_CLOCK_MONOTONIC:
  case __WASI_CLOCK_PROCESS_CPUTIME_ID:
  case __WASI_CLOCK_THREAD_CPUTIME_ID:
    /* TSC-based mono clock; ~1 us resolution after calibration */
    *resolution = 1000; /* 1 microsecond in nanoseconds */
    return __WASI_ESUCCESS;
  default:
    return __WASI_EINVAL;
  }
}

__wasi_errno_t os_clock_time_get(__wasi_clockid_t    clock_id,
                                 __wasi_timestamp_t  precision,
                                 __wasi_timestamp_t *time)
{
  (void)precision;

  if (time == NULL)
    return __WASI_EINVAL;

  switch (clock_id) {
  case __WASI_CLOCK_REALTIME:
  case __WASI_CLOCK_MONOTONIC:
  case __WASI_CLOCK_PROCESS_CPUTIME_ID:
  case __WASI_CLOCK_THREAD_CPUTIME_ID:
    *time = (__wasi_timestamp_t)pm_metal_time_mono_us() * 1000ull;
    return __WASI_ESUCCESS;
  default:
    return __WASI_EINVAL;
  }
}
