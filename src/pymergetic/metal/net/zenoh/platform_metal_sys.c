/*
 * pymergetic.metal.net.zenoh — zenoh-pico OS-system platform on top of the
 * Metal cards: memory from pm_util_mem (the card arena), a monotonic clock
 * from pm_metal_async_mono_us, and a seedable PRNG for ZID/lease noise.
 *
 * Compiled everywhere (unix, emcc, firmware): everything here lives on the
 * Metal card faces, so there is no seat-specific #if except the Firmware/UEFI
 * sleep fill (no libc sleep there). This is the system half; the network half
 * is platform_metal.c.
 *
 * z_time_* and _z_get_time_since_epoch are part of zenoh-pico's platform API
 * surface but are not called by any core transport path, so they are provided
 * for ABI completeness from the same monotonic clock.
 */
#include "zenoh-pico/system/common/platform.h"
#include "zenoh-pico/utils/logging.h"
#include "zenoh-pico/utils/result.h"

#include "pymergetic/metal/net/zenoh/__priv__.h"

#include <stdio.h>
#include <string.h>

/*------------------ Memory ------------------*/

void *z_malloc(size_t size) {
    pm_util_mem_arena_t *a = pm_metal_net_zenoh_arena();
    if (a == NULL) {
        return NULL;
    }
    return pm_util_mem_alloc(a, size);
}

void *z_realloc(void *ptr, size_t size) {
    pm_util_mem_arena_t *a = pm_metal_net_zenoh_arena();
    if (a == NULL) {
        return NULL;
    }
    return pm_util_mem_realloc(a, ptr, size);
}

void z_free(void *ptr) {
    pm_util_mem_arena_t *a = pm_metal_net_zenoh_arena();
    if (a != NULL) {
        pm_util_mem_free(a, ptr);
    }
}

/*------------------ Random ------------------*/

/* xorshift64 PRNG. Zenoh uses z_random_fill for auto ZIDs and lease noise; a
 * CSPRNG is not required (the wire is authenticated at a higher layer), and
 * firmware has no getrandom. Seeded once from the monotonic clock at the first
 * call. */
static uint64_t s_rng_state;
static uint8_t s_rng_seeded;

static void rng_seed(void) {
    uint64_t t = pm_metal_async_mono_us();
    uint64_t s = 0x9e3779b97f4a7c15ull; /* golden-ratio noise */
    if (s_rng_seeded) {
        return;
    }
    /* Mix the counter lightly; even a constant boot clock yields nonzero */
    t = (t ^ (t >> 30)) * 0xbf58476d1ce4e5b9ull;
    t = (t ^ (t >> 27)) * 0x94d049bb133111ebull;
    s ^= t;
    s_rng_state = s ? s : 1u;
    s_rng_seeded = 1;
}

static uint64_t rng_next(void) {
    uint64_t x = s_rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    s_rng_state = x;
    return x;
}

uint8_t z_random_u8(void) {
    rng_seed();
    return (uint8_t)rng_next();
}

uint16_t z_random_u16(void) {
    rng_seed();
    return (uint16_t)rng_next();
}

uint32_t z_random_u32(void) {
    rng_seed();
    return (uint32_t)rng_next();
}

uint64_t z_random_u64(void) {
    rng_seed();
    return rng_next();
}

void z_random_fill(void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    rng_seed();
    while (len-- > 0) {
        *p++ = (uint8_t)rng_next();
    }
}

/*------------------ Clock ------------------*/

z_clock_t z_clock_now(void) {
    return (z_clock_t)pm_metal_async_mono_us();
}

unsigned long zp_clock_elapsed_us_since(z_clock_t *instant, z_clock_t *epoch) {
    uint64_t d = *instant > *epoch ? *instant - *epoch : 0;
    return (unsigned long)(d / 1000ull);
}

unsigned long zp_clock_elapsed_ms_since(z_clock_t *instant, z_clock_t *epoch) {
    uint64_t d = *instant > *epoch ? *instant - *epoch : 0;
    return (unsigned long)(d / 1000ull);
}

unsigned long zp_clock_elapsed_s_since(z_clock_t *instant, z_clock_t *epoch) {
    uint64_t d = *instant > *epoch ? *instant - *epoch : 0;
    return (unsigned long)(d / 1000000ull);
}

