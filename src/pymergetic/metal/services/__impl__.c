/* pymergetic.metal.services — registered server types + live-instance walk.
 *
 * Registration is constructor-driven from the server cards (`PM_MOD_SERVICE_C`),
 * mirroring pm_mod_boot: the record lives in a `pm_metal_services` linker
 * section AND a constructor hands it to pm_metal_services_register, which
 * appends into a static array. The walkers (count/name/fqn/port/instances)
 * are only read at REPL time, long after every constructor has run, so the
 * append ordering between cards is irrelevant.
 */
#include "pymergetic/metal/services/__exports__.h"

#include <stdint.h>

#define PM_METAL_SERVICES_MAX 8u

static const pm_metal_service_t *s_svcs[PM_METAL_SERVICES_MAX];
static uint32_t s_nsvc;

int32_t pm_metal_services_init(pm_util_mem_arena_t *arena) {
    (void)arena;
    /* Registration is constructor-driven (server cards append during load);
     * nothing to set up here. */
    return 0;
}

void pm_metal_services_deinit(void) {
    s_nsvc = 0;
}

int32_t pm_metal_services_register(const pm_metal_service_t *rec) {
    if (rec == NULL || rec->name == NULL || rec->fqn == NULL || rec->listen == NULL
        || rec->count == NULL || rec->status == NULL || rec->stop == NULL) {
        return -1;
    }
    if (s_nsvc >= PM_METAL_SERVICES_MAX) {
        return -1;
    }
    s_svcs[s_nsvc++] = rec;
    return 0;
}

const pm_metal_service_t *pm_metal_services_at(uint32_t i) {
    if (i >= s_nsvc) {
        return NULL;
    }
    return s_svcs[i];
}

uint32_t pm_metal_services_count(void) {
    return s_nsvc;
}

const char *pm_metal_services_name(uint32_t i) {
    const pm_metal_service_t *r = pm_metal_services_at(i);
    return r != NULL ? r->name : NULL;
}

const char *pm_metal_services_fqn(uint32_t i) {
    const pm_metal_service_t *r = pm_metal_services_at(i);
    return r != NULL ? r->fqn : NULL;
}

uint16_t pm_metal_services_port(uint32_t i) {
    const pm_metal_service_t *r = pm_metal_services_at(i);
    return r != NULL ? r->default_port : 0;
}

/* Live instance count currently owned by service[i]. */
uint32_t pm_metal_services_instances(uint32_t i) {
    const pm_metal_service_t *r = pm_metal_services_at(i);
    return r != NULL ? r->count() : 0u;
}

/* 1 = instance id of service[i] is up, 0 = down, -1 = bad index. */
int32_t pm_metal_services_status(uint32_t i, int32_t id) {
    const pm_metal_service_t *r = pm_metal_services_at(i);
    return r != NULL ? r->status(id) : -1;
}

int32_t pm_metal_services_stop(uint32_t i, int32_t id) {
    const pm_metal_service_t *r = pm_metal_services_at(i);
    return r != NULL ? r->stop(id) : -1;
}

/* Start the default instance (default_addr, default_port) of service[i].
 * Returns the new instance id (>= 0) or -1 on failure. */
int32_t pm_metal_services_start(uint32_t i) {
    const pm_metal_service_t *r = pm_metal_services_at(i);
    return r != NULL ? r->listen(r->default_addr, r->default_port) : -1;
}

/* -- registration macro (used by server cards) --
 * Definition lives in services.h so `net.ssh`/`net.http.asgi` can use it. */

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_register, pm_metal_services_register, int32_t(const pm_metal_service_t *));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_count, pm_metal_services_count, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_name, pm_metal_services_name, const char *(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_fqn, pm_metal_services_fqn, const char *(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_port, pm_metal_services_port, uint16_t(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_instances, pm_metal_services_instances, uint32_t(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_status, pm_metal_services_status, int32_t(uint32_t, int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_stop, pm_metal_services_stop, int32_t(uint32_t, int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.services, pm_metal_services_start, pm_metal_services_start, int32_t(uint32_t));
PM_MOD_BOOT_C(pymergetic.metal.services, pm_metal_services_init, pm_metal_services_deinit);
