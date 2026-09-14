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
#include "pymergetic/metal/net/neighbors/__exports__.h"
#include "pymergetic/metal/net/zenoh/__exports__.h"
#include "pymergetic/metal/net/ip/__exports__.h"
#include "pymergetic/metal/coop/__exports__.h"

#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

void pm_metal_services_publish_deinit(void);

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
    pm_metal_services_publish_deinit();
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
    {
        uint32_t i;
        for (i = 0; i < s_nsvc; i++) {
            if ((s_svcs[i].flags & PM_METAL_SERVICE_REMOTE) != 0u
                    && s_svcs[i].peer_id == peer_id
                    && strcmp(s_svcs[i].name, name) == 0) {
                s_svcs[i].port = port;
                strncpy(s_svcs[i].fqn, fqn, sizeof(s_svcs[i].fqn) - 1u);
                s_svcs[i].fqn[sizeof(s_svcs[i].fqn) - 1u] = '\0';
                return 0;
            }
        }
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

/* -- services-to-neighbors bridge ---------------------------------------
 * One bounded text record per local service, carried by real zenoh PUTs:
 *   zidhex|host|name|fqn|port
 * The key is metal/services/<zid>. Repeated records are upserts, so periodic
 * publication repairs loss and reconnect without duplicate registry rows. */
static uint8_t s_mesh_started;
static uint64_t s_mesh_next_us;
static uint64_t s_mesh_burst_until_us;
static char s_mesh_host[64];

static void zid_hex(const uint8_t zid[16], char out[33]) {
    static const char h[] = "0123456789abcdef";
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        out[i * 2u] = h[zid[i] >> 4];
        out[i * 2u + 1u] = h[zid[i] & 15u];
    }
    out[32] = 0;
}

static void mesh_recv(const char *key, size_t klen, const uint8_t *payload,
        size_t plen, void *arg) {
    char row[384];
    char *zid, *host, *name, *fqn, *port_s;
    char *save = NULL;
    int32_t peer;
    (void)key; (void)klen; (void)arg;
    if (payload == NULL || plen == 0u || plen >= sizeof(row)) return;
    memcpy(row, payload, plen); row[plen] = 0;
    zid = strtok_r(row, "|", &save); host = strtok_r(NULL, "|", &save);
    name = strtok_r(NULL, "|", &save); fqn = strtok_r(NULL, "|", &save);
    port_s = strtok_r(NULL, "|", &save);
    if (zid == NULL || host == NULL || name == NULL || fqn == NULL || port_s == NULL) return;
    {
        uint8_t own[16]; char own_hex[33];
        if (pm_metal_net_zenoh_zid(own) == 1) {
            zid_hex(own, own_hex);
            if (strcmp(own_hex, zid) == 0) return;
        }
    }
    peer = pm_metal_neighbors_add(zid, host,
        PM_METAL_NEIGHBOR_CAP_HAS_ZENOH | PM_METAL_NEIGHBOR_CAP_HAS_SERVICES);
    if (peer > 0) {
        (void)pm_metal_neighbors_seen(zid);
        (void)pm_metal_services_register_remote(name, fqn,
            (uint16_t)strtoul(port_s, NULL, 10), (uint32_t)peer);
    }
}

int32_t pm_metal_services_mesh_start(const char *host) {
    if (s_mesh_started) return 0;
    if (host == NULL || host[0] == 0) return -1;
    strncpy(s_mesh_host, host, sizeof(s_mesh_host) - 1u);
    s_mesh_host[sizeof(s_mesh_host) - 1u] = 0;
    if (pm_metal_net_zenoh_subscribe("metal/services/**", mesh_recv, NULL) != 1) return -1;
    s_mesh_started = 1u;
    s_mesh_next_us = 0u;
    /* Both firmware seats open independently. Publish rapidly during the
     * initial convergence window so neither side can miss the other's roster
     * before its subscriber declaration reaches the peer. */
    s_mesh_burst_until_us = pm_metal_coop_mono_us() + 10000000ull;
    return 0;
}

int32_t pm_metal_services_publish_all(void) {
    uint8_t zid[16]; char zh[33]; char key[64]; char row[384];
    uint32_t i; int32_t rc = 0;
    if (!s_mesh_started || pm_metal_net_zenoh_zid(zid) != 1) return -1;
    zid_hex(zid, zh);
    (void)snprintf(key, sizeof(key), "metal/services/%s", zh);
    for (i = 0; i < s_nsvc; i++) {
        const pm_metal_service_record_t *r = &s_svcs[i];
        int n;
        if ((r->flags & PM_METAL_SERVICE_LOCAL) == 0u || r->svc == NULL) continue;
        n = snprintf(row, sizeof(row), "%s|%s|%s|%s|%u", zh, s_mesh_host,
            r->svc->name, r->svc->fqn, (unsigned)r->svc->default_port);
        if (n <= 0 || (size_t)n >= sizeof(row)
                || pm_metal_net_zenoh_put(key, (const uint8_t *)row, (uint32_t)n) != 1) rc = -1;
    }
    return rc;
}

void pm_metal_services_mesh_poll(void) {
    uint64_t now;
    if (!s_mesh_started) return;
    now = pm_metal_coop_mono_us();
    if (now >= s_mesh_next_us) {
        (void)pm_metal_services_publish_all();
        s_mesh_next_us = now + (now < s_mesh_burst_until_us ? 50000ull : 500000ull);
    }
}

int32_t pm_metal_services_publish_init(void *unused) { (void)unused; return 0; }
int32_t pm_metal_services_publish_local(uint32_t idx) { (void)idx; return pm_metal_services_publish_all(); }
int32_t pm_metal_services_discover_peer_services(const char *zid, uint32_t peer) {
    (void)zid; (void)peer; return s_mesh_started ? 0 : -1;
}
void pm_metal_services_publish_deinit(void) {
    s_mesh_started = 0u;
    s_mesh_next_us = 0u;
    s_mesh_burst_until_us = 0u;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_register, pm_metal_services_register, int32_t(const pm_metal_service_t *));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_register_remote, pm_metal_services_register_remote, int32_t(const char *, const char *, uint16_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_drop_peer, pm_metal_services_drop_peer, int32_t(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_peer_of, pm_metal_services_peer_of, uint32_t(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_publish_init, pm_metal_services_publish_init, int32_t(void *));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_publish_all, pm_metal_services_publish_all, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_mesh_start, pm_metal_services_mesh_start, int32_t(const char *));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_mesh_poll, pm_metal_services_mesh_poll, void(void));
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
