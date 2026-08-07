#include <stdint.h>

_Noreturn void pm_metal_rt_halt(void)
{
    for (;;) {
        __asm__ volatile("hlt");
    }
}
