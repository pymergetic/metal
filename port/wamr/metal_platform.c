/*
 * WAMR platform glue for Metal firmware and the emcc browser cell.
 * Single-thread malloc mmap; mutex/cond are no-op (interp, no pthread).
 */
#include "platform_api_vmcore.h"
#if defined(__EMSCRIPTEN__)
/* emcc libc already has WASI types; WAMR platform_api_extension.h
 * pulls platform_wasi_types.h and -Werror dies on the clash. */
void *os_mremap_slow(void *old_addr, size_t old_size, size_t new_size);
#else
#include "platform_api_extension.h"
#endif

#include <stdarg.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
#include <stdio.h>
#else
void uart_write(const char *s, size_t n);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);
#endif

int pm_metal_wamr_vprintf(const char *fmt, va_list ap) {
    char buf[256];
    int n;
#if defined(__EMSCRIPTEN__)
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n > 0) {
        fwrite(buf, 1, (size_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1), stderr);
    }
    return n;
#else
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n > 0) {
        size_t w = (size_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1);
        uart_write(buf, w);
    }
    return n;
#endif
}

int pm_metal_wamr_printf(const char *fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = pm_metal_wamr_vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int bh_platform_init(void) {
    return 0;
}

void bh_platform_destroy(void) {
}

void *os_malloc(unsigned size) {
    return malloc(size);
}

void *os_realloc(void *ptr, unsigned size) {
    return realloc(ptr, size);
}

void os_free(void *ptr) {
    free(ptr);
}

int os_dumps_proc_mem_info(char *out, unsigned int size) {
    (void)out;
    (void)size;
    return -1;
}

void *os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file) {
    void *addr;
    (void)hint;
    (void)prot;
    (void)flags;
    (void)file;
    if (size == 0 || size >= (size_t)UINT32_MAX) {
        return NULL;
    }
    addr = BH_MALLOC((unsigned)size);
    if (addr != NULL) {
        memset(addr, 0, size);
    }
    return addr;
}

void *os_mremap(void *old_addr, size_t old_size, size_t new_size) {
    return os_mremap_slow(old_addr, old_size, new_size);
}

void os_munmap(void *addr, size_t size) {
    (void)size;
    BH_FREE(addr);
}

int os_mprotect(void *addr, size_t size, int prot) {
    (void)addr;
    (void)size;
    (void)prot;
    return 0;
}

void os_dcache_flush(void) {
}

void os_icache_flush(void *start, size_t len) {
    (void)start;
    (void)len;
}

os_file_handle os_get_invalid_handle(void) {
    return -1;
}

os_raw_file_handle os_invalid_raw_handle(void) {
    return -1;
}

int os_getpagesize(void) {
    return 4096;
}

uint64 os_time_get_boot_us(void) {
    static uint64 times;
    return ++times;
}

uint64 os_time_thread_cputime_us(void) {
    return os_time_get_boot_us();
}

korp_tid os_self_thread(void) {
    return 1;
}

uint8 *os_thread_get_stack_boundary(void) {
    return NULL;
}

void os_thread_jit_write_protect_np(bool enabled) {
    (void)enabled;
}

int os_mutex_init(korp_mutex *mutex) {
    if (mutex == NULL) {
        return -1;
    }
    *mutex = 0;
    return 0;
}

int os_mutex_destroy(korp_mutex *mutex) {
    (void)mutex;
    return 0;
}

int os_mutex_lock(korp_mutex *mutex) {
    if (mutex == NULL) {
        return -1;
    }
    *mutex = 1;
    return 0;
}

int os_mutex_unlock(korp_mutex *mutex) {
    if (mutex == NULL) {
        return -1;
    }
    *mutex = 0;
    return 0;
}

int os_cond_init(korp_cond *cond) {
    (void)cond;
    return 0;
}

int os_cond_destroy(korp_cond *cond) {
    (void)cond;
    return 0;
}

int os_cond_wait(korp_cond *cond, korp_mutex *mutex) {
    (void)cond;
    (void)mutex;
    return 0;
}

int os_cond_reltimedwait(korp_cond *cond, korp_mutex *mutex, uint64 useconds) {
    (void)cond;
    (void)mutex;
    (void)useconds;
    return 0;
}

int os_cond_signal(korp_cond *cond) {
    (void)cond;
    return 0;
}

int os_cond_broadcast(korp_cond *cond) {
    (void)cond;
    return 0;
}

int os_rwlock_init(korp_rwlock *lock) {
    (void)lock;
    return 0;
}

int os_rwlock_rdlock(korp_rwlock *lock) {
    (void)lock;
    return 0;
}

int os_rwlock_wrlock(korp_rwlock *lock) {
    (void)lock;
    return 0;
}

int os_rwlock_unlock(korp_rwlock *lock) {
    (void)lock;
    return 0;
}

int os_rwlock_destroy(korp_rwlock *lock) {
    (void)lock;
    return 0;
}

int os_thread_create(korp_tid *p_tid, thread_start_routine_t start, void *arg, unsigned int stack_size) {
    (void)p_tid;
    (void)start;
    (void)arg;
    (void)stack_size;
    return -1;
}

int os_thread_create_with_prio(korp_tid *p_tid, thread_start_routine_t start, void *arg,
    unsigned int stack_size, int prio) {
    (void)p_tid;
    (void)start;
    (void)arg;
    (void)stack_size;
    (void)prio;
    return -1;
}

int os_thread_join(korp_tid thread, void **retval) {
    (void)thread;
    (void)retval;
    return -1;
}

int os_thread_detach(korp_tid thread) {
    (void)thread;
    return -1;
}

void os_thread_exit(void *retval) {
    (void)retval;
}

int os_thread_env_init(void) {
    return 0;
}

void os_thread_env_destroy(void) {
}

bool os_thread_env_inited(void) {
    return true;
}
