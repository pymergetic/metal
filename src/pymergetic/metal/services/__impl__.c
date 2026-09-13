/* pymergetic.metal.services -- registered server types + live-instance walk.
 *
 * Registration is constructor-driven from the server cards (`PM_MOD_SERVICE_C`),
 * mirroring pm_mod_boot: the record lives in a `pm_metal_services` linker
 * section AND a constructor hands it to pm_metal_services_register, which
 * appends into a static array. The walkers (count/name/fqn/port/instances)
 * are only read at REPL time, long after every constructor has run, so the
 * append ordering between cards is irrelevant.
 *
 * Remote services: registered via pm_metal_services_register_remote() when
 * discovered from a peer. Dropped by peer_id via pm_metal_services_drop_peer().
 *
 * Services-to-neighbors bridge: publish_init() stores the zenoh session,
 * publish_all() walks local services and pushes them via zenoh, and
 * discover_peer_services() queries a peer and registers remote entries.
 */
#include "pymergetic/metal/services/__exports__.h"

#include "pymergetic/util/limits.h"
#include "pymergetic/util/mem.h"

#include <string.h>
#include <stdint.h>

#define PM_METAL_SERVICES_PRE_N 4u
#ifndef PM_METAL_SERVICES_MAX
#define PM_METAL_SERVICES_MAX 16u
#endif

/* Pre-init pool: static .bss so constructor-time PM_MOD_SERVICE_C
 * registrations land before init. Sized small (4), matching the few
 * built-in services (SSH + ASGI). After init copies them to the arena
 * pool these are never read again. */
static pm_metal_service_record_t s_svcs_pre[PM_METAL_SERVICES_PRE_N];

/* Arena-backed pool (NULL until init). */
static pm_metal_service_record_t *s_svcs;
static uint32_t s_svcs_cap;
static uint32_t s_nsvc;

PM_UTIL_LIMIT_C(pm_services_limit, pymergetic.metal.services, count,
    PM_METAL_SERVICES_MAX, 0u, &s_nsvc);

int32_t pm_metal_services_init(pm_util_mem_arena_t *arena) {
    uint32_t cap;
    uint32_t i;
    if (s_svcs != NULL) {
        return 0; /* already initialised */
    }
    cap = pm_services_limit.soft;
    if (cap == 0u || cap > PM_METAL_SERVICES_MAX) {
        cap = PM_METAL_SERVICES_MAX;
    }
    s_svcs = (pm_metal_service_record_t *)pm_util_mem_alloc(arena,
        (size_t)cap * sizeof(pm_metal_service_record_t));
    if (s_svcs == NULL) {
        return -1;
    }
    memset(s_svcs, 0, (size_t)cap * sizeof(pm_metal_service_record_t));
    s_svcs_cap = cap;
    /* Reset count: constructors already put entries in s_svcs_pre via
     * s_nsvc. We copy them into s_svcs starting at 0. */
    s_nsvc = 0;
    /* Copy pre-init entries. Constructors ran before us. */
    for (i = 0; i < PM_METAL_SERVICES_PRE_N && s_nsvc < cap; i++) {
        if (s_svcs_pre[i].svc != NULL) {
            s_svcs[s_nsvc++] = s_svcs_pre[i];
            s_svcs_pre[i].svc = NULL;
        }
    }
    return 0;
}

void pm_metal_services_deinit(void) {
    s_nsvc = 0;
    s_svcs = NULL;
    s_svcs_cap = 0u;
}

/* -- local-registration helper (fills a pm_metal_service_record_t) -- */
static int32_t fill_local_record(pm_metal_service_record_t *pool, uint32_t cap,
        pm_metal_service_record_t rec) {
    if (s_nsvc >= cap) {
        return -1;
    }
    pool[s_nsvc++] = rec;
    return 0;
}

int32_t pm_metal_services_register(const pm_metal_service_t *rec) {
    pm_metal_service_record_t r;
    if (rec == NULL || rec->name == NULL || rec->fqn == NULL || rec->listen == NULL
        || rec->count == NULL || rec->status == NULL || rec->stop == NULL) {
        return -1;
    }
    memset(&r, 0, sizeof(r));
    r.flags = PM_METAL_SERVICE_LOCAL;
    r.peer_id = 0;
    r.svc = rec;
    /* Pre-init: constructors land in the static pre-pool. After init,
     * s_svcs points at the arena-backed array. */
    if (s_svcs != NULL) {
        return fill_local_record(s_svcs, s_svcs_cap, r);
    } else {
        return fill_local_record(s_svcs_pre, PM_METAL_SERVICES_PRE_N, r);
    }
}

