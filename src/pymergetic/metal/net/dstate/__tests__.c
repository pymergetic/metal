/* pymergetic.metal.net.dstate — border prove. Exercises set/get/del/recv
 * lifecycle, merge handler, Lamport clock evolution, tiebreaker, edge
 * cases, and the data structures without requiring a running zenoh session. */
#include "pymergetic/metal/net/dstate/__exports__.h"
#include "pymergetic/wasmmod/guest.h" /* PM_MOD_TEST_C */
#include "pymergetic/util/limits.h"
#include "pymergetic/metal/net/dstate.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail_ds(const char *why) {
    fprintf(stderr, "metal.net.dstate test: %s\n", why);
    return 1;
}

/* Merge handler: concatenates local and incoming values with a "+" separator. */
static int32_t concat_merge(const uint8_t *local_val, uint32_t local_len,
    const uint8_t *incoming_val, uint32_t incoming_len,
    uint8_t *merged, uint32_t *merged_len,
    void *user) {
    uint32_t total;
    (void)user;
    if (merged_len == NULL) {
        return -1;
    }
    total = local_len + incoming_len + 1; /* +1 for '+' */
    if (total > *merged_len) {
        return -1;
    }
    if (local_len > 0 && local_val != NULL) {
        memcpy(merged, local_val, local_len);
    }
    merged[local_len] = '+';
    if (incoming_len > 0 && incoming_val != NULL) {
        memcpy(merged + local_len + 1, incoming_val, incoming_len);
    }
    *merged_len = total;
    return 0;
}

/* Merge handler that always fails. */
static int32_t fail_merge(const uint8_t *local_val, uint32_t local_len,
    const uint8_t *incoming_val, uint32_t incoming_len,
    uint8_t *merged, uint32_t *merged_len,
    void *user) {
    (void)local_val;
    (void)local_len;
    (void)incoming_val;
    (void)incoming_len;
    (void)merged;
    (void)merged_len;
    (void)user;
    return -1;
}

static int32_t case_dstate_single(void) {
    uint8_t buf[PM_METAL_DSTATE_VALUE_MAX];
    uint32_t buflen;

    /* Init. */
    if (pm_metal_dstate_init(1) != 0) {
        return fail_ds("init");
    }

    /* Set/get roundtrip. */
    if (pm_metal_dstate_set("foo", (const uint8_t *)"bar", 3) != 0) {
        return fail_ds("set foo");
    }
    buflen = sizeof(buf);
    memset(buf, 0, buflen);
    if (pm_metal_dstate_get("foo", buf, &buflen) != 0) {
        return fail_ds("get foo");
    }
    if (buflen != 3 || memcmp(buf, "bar", 3) != 0) {
        return fail_ds("get foo value");
    }
    if (pm_metal_dstate_count() != 1) {
        return fail_ds("count after set");
    }

    /* Get missing key. */
    buflen = sizeof(buf);
    if (pm_metal_dstate_get("nope", buf, &buflen) != -1) {
        return fail_ds("get missing");
    }

    /* Get with too-small buffer. */
    buflen = 2;
    if (pm_metal_dstate_get("foo", buf, &buflen) != -2) {
        return fail_ds("get foo small buf");
    }

    /* Overwrite existing key. */
    if (pm_metal_dstate_set("foo", (const uint8_t *)"quux", 4) != 0) {
        return fail_ds("set foo overwrite");
    }
    buflen = sizeof(buf);
    memset(buf, 0, buflen);
    if (pm_metal_dstate_get("foo", buf, &buflen) != 0) {
        return fail_ds("get foo after overwrite");
    }
    if (buflen != 4 || memcmp(buf, "quux", 4) != 0) {
        return fail_ds("get foo overwrite value");
    }

    /* Delete existing key. */
    if (pm_metal_dstate_del("foo") != 0) {
        return fail_ds("del foo");
    }
    if (pm_metal_dstate_count() != 0) {
        return fail_ds("count after del");
    }
    buflen = sizeof(buf);
    if (pm_metal_dstate_get("foo", buf, &buflen) != -1) {
        return fail_ds("get after del");
    }

    /* Delete missing key. */
    if (pm_metal_dstate_del("nope") != -1) {
        return fail_ds("del missing");
    }

    /* Misuse guards. */
    if (pm_metal_dstate_set(NULL, (const uint8_t *)"x", 1) != -1) {
        return fail_ds("set null key");
    }
    if (pm_metal_dstate_set("", (const uint8_t *)"x", 1) != -1) {
        return fail_ds("set empty key");
    }
    if (pm_metal_dstate_set("x", NULL, 1) != -1) {
        return fail_ds("set null value nonzero len");
    }
    if (pm_metal_dstate_get(NULL, buf, &buflen) != -1) {
        return fail_ds("get null key");
    }
    if (pm_metal_dstate_del(NULL) != -1) {
        return fail_ds("del null key");
    }
    if (pm_metal_dstate_del("") != -1) {
        return fail_ds("del empty key");
    }

    return 0;
}

