/* pymergetic.metal.services — prove the registry + the live-instance walk.
 * Registers a scratch service, checks count/name/fqn/port/instances/status,
 * then start/stop through the same owner face the walkers use.
 */
#include "pymergetic/metal/services/__exports__.h"

#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t g_started = -1; /* emulates an owner holding instance id 3 */

static int32_t scratch_listen(uint32_t addr_be, uint16_t port) {
    (void)addr_be;
    (void)port;
    return (g_started = 3);
}

static uint32_t scratch_count(void) {
    return g_started >= 0 ? 1u : 0u;
}

static int32_t scratch_status(int32_t id) {
    return id == g_started ? 1 : 0;
}

static int32_t scratch_stop(int32_t id) {
    (void)id;
    g_started = -1;
    return 0;
}

/* Registered via constructor (mirrors how server cards attach). */
static const pm_metal_service_t s_scratch = {
    "scratch",
    "pymergetic.metal.services.test",
    0u,
    9999,
    scratch_listen,
    scratch_count,
    scratch_status,
    scratch_stop,
};
static void __attribute__((constructor)) reg_scratch(void) {
    (void)pm_metal_services_register(&s_scratch);
}

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.services test: %s\n", why);
    return 1;
}

int32_t pm_metal_services_tests(void) {
    uint32_t n = pm_metal_services_count();
    uint32_t i;
    uint32_t found = 0;
    uint32_t idx = 0;
    const char *name;
    if (n == 0) {
        return fail("count 0");
    }
    for (i = 0; i < n; i++) {
        name = pm_metal_services_name(i);
        if (name != NULL && strcmp(name, "scratch") == 0) {
            found = 1;
            idx = i;
        }
    }
    if (!found) {
        return fail("scratch not registered");
    }
    if (pm_metal_services_fqn(idx) == NULL || strcmp(pm_metal_services_fqn(idx), "pymergetic.metal.services.test") != 0) {
        return fail("fqn");
    }
    if (pm_metal_services_port(idx) != 9999) {
        return fail("port");
    }
    if (pm_metal_services_instances(idx) != 0u) {
        return fail("instances before start");
    }
    if (pm_metal_services_start(idx) != 3) {
        return fail("start id");
    }
    if (pm_metal_services_instances(idx) != 1u) {
        return fail("instances after start");
    }
    if (pm_metal_services_status(idx, 3) != 1 || pm_metal_services_status(idx, 0) != 0) {
        return fail("status");
    }
    if (pm_metal_services_stop(idx, 3) != 0) {
        return fail("stop");
    }
    if (pm_metal_services_instances(idx) != 0u) {
        return fail("instances after stop");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.services, tests, pm_metal_services_tests);
