/*
 * Browser C twin for pymergetic.metal.rt (RS muscle on firmware).
 */
#include "pymergetic/metal/rt/__init__.h"

#include <stdio.h>
#include <stdlib.h>

int32_t pm_metal_rt_halt(void)
{
#if defined(__EMSCRIPTEN__)
    abort();
#else
    for (;;) {
    }
#endif
    return -1;
}

int32_t pm_metal_rt_panic(const uint8_t *msg)
{
    return pm_metal_rt_panic_at(NULL, 0, msg);
}

int32_t pm_metal_rt_panic_at(const uint8_t *file, uint32_t line, const uint8_t *msg)
{
    fprintf(stderr, "panic");
    if (file != NULL && line != 0u) {
        fprintf(stderr, ": %s:%u", (const char *)file, (unsigned)line);
    }
    if (msg != NULL) {
        fprintf(stderr, ": %s", (const char *)msg);
    }
    fprintf(stderr, "\n");
    return pm_metal_rt_halt();
}

int32_t pm_metal_rt_register_symbols(void)
{
    return -1;
}

int32_t pm_metal_rt_connect_symbols(void)
{
    return 0;
}
