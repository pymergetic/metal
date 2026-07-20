/*
 * EFI / freestanding WAMR platform — vmcore APIs.
 */
#include "platform_api_vmcore.h"
#include "platform_api_extension.h"
#include "efi_wamr_internal.h"

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>

#include <mem/mem.h>
#include <time/time.h>

#include <stdarg.h>
#include <stdio.h>

int
bh_platform_init(void)
{
    return BHT_OK;
}

void
bh_platform_destroy(void)
{}

void *
os_malloc(unsigned size)
{
    return pm_metal_mem_alloc((size_t)size, PM_METAL_MEM_HEAP,
                              PM_METAL_MEM_ID_NONE);
}

void *
os_realloc(void *ptr, unsigned size)
{
    return pm_metal_mem_realloc(ptr, (size_t)size);
}

void
os_free(void *ptr)
{
    pm_metal_mem_free(ptr);
}

int
os_dumps_proc_mem_info(char *out, unsigned int size)
{
    if (out == NULL || size == 0)
        return BHT_ERROR;
    out[0] = '\0';
    return BHT_ERROR;
}

int
os_printf(const char *format, ...)
{
    va_list ap;
    int ret;

    va_start(ap, format);
    ret = os_vprintf(format, ap);
    va_end(ap);
    return ret;
}

int
os_vprintf(const char *format, va_list ap)
{
    CHAR8 buf[512];
    int n;

    if (format == NULL)
        return 0;

    /* C %s semantics (see mem/libc.c vsnprintf) — not EDK2 AsciiVSPrint. */
    n = vsnprintf(buf, sizeof(buf), format, ap);
    if (n < 0)
        return n;
    if ((UINTN)n >= sizeof(buf))
        n = (int)sizeof(buf) - 1;
    buf[n] = '\0';

    /* Serial immediately; tab via line buffer (no second serial Print). */
    Print(L"%a", buf);
    efi_wamr_feed_stdout((const char *)buf, (size_t)n, 0);
    return n;
}

uint64
os_time_get_boot_us(void)
{
    return (uint64)pm_metal_time_mono_us();
}

uint64
os_time_thread_cputime_us(void)
{
    return os_time_get_boot_us();
}

korp_tid
os_self_thread(void)
{
    return (korp_tid)1;
}

uint8 *
os_thread_get_stack_boundary(void)
{
    return NULL;
}

void
os_thread_jit_write_protect_np(bool enabled)
{
    (void)enabled;
}

int
os_mutex_init(korp_mutex *mutex)
{
    if (mutex == NULL)
        return BHT_ERROR;
    *mutex = 0;
    return BHT_OK;
}

int
os_mutex_destroy(korp_mutex *mutex)
{
    (void)mutex;
    return BHT_OK;
}

int
os_mutex_lock(korp_mutex *mutex)
{
    (void)mutex;
    return BHT_OK;
}

int
os_mutex_unlock(korp_mutex *mutex)
{
    (void)mutex;
    return BHT_OK;
}

void *
os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file)
{
    void *addr;

    (void)hint;
    (void)prot;
    (void)flags;
    (void)file;

    if (size == 0 || size >= UINT32_MAX)
        return NULL;

    addr = BH_MALLOC((unsigned)size);
    if (addr != NULL)
        SetMem(addr, (UINTN)size, 0);
    return addr;
}

void *
os_mremap(void *old_addr, size_t old_size, size_t new_size)
{
    return os_mremap_slow(old_addr, old_size, new_size);
}

void
os_munmap(void *addr, size_t size)
{
    (void)size;
    BH_FREE(addr);
}

int
os_mprotect(void *addr, size_t size, int prot)
{
    (void)addr;
    (void)size;
    (void)prot;
    return 0;
}

void
os_dcache_flush(void)
{}

void
os_icache_flush(void *start, size_t len)
{
    (void)start;
    (void)len;
}

os_raw_file_handle
os_invalid_raw_handle(void)
{
    return -1;
}

__wasi_errno_t
os_clock_res_get(__wasi_clockid_t clock_id, __wasi_timestamp_t *resolution)
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

__wasi_errno_t
os_clock_time_get(__wasi_clockid_t clock_id, __wasi_timestamp_t precision,
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
