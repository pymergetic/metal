#include "lwipopts.h"
#include "lwip/sys.h"

#include "pymergetic/metal/async/time.h"

u32_t sys_now(void)
{
    return (u32_t)(pm_metal_time_mono_us() / 1000ull);
}
