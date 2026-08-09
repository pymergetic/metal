/*
 * µPy network port hooks for Metal + lwIP (NO_SYS).
 * sys_now lives in metal net/ip/lwip_sys.c (shared with Metal sockets).
 */
#include "py/mphal.h"
#include "extmod/modnetwork.h"

#if MICROPY_PY_LWIP

#include "lwip/timeouts.h"

void mod_network_lwip_init(void)
{
    /* Metal owns lwip_init via pm_metal_net_ip_* bring-up. */
}

void mod_network_lwip_poll_wrapper(uint32_t ticks_ms)
{
    (void)ticks_ms;
    sys_check_timeouts();
}

#endif /* MICROPY_PY_LWIP */