static int32_t case_dstate_recv(void) {
    uint8_t buf[PM_METAL_DSTATE_VALUE_MAX];
    uint32_t buflen;
    int32_t rc;

    /* Fresh init. */
    if (pm_metal_dstate_init(1) != 0) {
        return fail_ds("init recv");
    }

    /* Set a local value. */
    if (pm_metal_dstate_set("alpha", (const uint8_t *)"a", 1) != 0) {
        return fail_ds("set alpha");
    }

    /* Receive an update from a peer with a higher version: LWW overwrite. */
    rc = pm_metal_dstate_recv("alpha", (const uint8_t *)"b", 1, 10, 2);
    if (rc != 0) {
        return fail_ds("recv alpha new");
    }
    buflen = sizeof(buf);
    memset(buf, 0, buflen);
    rc = pm_metal_dstate_get("alpha", buf, &buflen);
    if (rc != 0 || buflen != 1 || buf[0] != 'b') {
        return fail_ds("recv alpha value");
    }

    /* Receive an older version: keep local. */
    rc = pm_metal_dstate_recv("alpha", (const uint8_t *)"old", 3, 5, 3);
    if (rc != 0) {
        return fail_ds("recv alpha old");
    }
    buflen = sizeof(buf);
    memset(buf, 0, buflen);
    rc = pm_metal_dstate_get("alpha", buf, &buflen);
    if (rc != 0 || buflen != 1 || buf[0] != 'b') {
        return fail_ds("recv alpha kept local");
    }

    /* Receive same version, higher peer_id: overwrite. */
    rc = pm_metal_dstate_recv("alpha", (const uint8_t *)"c", 1, 10, 3);
    if (rc != 0) {
        return fail_ds("recv alpha same ver higher peer");
    }
    buflen = sizeof(buf);
    memset(buf, 0, buflen);
    rc = pm_metal_dstate_get("alpha", buf, &buflen);
    if (rc != 0 || buflen != 1 || buf[0] != 'c') {
        return fail_ds("recv alpha higher peer value");
    }

    /* Receive same version, lower peer_id: keep local. */
    rc = pm_metal_dstate_recv("alpha", (const uint8_t *)"d", 1, 10, 1);
    if (rc != 0) {
        return fail_ds("recv alpha same ver lower peer");
    }
    buflen = sizeof(buf);
    memset(buf, 0, buflen);
    rc = pm_metal_dstate_get("alpha", buf, &buflen);
    if (rc != 0 || buflen != 1 || buf[0] != 'c') {
        return fail_ds("recv alpha lower peer value");
    }

    /* Receive a brand new key from a peer. */
    rc = pm_metal_dstate_recv("beta", (const uint8_t *)"remote", 6, 20, 2);
    if (rc != 0) {
        return fail_ds("recv beta new");
    }
    buflen = sizeof(buf);
    memset(buf, 0, buflen);
    rc = pm_metal_dstate_get("beta", buf, &buflen);
    if (rc != 0 || buflen != 6 || memcmp(buf, "remote", 6) != 0) {
        return fail_ds("recv beta value");
    }

    /* Ignore own echo (from_peer == local peer_id). */
    buflen = sizeof(buf);
    memset(buf, 0, buflen);
    rc = pm_metal_dstate_recv("alpha", (const uint8_t *)"echo", 4, 100, 1);
    if (rc != 0) {
        return fail_ds("recv own echo");
    }
    rc = pm_metal_dstate_get("alpha", buf, &buflen);
    if (rc != 0 || buflen != 1 || buf[0] != 'c') {
        return fail_ds("recv own echo value unchanged");
    }

    /* Misuse: NULL key/value. */
    if (pm_metal_dstate_recv(NULL, (const uint8_t *)"x", 1, 1, 2) != -1) {
        return fail_ds("recv null key");
    }
    if (pm_metal_dstate_recv("x", NULL, 1, 1, 2) != -1) {
        return fail_ds("recv null value nonzero len");
    }

    return 0;
}