unsigned long z_clock_elapsed_us(z_clock_t *time) {
    return zp_clock_elapsed_us_since(&(z_clock_t){pm_metal_async_mono_us()}, time);
}

unsigned long z_clock_elapsed_ms(z_clock_t *time) {
    return zp_clock_elapsed_ms_since(&(z_clock_t){pm_metal_async_mono_us()}, time);
}

unsigned long z_clock_elapsed_s(z_clock_t *time) {
    return zp_clock_elapsed_s_since(&(z_clock_t){pm_metal_async_mono_us()}, time);
}

void z_clock_advance_us(z_clock_t *clock, unsigned long duration) {
    *clock += (z_clock_t)duration;
}

void z_clock_advance_ms(z_clock_t *clock, unsigned long duration) {
    *clock += (z_clock_t)duration * 1000ull;
}

void z_clock_advance_s(z_clock_t *clock, unsigned long duration) {
    *clock += (z_clock_t)duration * 1000000ull;
}

/*------------------ Sleep ------------------*/

z_result_t z_sleep_us(size_t time) {
    uint64_t t0 = pm_metal_async_mono_us();
    uint64_t want = (uint64_t)time;
    while (pm_metal_async_mono_us() - t0 < want) {
        /* Busy-wait: firmware has no sleep syscall, and the zenoh spin mode
         * only ever naps microseconds in the bounded poll() step. */
    }
    return _Z_RES_OK;
}

z_result_t z_sleep_ms(size_t time) {
    return z_sleep_us(time * 1000ull);
}

z_result_t z_sleep_s(size_t time) {
    return z_sleep_us(time * 1000000ull);
}

/*------------------ Time (wall clock, ABI completeness) ------------------*/

z_time_t z_time_now(void) {
    z_time_t now;
    uint64_t us = pm_metal_async_mono_us();
    now.secs = (uint32_t)(us / 1000000ull);
    now.nanos = (uint32_t)((us % 1000000ull) * 1000ull);
    return now;
}

const char *z_time_now_as_str(char *const buf, unsigned long buflen) {
    if (buf == NULL || buflen == 0) {
        return buf;
    }
    buf[0] = '\0';
    return buf;
}

unsigned long z_time_elapsed_us(z_time_t *time) {
    uint64_t us = pm_metal_async_mono_us();
    uint64_t then_us = ((uint64_t)time->secs * 1000000ull) + ((uint64_t)time->nanos / 1000ull);
    return (unsigned long)(us > then_us ? us - then_us : 0);
}

unsigned long z_time_elapsed_ms(z_time_t *time) {
    return z_time_elapsed_us(time) / 1000ull;
}

unsigned long z_time_elapsed_s(z_time_t *time) {
    return z_time_elapsed_us(time) / 1000000ull;
}

z_result_t _z_get_time_since_epoch(_z_time_since_epoch *t) {
    uint64_t us;
    if (t == NULL) {
        _Z_ERROR_RETURN(_Z_ERR_GENERIC);
    }
    us = pm_metal_async_mono_us();
    t->secs = (uint32_t)(us / 1000000ull);
    t->nanos = (uint32_t)((us % 1000000ull) * 1000ull);
    return _Z_RES_OK;
}

/*------------------ IPv4 endpoint formatting ------------------*/

z_result_t _z_ip_port_to_endpoint(const uint8_t *address, size_t address_len, uint16_t port, char *dst,
                                  size_t dst_len) {
    int n;
    if (address == NULL || dst == NULL || dst_len == 0) {
        _Z_ERROR_RETURN(_Z_ERR_INVALID);
    }
    if (address_len != sizeof(uint32_t)) {
        /* net.ip is IPv4-only. */
        _Z_ERROR_RETURN(_Z_ERR_TRANSPORT_NOT_AVAILABLE);
    }
    n = snprintf(dst, dst_len, "%u.%u.%u.%u:%u", address[0], address[1], address[2], address[3],
        (unsigned)port);
    if (n < 0 || (size_t)n >= dst_len) {
        _Z_ERROR_RETURN(_Z_ERR_GENERIC);
    }
    return _Z_RES_OK;
}
