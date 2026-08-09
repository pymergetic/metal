/*
 * Unix seat — single-CPU SMP face (host process; no AP bringup).
 */
#include "pymergetic/metal/async/smp.h"

uint32_t pm_metal_smp_cpu_index(void)
{
    return 0;
}

uint32_t pm_metal_smp_online_count(void)
{
    return 1;
}

int32_t pm_metal_smp_start(void)
{
    return 0;
}