static int32_t case_dstate_merge(void) {
    uint8_t buf[PM_METAL_DSTATE_VALUE_MAX];
    uint32_t buflen;
    int32_t rc;

    /* Fresh init. */
    if (pm_metal_dstate_init(1) != 0) {
        return fail_ds("init merge");
    }

    /* Register a merge handler for "count" prefix. */
    if (pm_metal_dstate_merge_register("count", concat_merge, NULL) != 0) {
        return fail_ds("merge register count");
    }

    /* Set local value. */
    if (pm_metal_dstate_set("count-a", (const uint8_t *)"AAA", 3) != 0) {
        return fail_ds("set count-a local");
    }

    /* Receive same version from a lower peer: merge fires. */
    /* First, note local version after set. */
    {
        pm_metal_dstate_entry_t ent;
        if (pm_metal_dstate_at(0, &ent) != 0) {
            return fail_ds("at 0");
        }
        /* Receive same version from a different peer: merge via concat_merge. */
        rc = pm_metal_dstate_recv("count-a", (const uint8_t *)"BBB", 3, ent.version, 2);
        if (rc != 0) {
            return fail_ds("recv count-a merge");
        }
        buflen = sizeof(buf);
        memset(buf, 0, buflen);
        rc = pm_metal_dstate_get("count-a", buf, &buflen);
        if (rc != 0) {
            return fail_ds("get count-a after merge");
        }
        /* Expected: "AAA+BBB" = 7 bytes. */
        if (buflen != 7 || memcmp(buf, "AAA+BBB", 7) != 0) {
            return fail_ds("merge count-a value");
        }
    }

    /* Newer version skips merge: LWW overwrites. */
    rc = pm_metal_dstate_recv("count-a", (const uint8_t *)"Z", 1, 999, 3);
    if (rc != 0) {
        return fail_ds("recv count-a newer");
    }
    buflen = sizeof(buf);
    memset(buf, 0, buflen);
    rc = pm_metal_dstate_get("count-a", buf, &buflen);
    if (rc != 0 || buflen != 1 || buf[0] != 'Z') {
        return fail_ds("count-a newer LWW value");
    }

    /* Merge handler that fails: keep local. */
    {
        pm_metal_dstate_entry_t ent;
        uint32_t i;
        uint32_t n;
        int32_t found = 0;
        (void)pm_metal_dstate_merge_register("fail", fail_merge, NULL);
        if (pm_metal_dstate_set("fail-x", (const uint8_t *)"keep", 4) != 0) {
            return fail_ds("set fail-x");
        }
        n = pm_metal_dstate_count();
        for (i = 0; i < n; i++) {
            if (pm_metal_dstate_at(i, &ent) != 0) {
                return fail_ds("at iter");
            }
            if (strcmp(ent.key, "fail-x") == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            return fail_ds("find fail-x entry");
        }
        rc = pm_metal_dstate_recv("fail-x", (const uint8_t *)"drop", 4, ent.version, 2);
        if (rc != 0) {
            return fail_ds("recv fail-x");
        }
        buflen = sizeof(buf);
        memset(buf, 0, buflen);
        rc = pm_metal_dstate_get("fail-x", buf, &buflen);
        if (rc != 0 || buflen != 4 || memcmp(buf, "keep", 4) != 0) {
            return fail_ds("fail-x merge fail kept local");
        }
    }

    /* Misuse: merge_register NULL prefix/fn. */
    if (pm_metal_dstate_merge_register(NULL, concat_merge, NULL) != -1) {
        return fail_ds("merge register null prefix");
    }
    if (pm_metal_dstate_merge_register("", concat_merge, NULL) != -1) {
        return fail_ds("merge register empty prefix");
    }
    if (pm_metal_dstate_merge_register("x", NULL, NULL) != -1) {
        return fail_ds("merge register null fn");
    }

    return 0;
}

static int32_t case_dstate_at_publish(void) {
    pm_metal_dstate_entry_t ent;

    if (pm_metal_dstate_init(2) != 0) {
        return fail_ds("init at");
    }

    /* Fill entries. */
    if (pm_metal_dstate_set("a", (const uint8_t *)"1", 1) != 0) {
        return fail_ds("set a");
    }
    if (pm_metal_dstate_set("b", (const uint8_t *)"22", 2) != 0) {
        return fail_ds("set b");
    }
    if (pm_metal_dstate_set("c", (const uint8_t *)"333", 3) != 0) {
        return fail_ds("set c");
    }
    if (pm_metal_dstate_count() != 3) {
        return fail_ds("count 3");
    }

    /* at(): iterate. */
    if (pm_metal_dstate_at(0, &ent) != 0) {
        return fail_ds("at 0");
    }
    if (strcmp(ent.key, "a") != 0 || ent.value_len != 1) {
        return fail_ds("at 0 key/value");
    }
    if (pm_metal_dstate_at(3, &ent) != -1) {
        return fail_ds("at past end");
    }
    if (pm_metal_dstate_at(0, NULL) != -1) {
        return fail_ds("at null out");
    }

    /* publish(). */
    if (pm_metal_dstate_publish() != 3) {
        return fail_ds("publish count");
    }

    /* Fill to max. */
    {
        char kbuf[64];
        uint32_t i;
        for (i = 0; i < PM_METAL_DSTATE_ENTRIES_MAX - 3; i++) {
            snprintf(kbuf, sizeof(kbuf), "k%u", (unsigned)i);
            if (pm_metal_dstate_set(kbuf, (const uint8_t *)"v", 1) != 0) {
                return fail_ds("fill entries");
            }
        }
        if (pm_metal_dstate_count() != PM_METAL_DSTATE_ENTRIES_MAX) {
            return fail_ds("count max");
        }
        /* One more should fail. */
        if (pm_metal_dstate_set("overflow", (const uint8_t *)"o", 1) != -1) {
            return fail_ds("overflow set");
        }
    }

    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.net.dstate, case_dstate_single, case_dstate_single);
PM_MOD_TEST_C(pymergetic.metal.net.dstate, case_dstate_recv, case_dstate_recv);
PM_MOD_TEST_C(pymergetic.metal.net.dstate, case_dstate_merge, case_dstate_merge);
PM_MOD_TEST_C(pymergetic.metal.net.dstate, case_dstate_at_publish, case_dstate_at_publish);