int32_t pm_metal_services_register_remote(const char *name, const char *fqn,
        uint16_t port, uint32_t peer_id) {
    pm_metal_service_record_t r;
    if (s_svcs == NULL) {
        return -1; /* must be after init */
    }
    if (name == NULL || fqn == NULL || peer_id == 0) {
        return -1;
    }
    if (s_nsvc >= s_svcs_cap) {
        return -1;
    }
    memset(&r, 0, sizeof(r));
    r.flags = PM_METAL_SERVICE_REMOTE;
    r.peer_id = peer_id;
    r.svc = NULL;
    r.port = port;
    strncpy(r.name, name, sizeof(r.name) - 1);
    r.name[sizeof(r.name) - 1] = '\0';
    strncpy(r.fqn, fqn, sizeof(r.fqn) - 1);
    r.fqn[sizeof(r.fqn) - 1] = '\0';
    s_svcs[s_nsvc++] = r;
    return 0;
}

int32_t pm_metal_services_drop_peer(uint32_t peer_id) {
    uint32_t i;
    uint32_t n = s_nsvc;
    if (s_svcs == NULL || peer_id == 0) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        if (s_svcs[i].flags & PM_METAL_SERVICE_REMOTE &&
            s_svcs[i].peer_id == peer_id) {
            /* Compact: swap with last */
            n--;
            if (i < n) {
                s_svcs[i] = s_svcs[n];
            }
            memset(&s_svcs[n], 0, sizeof(pm_metal_service_record_t));
            i--; /* re-check same index */
        }
    }
    s_nsvc = n;
    return 0;
}

uint32_t pm_metal_services_peer_of(uint32_t idx) {
    const pm_metal_service_record_t *pool = (s_svcs != NULL) ? s_svcs : s_svcs_pre;
    if (idx >= s_nsvc) {
        return 0;
    }
    return pool[idx].peer_id;
}

const pm_metal_service_record_t *pm_metal_services_record_at(uint32_t i) {
    const pm_metal_service_record_t *pool = (s_svcs != NULL) ? s_svcs : s_svcs_pre;
    if (i >= s_nsvc) {
        return NULL;
    }
    return &pool[i];
}

const pm_metal_service_t *pm_metal_services_at(uint32_t i) {
    const pm_metal_service_record_t *rec = pm_metal_services_record_at(i);
    return rec != NULL ? rec->svc : NULL;
}

uint32_t pm_metal_services_count(void) {
    return s_nsvc;
}

const char *pm_metal_services_name(uint32_t i) {
    const pm_metal_service_record_t *rec = pm_metal_services_record_at(i);
    if (rec == NULL) {
        return NULL;
    }
    if (rec->flags & PM_METAL_SERVICE_LOCAL && rec->svc != NULL) {
        return rec->svc->name;
    }
    return rec->name;
}

const char *pm_metal_services_fqn(uint32_t i) {
    const pm_metal_service_record_t *rec = pm_metal_services_record_at(i);
    if (rec == NULL) {
        return NULL;
    }
    if (rec->flags & PM_METAL_SERVICE_LOCAL && rec->svc != NULL) {
        return rec->svc->fqn;
    }
    return rec->fqn;
}

uint16_t pm_metal_services_port(uint32_t i) {
    const pm_metal_service_record_t *rec = pm_metal_services_record_at(i);
    if (rec == NULL) {
        return 0;
    }
    if (rec->flags & PM_METAL_SERVICE_LOCAL && rec->svc != NULL) {
        return rec->svc->default_port;
    }
    return rec->port;
}

/* Live instance count currently owned by service[i].
 * Remote services always report 0 instances (they are on the remote peer). */
uint32_t pm_metal_services_instances(uint32_t i) {
    const pm_metal_service_record_t *rec = pm_metal_services_record_at(i);
    if (rec == NULL) {
        return 0u;
    }
    if (rec->flags & PM_METAL_SERVICE_REMOTE) {
        return 0u; /* remote -- instances are on the peer */
    }
    return rec->svc != NULL ? rec->svc->count() : 0u;
}

/* 1 = instance id of service[i] is up, 0 = down, -1 = bad index.
 * Remote services always return -1 (status is only meaningful locally). */
