/* Metal freestanding WAMR platform — vmcore APIs (interp). */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#include "platform_internal.h"
#include "platform_api_vmcore.h"

#include "pymergetic/metal/async/time.h"
#include "pymergetic/metal/log.h"
#include "pymergetic/metal/mem.h"

#define PAGE_SIZE 4096u

int errno;

void os_munmap(void *addr, size_t size);

int bh_platform_init(void)
{
  return BHT_OK;
}

void bh_platform_destroy(void) {}

void *os_malloc(unsigned size)
{
  return (void *)pm_metal_mem_alloc((size_t)size);
}

void *os_realloc(void *ptr, unsigned size)
{
  return (void *)pm_metal_mem_realloc((uint8_t *)ptr, (size_t)size);
}

void os_free(void *ptr)
{
  pm_metal_mem_free((uint8_t *)ptr);
}

int os_dumps_proc_mem_info(char *out, unsigned int size)
{
  if (out == NULL || size == 0) {
    return BHT_ERROR;
  }
  out[0] = '\0';
  return BHT_ERROR;
}

int os_printf(const char *format, ...)
{
  va_list ap;
  int ret;

  va_start(ap, format);
  ret = os_vprintf(format, ap);
  va_end(ap);
  return ret;
}

int os_vprintf(const char *format, va_list ap)
{
  char buf[512];
  int n;

  if (format == NULL) {
    return 0;
  }
  n = vsnprintf(buf, sizeof(buf), format, ap);
  if (n < 0) {
    return n;
  }
  if ((size_t)n >= sizeof(buf)) {
    n = (int)sizeof(buf) - 1;
  }
  buf[n] = '\0';
  pm_metal_log((const uint8_t *)buf);
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
  /* NULL disables native stack overflow guard (interp; AOT would care more). */
  return NULL;
}

void os_thread_jit_write_protect_np(bool enabled)
{
  (void)enabled;
}

int os_mutex_init(korp_mutex *mutex)
{
  if (mutex == NULL) {
    return BHT_ERROR;
  }
  mutex->state = 0;
  return BHT_OK;
}

int os_mutex_destroy(korp_mutex *mutex)
{
  (void)mutex;
  return BHT_OK;
}

int os_mutex_lock(korp_mutex *mutex)
{
  if (mutex == NULL) {
    return BHT_ERROR;
  }
  while (__atomic_exchange_n(&mutex->state, 1u, __ATOMIC_ACQUIRE) != 0u) {
    /* busy wait */
  }
  return BHT_OK;
}

int os_mutex_unlock(korp_mutex *mutex)
{
  if (mutex == NULL) {
    return BHT_ERROR;
  }
  __atomic_store_n(&mutex->state, 0u, __ATOMIC_RELEASE);
  return BHT_OK;
}

void *os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file)
{
  uint8_t *raw;
  uintptr_t aligned;
  size_t total;

  (void)hint;
  (void)prot;
  (void)flags;
  (void)file;

  if (size == 0 || size >= (size_t)UINT32_MAX) {
    return NULL;
  }

  total = size + PAGE_SIZE + sizeof(void *);
  raw = pm_metal_mem_alloc(total);
  if (raw == NULL) {
    return NULL;
  }

  aligned = ((uintptr_t)raw + sizeof(void *) + PAGE_SIZE - 1u) & ~(uintptr_t)(PAGE_SIZE - 1u);
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
  if (addr == NULL) {
    return;
  }
  pm_metal_mem_free((uint8_t *)((void **)addr)[-1]);
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

/* Condvars — single-thread stubs (runtime_timer creates/destroys only). */
int os_cond_init(korp_cond *cond)
{
  if (cond == NULL) {
    return BHT_ERROR;
  }
  cond->dummy = 0;
  return BHT_OK;
}

int os_cond_destroy(korp_cond *cond)
{
  (void)cond;
  return BHT_OK;
}

int os_cond_wait(korp_cond *cond, korp_mutex *mutex)
{
  (void)cond;
  (void)mutex;
  return BHT_ERROR;
}

int os_cond_reltimedwait(korp_cond *cond, korp_mutex *mutex, uint64 useconds)
{
  (void)cond;
  (void)mutex;
  (void)useconds;
  return BHT_ERROR;
}

int os_cond_signal(korp_cond *cond)
{
  (void)cond;
  return BHT_OK;
}

int os_cond_broadcast(korp_cond *cond)
{
  (void)cond;
  return BHT_OK;
}
