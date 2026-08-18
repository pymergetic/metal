/* pymergetic.metal.services — umbrella (path == module). */
#ifndef PYMERGETIC_METAL_SERVICES_H
#define PYMERGETIC_METAL_SERVICES_H

#include "pymergetic/metal/services/__exports__.h" /* IWYU pragma: export */
#include "pymergetic/metal/services/__types__.h"   /* IWYU pragma: export */

/* Server-card self-registration. Declare a const pm_metal_service_t record and
 * register it via a constructor into the services table. Use from a `__impl__.c`
 * (ssh) or a tiny C shim beside an RS server (asgi). Example:
 *
 *   PM_MOD_SERVICE_C(pymergetic.metal.services, ssh_svc,
 *       "ssh", pm_metal_net_ssh_listen, pm_metal_net_ssh_count,
 *       pm_metal_net_ssh_status, pm_metal_net_ssh_stop, 0u, 2222);
 */
#define PM_METAL_SERVICE_CAT_(a, b) a##b
#define PM_METAL_SERVICE_CAT(a, b) PM_METAL_SERVICE_CAT_(a, b)

#define PM_MOD_SERVICE_C(mod, sym, name, listen_fn, count_fn, status_fn, stop_fn, addr, port) \
    static const pm_metal_service_t s_##sym = { \
        (name), (#mod), (uint32_t)(addr), (uint16_t)(port), \
        (pm_metal_service_listen_fn)(listen_fn), (pm_metal_service_count_fn)(count_fn), \
        (pm_metal_service_status_fn)(status_fn), (pm_metal_service_stop_fn)(stop_fn), \
    }; \
    static void __attribute__((constructor)) PM_METAL_SERVICE_CAT(pm_metal_svc_reg_, sym)(void) { \
        (void)pm_metal_services_register(&s_##sym); \
    }

#endif /* PYMERGETIC_METAL_SERVICES_H */
