/*
 * Metal platform types for the vendored zenoh-pico (picked up via the
 * ZENOH_GENERIC branch of zenoh-pico/system/common/platform.h). This is the
 * integration's platform header — it is what makes the library sit on
 * pm_metal_net_ip_* instead of a POSIX socket layer.
 *
 * system/common/platform.h already defines the dummy thread/mutex/condvar
 * types (_z_task_t = void * etc.) for every platform including single-threaded
 * builds, plus _z_time_since_epoch. This header only adds the four things the
 * common header references but cannot define generically: z_clock_t, z_time_t,
 * and the two system-net types.
 *
 * z_clock_t is a monotonic microsecond counter (uint64_t). z_time_t is a wall
 * clock that only the platform layer itself touches; kept as packed secs/nanos
 * so the header stays freestanding-friendly.
 */
#ifndef PM_METAL_NET_ZENOH_GENERIC_PLATFORM_H
#define PM_METAL_NET_ZENOH_GENERIC_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#include "zenoh-pico/config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t z_clock_t;

typedef struct _z_time_t {
    uint32_t secs;
    uint32_t nanos;
} z_time_t;

/* A Metal net.ip socket fd. Small enough to pass by value, which is how
 * zenoh-pico's link layer carries it into _z_read_*_tcp / _z_send_*_tcp. */
typedef struct {
    int _fd;
} _z_sys_net_socket_t;

/* Resolved IPv4 endpoint (network-order address + host-order port). The unix
 * platform stores a struct addrinfo *; Metal resolves host:port to this literal
 * pair at _z_create_endpoint_* time (net.ip is IPv4-only). */
typedef struct {
    uint32_t _addr_be;
    uint16_t _port_host;
} _z_sys_net_endpoint_t;

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_NET_ZENOH_GENERIC_PLATFORM_H */
