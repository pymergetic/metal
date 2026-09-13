/* pymergetic.metal.services — registry of server service types.
 *
 * Every server card (`net.ssh`, `net.http.asgi`, ...) self-registers one
 * pm_metal_service_t describing its conventional listener: the short name used
 * in hints/`m.services()`, the owner module fqn, the default ANY(0)/port it
 * listens on, and its live per-instance query/control face (count/status/stop)
 * plus the `listen(addr, port)` that starts it. `m.serve()` and `m.services()`
 * are thin walks over this table, so adding a service is one registration
 * below, no REPL/help edits.
 *
 * Remote services: services discovered from another peer via the network.
 * They carry name/fqn/port strings directly (no function pointers -- remote
 * instances are owned by the peer) and a peer_id that links them to a
 * neighbor entry.
 */
#ifndef PYMERGETIC_METAL_SERVICES_TYPES_H
#define PYMERGETIC_METAL_SERVICES_TYPES_H

#include <stdint.h>

#include "pymergetic/util/mem/__types__.h"

typedef int32_t (*pm_metal_service_listen_fn)(uint32_t addr_be, uint16_t port);
typedef uint32_t (*pm_metal_service_count_fn)(void);
typedef int32_t (*pm_metal_service_status_fn)(int32_t id);
typedef int32_t (*pm_metal_service_stop_fn)(int32_t id);

typedef struct pm_metal_service {
    const char *name;        /* short, human name for hints: "ssh", "httpd" */
    const char *fqn;         /* owner module: "pymergetic.metal.net.ssh" */
    uint32_t default_addr;   /* 0u = ANY */
    uint16_t default_port;
    pm_metal_service_listen_fn listen;
    pm_metal_service_count_fn count;
    pm_metal_service_status_fn status;
    pm_metal_service_stop_fn stop;
} pm_metal_service_t;

/* Flags for a service record -- local (registered here) or remote (discovered). */
typedef uint32_t pm_metal_service_flags_t;
#define PM_METAL_SERVICE_LOCAL  1u
#define PM_METAL_SERVICE_REMOTE 2u

/* One unified service record in the arena-backed pool. A local record has
 * `svc` non-NULL and uses its name/fqn/port; a remote record has `svc` NULL
 * and the strings/port inline. */
typedef struct pm_metal_service_record {
    uint32_t flags;                  /* PM_METAL_SERVICE_LOCAL / _REMOTE */
    uint32_t peer_id;                /* 0 = local (this host) */
    const pm_metal_service_t *svc;   /* non-NULL when LOCAL flag is set */
    char name[64];                   /* remote: short name */
    char fqn[128];                   /* remote: owner module fqn */
    uint16_t port;                   /* remote: default port */
} pm_metal_service_record_t;

#endif /* PYMERGETIC_METAL_SERVICES_TYPES_H */