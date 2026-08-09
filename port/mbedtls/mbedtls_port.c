#include "mbedtls/mbedtls_config_port.h"

#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/async/time.h"
#include "pymergetic/metal/mem.h"

void mbedtls_platform_zeroize(void *buf, size_t len)
{
    volatile unsigned char *p = (volatile unsigned char *)buf;
    size_t i;
    for (i = 0; i < len; i++) {
        p[i] = 0;
    }
}

/* Minimal UTC breakdown for cert notBefore/notAfter checks. */
struct tm *mbedtls_platform_gmtime_r(const time_t *tt, struct tm *tm_buf)
{
    static const int mdays_n[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    static const int mdays_l[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int64_t t;
    int64_t days;
    int64_t secs;
    int y, m;
    const int *mdays;
    int leap;

    if (tt == NULL || tm_buf == NULL) {
        return NULL;
    }
    t = (int64_t)(*tt);
    if (t < 0) {
        t = 0;
    }
    days = t / 86400;
    secs = t % 86400;
    tm_buf->tm_hour = (int)(secs / 3600);
    tm_buf->tm_min = (int)((secs % 3600) / 60);
    tm_buf->tm_sec = (int)(secs % 60);
    tm_buf->tm_wday = (int)((days + 4) % 7); /* 1970-01-01 Thursday */
    y = 1970;
    for (;;) {
        leap = ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
        int ydays = leap ? 366 : 365;
        if (days < ydays) {
            break;
        }
        days -= ydays;
        y++;
    }
    tm_buf->tm_year = y - 1900;
    tm_buf->tm_yday = (int)days;
    leap = ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
    mdays = leap ? mdays_l : mdays_n;
    for (m = 0; m < 12; m++) {
        if (days < mdays[m]) {
            break;
        }
        days -= mdays[m];
    }
    tm_buf->tm_mon = m;
    tm_buf->tm_mday = (int)days + 1;
    tm_buf->tm_isdst = 0;
    return tm_buf;
}

/* Wall clock: boot epoch (2026-08-01) + mono; NTP can bump via setter. */
static volatile uint32_t g_unix_base = 1754006400u; /* 2026-08-01 UTC */
static uint64_t g_mono_base_us;

void pm_metal_mbedtls_set_unix_time(uint32_t unix_secs)
{
    g_unix_base = unix_secs;
    g_mono_base_us = pm_metal_async_mono_us();
}

void *pm_metal_mbedtls_calloc(size_t nmemb, size_t size)
{
    size_t n;
    uint8_t *p;

    if (nmemb == 0u || size == 0u) {
        return NULL;
    }
    if (nmemb > (SIZE_MAX / size)) {
        return NULL;
    }
    n = nmemb * size;
    p = pm_metal_mem_alloc(n);
    if (p != NULL) {
        memset(p, 0, n);
    }
    return p;
}

void pm_metal_mbedtls_free(void *ptr)
{
    pm_metal_mem_free((uint8_t *)ptr);
}

time_t pm_metal_mbedtls_time(time_t *timer)
{
    uint64_t now = pm_metal_async_mono_us();
    uint64_t delta_s;
    time_t t;

    if (g_mono_base_us == 0ull) {
        g_mono_base_us = now;
    }
    delta_s = (now - g_mono_base_us) / 1000000ull;
    t = (time_t)((uint64_t)g_unix_base + delta_s);
    if (timer != NULL) {
        *timer = t;
    }
    return t;
}

int64_t pm_metal_mbedtls_ms_time(void)
{
    return (int64_t)pm_metal_mbedtls_time(NULL) * 1000;
}

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
    size_t i;
    uint64_t mix = pm_metal_async_mono_us();

    (void)data;
    if (output == NULL || olen == NULL) {
        return -1;
    }
    /* Do not issue RDRAND without CPUID — #UD resets QEMU guests. */
    for (i = 0; i < len; i++) {
        mix ^= mix << 13;
        mix ^= mix >> 7;
        mix ^= mix << 17;
        mix += (uint64_t)i + 0x9e3779b97f4a7c15ull;
        output[i] = (unsigned char)(mix >> 3);
    }
    *olen = len;
    return 0;
}
