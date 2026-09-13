/* pymergetic.metal.services -- prove the registry + the live-instance walk.
 * Registers a scratch service, checks count/name/fqn/port/instances/status,
 * then start/stop through the same owner face the walkers use.
 *
 * Remote service tests: register_remote, drop_peer, peer_of.
 */
#include "pymergetic/metal/services/__exports__.h"

#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t g_started = -1;

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
    uint32_t n;
    uint32_t i;
    uint32_t found = 0;
    uint32_t idx = 0;
    const char *name;
    n = pm_metal_services_count();
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

/* -- remote service tests -- */

static int32_t case_register_remote(void) {
    uint32_t before = pm_metal_services_count();
    uint32_t idx;
    int32_t rc;
    rc = pm_metal_services_register_remote("httpd", "pymergetic.metal.net.http", 8080, 42);
    if (rc != 0) return fail("register_remote returned error");
    if (pm_metal_services_count() != before + 1) return fail("count did not increase");
    idx = before;
    if (pm_metal_services_peer_of(idx) != 42) return fail("peer_of wrong");
    if (pm_metal_services_instances(idx) != 0u) return fail("remote instances not 0");
    if (pm_metal_services_status(idx, 0) != -1) return fail("remote status should be -1");
    if (pm_metal_services_start(idx) != -1) return fail("remote start should be -1");
    if (pm_metal_services_stop(idx, 0) != -1) return fail("remote stop should be -1");
    return 0;
}

static int32_t case_remote_name_fqn_port(void) {
    uint32_t before = pm_metal_services_count();
    uint32_t idx;
    const char *name;
    const char *fqn;
    uint16_t port;
    int32_t rc;
    rc = pm_metal_services_register_remote("ssh", "pymergetic.metal.net.ssh", 2222, 7);
    if (rc != 0) return fail("register_remote ssh");
    idx = before;
    name = pm_metal_services_name(idx);
    fqn = pm_metal_services_fqn(idx);
    port = pm_metal_services_port(idx);
    if (name == NULL || strcmp(name, "ssh") != 0) return fail("remote name");
    if (fqn == NULL || strcmp(fqn, "pymergetic.metal.net.ssh") != 0) return fail("remote fqn");
    if (port != 2222) return fail("remote port");
    return 0;
}

static int32_t case_drop_peer(void) {
    uint32_t n;
    uint32_t i;
    uint32_t found;
    int32_t rc;
    rc = pm_metal_services_register_remote("a", "net.a", 1, 100);
    if (rc != 0) return fail("reg a");
    rc = pm_metal_services_register_remote("b", "net.b", 2, 101);
    if (rc != 0) return fail("reg b");
    rc = pm_metal_services_register_remote("c", "net.c", 3, 100);
    if (rc != 0) return fail("reg c");
    rc = pm_metal_services_drop_peer(100);
    if (rc != 0) return fail("drop_peer error");
    n = pm_metal_services_count();
    found = 0;
    for (i = 0; i < n; i++) {
        if (pm_metal_services_peer_of(i) == 100) {
            found = 1;
            break;
        }
    }
    if (found) return fail("peer 100 still present after drop");
    found = 0;
    for (i = 0; i < n; i++) {
        const char *nm = pm_metal_services_name(i);
        if (nm != NULL && strcmp(nm, "b") == 0 &&
            pm_metal_services_peer_of(i) == 101) {
            found = 1;
            break;
        }
    }
    if (!found) return fail("peer 101 missing after drop");
    (void)pm_metal_services_drop_peer(101);
    return 0;
}

static int32_t case_drop_peer_rejects_zero(void) {
    int32_t rc = pm_metal_services_drop_peer(0);
    if (rc != -1) return fail("drop_peer(0) should return -1");
    return 0;
}

static int32_t case_register_remote_rejects_null(void) {
    int32_t rc;
    rc = pm_metal_services_register_remote(NULL, "net.x", 1, 1);
    if (rc != -1) return fail("NULL name not refused");
    rc = pm_metal_services_register_remote("x", NULL, 1, 1);
    if (rc != -1) return fail("NULL fqn not refused");
    rc = pm_metal_services_register_remote("x", "net.x", 1, 0);
    if (rc != -1) return fail("peer_id 0 not refused");
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.services, tests, pm_metal_services_tests);
PM_MOD_TEST_C(pymergetic.metal.services, case_register_remote, case_register_remote);
PM_MOD_TEST_C(pymergetic.metal.services, case_remote_name_fqn_port, case_remote_name_fqn_port);
PM_MOD_TEST_C(pymergetic.metal.services, case_drop_peer, case_drop_peer);
PM_MOD_TEST_C(pymergetic.metal.services, case_drop_peer_rejects_zero, case_drop_peer_rejects_zero);
PM_MOD_TEST_C(pymergetic.metal.services, case_register_remote_rejects_null, case_register_remote_rejects_null);