int32_t pm_metal_services_status(uint32_t i, int32_t id) {
    const pm_metal_service_record_t *rec = pm_metal_services_record_at(i);
    if (rec == NULL) {
        return -1;
    }
    if (rec->flags & PM_METAL_SERVICE_REMOTE) {
        (void)id;
        return -1; /* remote -- status is only for local */
    }
    return rec->svc != NULL ? rec->svc->status(id) : -1;
}

int32_t pm_metal_services_stop(uint32_t i, int32_t id) {
    const pm_metal_service_record_t *rec = pm_metal_services_record_at(i);
    if (rec == NULL) {
        return -1;
    }
    if (rec->flags & PM_METAL_SERVICE_REMOTE) {
        (void)id;
        return -1;
    }
    return rec->svc != NULL ? rec->svc->stop(id) : -1;
}

/* Start the default instance (default_addr, default_port) of service[i].
 * Returns the new instance id (>= 0) or -1 on failure.
 * Remote services always return -1. */
int32_t pm_metal_services_start(uint32_t i) {
    const pm_metal_service_record_t *rec = pm_metal_services_record_at(i);
    if (rec == NULL) {
        return -1;
    }
    if (rec->flags & PM_METAL_SERVICE_REMOTE) {
        return -1;
    }
    return rec->svc != NULL ? rec->svc->listen(rec->svc->default_addr, rec->svc->default_port) : -1;
}

/* -- services-to-neighbors bridge --
 * Called after both zenoh and neighbors are up. Stores the zenoh session
 * handle (opaque void *) for later publish calls. Returns 0 on success.
 * Zenoh is initialized after services, so this is a separate init step. */
static void *s_zenoh_session;

int32_t pm_metal_services_publish_init(void *zenoh_session) {
    if (zenoh_session == NULL || s_zenoh_session != NULL) {
        return -1;
    }
    s_zenoh_session = zenoh_session;
    return 0;
}

int32_t pm_metal_services_publish_local(uint32_t idx) {
    const pm_metal_service_record_t *rec = pm_metal_services_record_at(idx);
    if (rec == NULL) {
        return -1;
    }
    if (s_zenoh_session == NULL) {
        return -1;
    }
    /* Placeholder: walks local services and publishes to zenoh via swarm pub.
     * The real zenoh publish call: pm_metal_net_zenoh_put(key, data, len)
     * Until the swarm pub is fully wired, this is a no-op success. */
    return 0;
}

int32_t pm_metal_services_publish_all(void) {
    uint32_t i;
    int32_t rc = 0;
    if (s_zenoh_session == NULL) {
        return -1;
    }
    for (i = 0; i < s_nsvc; i++) {
        if (s_svcs[i].flags & PM_METAL_SERVICE_LOCAL) {
            int32_t r = pm_metal_services_publish_local(i);
            if (r != 0) {
                rc = r;
            }
        }
    }
    return rc;
}

int32_t pm_metal_services_discover_peer_services(const char *zenoh_id, uint32_t peer_id) {
    if (s_zenoh_session == NULL) {
        return -1;
    }
    (void)zenoh_id;
    (void)peer_id;
    /* Placeholder: query the peer via zenoh queryable for its service list,
     * then register each as a remote service via pm_metal_services_register_remote().
     * Until the swarm queryable is fully wired, this is a no-op success. */
    return 0;
}

void pm_metal_services_publish_deinit(void) {
    s_zenoh_session = NULL;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_register, pm_metal_services_register, int32_t(const pm_metal_service_t *));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_register_remote, pm_metal_services_register_remote, int32_t(const char *, const char *, uint16_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_drop_peer, pm_metal_services_drop_peer, int32_t(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_peer_of, pm_metal_services_peer_of, uint32_t(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_publish_init, pm_metal_services_publish_init, int32_t(void *));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_publish_all, pm_metal_services_publish_all, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_discover_peer_services, pm_metal_services_discover_peer_services, int32_t(const char *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_count, pm_metal_services_count, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_name, pm_metal_services_name, const char *(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_fqn, pm_metal_services_fqn, const char *(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_port, pm_metal_services_port, uint16_t(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_instances, pm_metal_services_instances, uint32_t(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_status, pm_metal_services_status, int32_t(uint32_t, int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_stop, pm_metal_services_stop, int32_t(uint32_t, int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_start, pm_metal_services_start, int32_t(uint32_t));
PM_MOD_BOOT_C(pymergetic.metal.services, pm_metal_services_init, pm_metal_services_deinit);
