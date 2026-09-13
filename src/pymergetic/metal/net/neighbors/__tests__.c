/* pymergetic.metal.net.neighbors -- prove the neighbor table. */
#include "pymergetic/metal/net/neighbors/__exports__.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail_nei(const char *why) {
    fprintf(stderr, "metal.net.neighbors test: %s\n", why);
    return 1;
}

/* 1: add a single neighbor and read it back */
static int32_t case_add_one(void) {
    int32_t peer_id;
    pm_metal_neighbor_t out;

    peer_id = pm_metal_neighbors_add("zid-aaa", "10.0.0.1",
        PM_METAL_NEIGHBOR_CAP_HAS_ZENOH);
    if (peer_id < 0) return fail_nei("add returned error");

    if (pm_metal_neighbors_count() != 1) return fail_nei("count != 1");

    if (pm_metal_neighbors_at(0, &out) != 0) return fail_nei("at error");
    if (strcmp(out.zenoh_id, "zid-aaa") != 0) return fail_nei("zenoh_id");
    if (strcmp(out.host, "10.0.0.1") != 0) return fail_nei("host");
    if (out.caps != PM_METAL_NEIGHBOR_CAP_HAS_ZENOH) return fail_nei("caps");
    if (out.alive != 1) return fail_nei("not alive");
    if (out.peer_id != (uint32_t)peer_id) return fail_nei("peer_id mismatch");

    return 0;
}

/* 2: idempotent add -- same zenoh_id updates, no new entry */
static int32_t case_add_idempotent(void) {
    int32_t p1, p2;
    pm_metal_neighbor_t out;

    p1 = pm_metal_neighbors_add("zid-bbb", "192.168.1.1", 0);
    if (p1 < 0) return fail_nei("add 1");

    /* Same zenoh_id, different host/caps -- should update, not duplicate */
    p2 = pm_metal_neighbors_add("zid-bbb", "192.168.1.2",
        PM_METAL_NEIGHBOR_CAP_HAS_BUILD);
    if (p2 < 0) return fail_nei("add 2");
    if (p1 != p2) return fail_nei("peer_id changed on idempotent add");

    if (pm_metal_neighbors_at((uint32_t)p1, &out) != 0) return fail_nei("at");
    if (strcmp(out.host, "192.168.1.2") != 0) return fail_nei("host not updated");
    if (out.caps != PM_METAL_NEIGHBOR_CAP_HAS_BUILD) return fail_nei("caps not updated");

    return 0;
}

/* 3: seen() returns peer_id */
static int32_t case_seen(void) {
    int32_t p, s;

    p = pm_metal_neighbors_add("zid-ccc", "10.0.0.3", 0);
    if (p < 0) return fail_nei("add");

    s = pm_metal_neighbors_seen("zid-ccc");
    if (s != p) return fail_nei("seen wrong peer_id");

    /* Unknown zenoh_id returns -1 */
    if (pm_metal_neighbors_seen("no-such") != -1) return fail_nei("seen unknown");

    return 0;
}

/* 4: drop removes an entry */
static int32_t case_drop(void) {
    uint32_t n;

    (void)pm_metal_neighbors_add("zid-ddd", "10.0.0.4", 0);
    n = pm_metal_neighbors_count();

    if (pm_metal_neighbors_drop("zid-ddd") != 0) return fail_nei("drop error");
    if (pm_metal_neighbors_count() != n - 1) return fail_nei("count after drop");

    /* Drop nonexistent */
    if (pm_metal_neighbors_drop("no-such") != -1) return fail_nei("drop nonexistent");

    return 0;
}

/* 5: find returns index */
static int32_t case_find(void) {
    int32_t idx;

    (void)pm_metal_neighbors_add("zid-eee", "10.0.0.5", PM_METAL_NEIGHBOR_CAP_HAS_SERVICES);
    idx = pm_metal_neighbors_find("zid-eee");
    if (idx < 0) return fail_nei("find negative");

    /* Find nonexistent */
    if (pm_metal_neighbors_find("no-such") != -1) return fail_nei("find nonexistent");

    return 0;
}

/* 6: reap compacts stale entries */
static int32_t case_reap(void) {
    uint32_t n_before, n_after, reaped;

    n_before = pm_metal_neighbors_count();
    (void)pm_metal_neighbors_add("zid-fff", "10.0.0.6", 0);
    (void)pm_metal_neighbors_add("zid-ggg", "10.0.0.7", 0);

    /* Drop "zid-ggg" (sets alive=0) */
    (void)pm_metal_neighbors_drop("zid-ggg");

    reaped = pm_metal_neighbors_reap(0);
    n_after = pm_metal_neighbors_count();
    if (reaped != 1) return fail_nei("reaped count");
    if (n_after != n_before + 1) return fail_nei("count after reap");

    /* Verify "zid-fff" still exists */
    if (pm_metal_neighbors_find("zid-fff") < 0) return fail_nei("zid-fff gone after reap");
    return 0;
}

/* 7: null arg rejection */
static int32_t case_null_reject(void) {
    if (pm_metal_neighbors_add(NULL, "host", 0) != -1) return fail_nei("add NULL zenoh_id");
    if (pm_metal_neighbors_add("zid", NULL, 0) != -1) return fail_nei("add NULL host");
    if (pm_metal_neighbors_seen(NULL) != -1) return fail_nei("seen NULL");
    if (pm_metal_neighbors_drop(NULL) != -1) return fail_nei("drop NULL");
    if (pm_metal_neighbors_find(NULL) != -1) return fail_nei("find NULL");
    return 0;
}

/* 8: reap with NULL pointer returns 0 (no crash) */
static int32_t case_reap_null_pointer(void) {
    uint32_t reaped = pm_metal_neighbors_reap(0);
    (void)reaped;
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.net.neighbors, case_add_one, case_add_one);
PM_MOD_TEST_C(pymergetic.metal.net.neighbors, case_add_idempotent, case_add_idempotent);
PM_MOD_TEST_C(pymergetic.metal.net.neighbors, case_seen, case_seen);
PM_MOD_TEST_C(pymergetic.metal.net.neighbors, case_drop, case_drop);
PM_MOD_TEST_C(pymergetic.metal.net.neighbors, case_find, case_find);
PM_MOD_TEST_C(pymergetic.metal.net.neighbors, case_reap, case_reap);
PM_MOD_TEST_C(pymergetic.metal.net.neighbors, case_null_reject, case_null_reject);
PM_MOD_TEST_C(pymergetic.metal.net.neighbors, case_reap_null_pointer, case_reap_null_pointer);
