#include "wireguard-platform.h"

#include <string.h>

#include "lwip/sys.h"
#include "pymergetic/metal/async/time.h"

uint32_t wireguard_sys_now(void)
{
    return sys_now();
}

void wireguard_random_bytes(void *bytes, size_t size)
{
    uint8_t *b = (uint8_t *)bytes;
    static uint32_t ctr;
    uint64_t t = pm_metal_time_mono_us();
    uint32_t x = (uint32_t)t ^ (uint32_t)(t >> 32) ^ (++ctr * 0x9E3779B9u);
    size_t i;
    for (i = 0; i < size; i++) {
        x = x * 1664525u + 1013904223u;
        b[i] = (uint8_t)(x >> 24);
        if ((i & 3u) == 3u) {
            x ^= (uint32_t)pm_metal_time_mono_us() + ctr;
        }
    }
}

void wireguard_tai64n_now(uint8_t *output)
{
    uint64_t us = pm_metal_time_mono_us();
    uint64_t sec = us / 1000000ull;
    uint32_t nano = (uint32_t)((us % 1000000ull) * 1000ull);
    uint64_t tai = sec + 0x400000000000000aULL; /* TAI64 offset approx */
    int i;
    if (output == NULL) {
        return;
    }
    for (i = 7; i >= 0; i--) {
        output[i] = (uint8_t)(tai & 0xffu);
        tai >>= 8;
    }
    output[8] = (uint8_t)(nano >> 24);
    output[9] = (uint8_t)(nano >> 16);
    output[10] = (uint8_t)(nano >> 8);
    output[11] = (uint8_t)nano;
}

bool wireguard_is_under_load(void)
{
    return false;
}
