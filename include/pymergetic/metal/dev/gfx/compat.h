/* Shims for restored scanout backends (old runtime to live Metal APIs). */
#ifndef PM_METAL_DEV_GFX_COMPAT_H_
#define PM_METAL_DEV_GFX_COMPAT_H_

#include <stddef.h>
#include <stdint.h>

#include "io_pci.h"
#include "pymergetic/metal/async/time.h"
#include "pymergetic/metal/bus/pci.h"
#include "pymergetic/metal/mem.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void pm_metal_gfx_out16(uint16_t port, uint16_t val)
{
    outw(port, val);
}

static inline uint16_t pm_metal_gfx_in16(uint16_t port)
{
    uint16_t val;
    __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void pm_metal_mem_fence(void)
{
    __asm__ volatile("mfence" ::: "memory");
}

static inline void pm_metal_cpu_pause(void)
{
    __asm__ volatile("pause");
}

static inline void pm_metal_gfx_logf(const char *fmt, ...)
{
    (void)fmt;
}

#ifdef __cplusplus
}
#endif

#endif
