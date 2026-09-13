/* pymergetic.metal.net.cloud -- prove the cloud compile job table.
 *
 * Exercises offer/claim/submit/poll state machine, cancel, counters,
 * FNV-1a src_hash, edge cases, and NULL/overflow rejection without
 * requiring a running zenoh session. */
#include "pymergetic/metal/net/cloud/__exports__.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail_cl(const char *why) {
    fprintf(stderr, "metal.net.cloud test: %s\n", why);
    return 1;
}

/* 1: FNV-1a hash produces the same value for the same input */
static int32_t case_fnv1a_deterministic(void) {
    int64_t j1, j2;
    pm_metal_cloud_job_t out;
    const uint8_t source[] = "hello world";

    if (pm_metal_cloud_init(7) != 0) return fail_cl("init");

    j1 = pm_metal_cloud_offer("target-x", source, 11);
    if (j1 < 0) return fail_cl("offer 1");
    j2 = pm_metal_cloud_offer("target-y", source, 11);
    if (j2 < 0) return fail_cl("offer 2");

    if (pm_metal_cloud_job_state((uint64_t)j1, &out) != 0) return fail_cl("job_state 1");
    {
        uint32_t h1 = out.src_hash;
        if (pm_metal_cloud_job_state((uint64_t)j2, &out) != 0) return fail_cl("job_state 2");
        if (h1 != out.src_hash) return fail_cl("src_hash mismatch for same source");
    }
    return 0;
}

/* 2: FNV-1a produces different hashes for different input */
static int32_t case_fnv1a_distinct(void) {
    int64_t j1, j2;
    pm_metal_cloud_job_t out;
    const uint8_t s1[] = "abc";
    const uint8_t s2[] = "abd";

    if (pm_metal_cloud_init(1) != 0) return fail_cl("init");

    j1 = pm_metal_cloud_offer("x", s1, 3);
    if (j1 < 0) return fail_cl("offer 1");
    j2 = pm_metal_cloud_offer("y", s2, 3);
    if (j2 < 0) return fail_cl("offer 2");

    if (pm_metal_cloud_job_state((uint64_t)j1, &out) != 0) return fail_cl("js 1");
    {
        uint32_t h1 = out.src_hash;
        if (pm_metal_cloud_job_state((uint64_t)j2, &out) != 0) return fail_cl("js 2");
        if (h1 == out.src_hash) return fail_cl("src_hash collision for distinct source");
    }
    return 0;
}

/* 3: single job lifecycle: offer -> claim -> submit success */
static int32_t case_job_lifecycle(void) {
    int64_t jid;
    pm_metal_cloud_job_t out;
    const uint8_t src[] = "source code here";
    const uint8_t art[] = {0xde, 0xad, 0xbe, 0xef};

    if (pm_metal_cloud_init(42) != 0) return fail_cl("init");

    /* Offer. */
    jid = pm_metal_cloud_offer("my-target", src, 16);
    if (jid < 0) return fail_cl("offer");
    if (pm_metal_cloud_pending() != 1) return fail_cl("pending after offer");
    if (pm_metal_cloud_running() != 0) return fail_cl("running after offer");

    if (pm_metal_cloud_job_state((uint64_t)jid, &out) != 0) return fail_cl("job_state");
    if (out.state != PM_METAL_CLOUD_JOB_OFFERED) return fail_cl("state not OFFERED");
    if (out.peer_id != 0) return fail_cl("peer_id not 0");

    /* Poll returns 1 (change flag dirty). */
    if (pm_metal_cloud_poll() != 1) return fail_cl("poll after offer");
    /* Second poll returns 0 (no change since). */
    if (pm_metal_cloud_poll() != 0) return fail_cl("poll no change");

    /* Claim by peer 99. */
    if (pm_metal_cloud_claim((uint64_t)jid, 99) != 0) return fail_cl("claim");
    if (pm_metal_cloud_pending() != 1) return fail_cl("pending after claim");
    if (pm_metal_cloud_job_state((uint64_t)jid, &out) != 0) return fail_cl("js after claim");
    if (out.state != PM_METAL_CLOUD_JOB_CLAIMED) return fail_cl("not CLAIMED");
    if (out.peer_id != 99) return fail_cl("peer_id not 99");

    /* Submit artifact (success). */
    if (pm_metal_cloud_submit((uint64_t)jid, art, 4, 0, NULL) != 0) return fail_cl("submit");
    if (pm_metal_cloud_job_state((uint64_t)jid, &out) != 0) return fail_cl("js after submit");
    if (out.state != PM_METAL_CLOUD_JOB_DONE) return fail_cl("not DONE");
    if (out.artifact_len != 4) return fail_cl("artifact_len wrong");
    if (memcmp(out.artifact, art, 4) != 0) return fail_cl("artifact mismatch");
    if (pm_metal_cloud_pending() != 0) return fail_cl("pending after done");

    return 0;
}

/* 4: submit failure path */
static int32_t case_job_fail(void) {
    int64_t jid;
    pm_metal_cloud_job_t out;
    const uint8_t src[] = "fail me";

    if (pm_metal_cloud_init(5) != 0) return fail_cl("init");

    jid = pm_metal_cloud_offer("fail-target", src, 7);
    if (jid < 0) return fail_cl("offer");
    if (pm_metal_cloud_claim((uint64_t)jid, 7) != 0) return fail_cl("claim");

    /* Submit failure. */
    if (pm_metal_cloud_submit((uint64_t)jid, NULL, 0, -1, "compile error") != 0) return fail_cl("submit fail");
    if (pm_metal_cloud_job_state((uint64_t)jid, &out) != 0) return fail_cl("js");
    if (out.state != PM_METAL_CLOUD_JOB_FAILED) return fail_cl("not FAILED");
    if (strstr(out.error, "compile error") == NULL) return fail_cl("error message missing");

    return 0;
}

/* 5: cancel an offered job */
static int32_t case_cancel(void) {
    int64_t jid;
    pm_metal_cloud_job_t out;
    const uint8_t src[] = "to cancel";

    if (pm_metal_cloud_init(3) != 0) return fail_cl("init");

    jid = pm_metal_cloud_offer("cancel-me", src, 9);
    if (jid < 0) return fail_cl("offer");
    if (pm_metal_cloud_cancel((uint64_t)jid) != 0) return fail_cl("cancel");

    /* Job is gone. */
    if (pm_metal_cloud_job_state((uint64_t)jid, &out) != -1) return fail_cl("js after cancel");
    /* Cancel a terminal job returns 0. */
    jid = pm_metal_cloud_offer("cancel-me-2", src, 9);
    if (jid < 0) return fail_cl("offer 2");
    if (pm_metal_cloud_claim((uint64_t)jid, 1) != 0) return fail_cl("claim 2");
    if (pm_metal_cloud_submit((uint64_t)jid, NULL, 0, -1, "err") != 0) return fail_cl("submit fail 2");
    /* Now it's FAILED (terminal); cancel returns 0. */
    if (pm_metal_cloud_cancel((uint64_t)jid) != 0) return fail_cl("cancel terminal");

    return 0;
}

/* 6: NULL/empty rejection */
static int32_t case_null_reject(void) {
    const uint8_t src[] = "x";

    if (pm_metal_cloud_init(1) != 0) return fail_cl("init");

    if (pm_metal_cloud_offer(NULL, src, 1) != -1) return fail_cl("offer NULL target");
    if (pm_metal_cloud_offer("", src, 1) != -1) return fail_cl("offer empty target");
    if (pm_metal_cloud_offer("x", NULL, 1) != -1) return fail_cl("offer NULL source nonzero len");
    /* Peer 0 is localhost, but a missing job is still refused. */
    if (pm_metal_cloud_claim(0, 0) != -1) return fail_cl("claim missing local job");
    if (pm_metal_cloud_claim(1, 1) != -1) return fail_cl("claim nonexistent job");
    if (pm_metal_cloud_job_state(1, NULL) != -1) return fail_cl("job_state NULL out");

    return 0;
}

/* 7: overflow -- fill to max and the next offer is refused */
static int32_t case_overflow(void) {
    char tgt[64];
    uint32_t i;
    const uint8_t src[] = "x";

    if (pm_metal_cloud_init(1) != 0) return fail_cl("init");

    for (i = 0; i < PM_METAL_CLOUD_JOBS_MAX; i++) {
        snprintf(tgt, sizeof(tgt), "t%u", (unsigned)i);
        if (pm_metal_cloud_offer(tgt, src, 1) < 0) return fail_cl("offer fill");
    }
    /* One more should fail. */
    if (pm_metal_cloud_offer("overflow", src, 1) != -1) return fail_cl("overflow not refused");
    return 0;
}

/* 8: counters track pending/running correctly */
static int32_t case_counters(void) {
    int64_t j1, j2, j3;
    const uint8_t src[] = "counter test";

    if (pm_metal_cloud_init(9) != 0) return fail_cl("init");

    j1 = pm_metal_cloud_offer("t1", src, 12);
    j2 = pm_metal_cloud_offer("t2", src, 12);
    j3 = pm_metal_cloud_offer("t3", src, 12);
    if (j1 < 0 || j2 < 0 || j3 < 0) return fail_cl("offers");

    if (pm_metal_cloud_pending() != 3) return fail_cl("pending 3");
    if (pm_metal_cloud_running() != 0) return fail_cl("running 0");

    if (pm_metal_cloud_claim((uint64_t)j1, 99) != 0) return fail_cl("claim j1");
    if (pm_metal_cloud_pending() != 3) return fail_cl("pending after claim");
    if (pm_metal_cloud_running() != 0) return fail_cl("running still 0");

    if (pm_metal_cloud_submit((uint64_t)j1, NULL, 0, -1, "err") != 0) return fail_cl("fail j1");
    if (pm_metal_cloud_pending() != 2) return fail_cl("pending after fail 2");
    if (pm_metal_cloud_running() != 0) return fail_cl("running after fail 0");

    if (pm_metal_cloud_claim((uint64_t)j2, 88) != 0) return fail_cl("claim j2");
    if (pm_metal_cloud_cancel((uint64_t)j3) != 0) return fail_cl("cancel j3");
    if (pm_metal_cloud_pending() != 1) return fail_cl("pending after cancel");

    return 0;
}

/* 9: claim a non-OFFERED job is refused */
static int32_t case_claim_bad_state(void) {
    int64_t jid;
    const uint8_t src[] = "bad";

    if (pm_metal_cloud_init(2) != 0) return fail_cl("init");

    jid = pm_metal_cloud_offer("bad", src, 3);
    if (jid < 0) return fail_cl("offer");
    if (pm_metal_cloud_claim((uint64_t)jid, 2) != 0) return fail_cl("claim 1");
    /* Claim the same job again -- already CLAIMED, not OFFERED. */
    if (pm_metal_cloud_claim((uint64_t)jid, 3) != -2) return fail_cl("claim bad state not refused");
    return 0;
}

/* 10: large artifact is rejected */
static int32_t case_artifact_too_large(void) {
    int64_t jid;
    pm_metal_cloud_job_t out;
    const uint8_t src[] = "large";
    uint8_t big_art[PM_METAL_CLOUD_ARTIFACT_MAX + 1];

    memset(big_art, 0xAA, sizeof(big_art));

    if (pm_metal_cloud_init(1) != 0) return fail_cl("init");

    jid = pm_metal_cloud_offer("large-tgt", src, 5);
    if (jid < 0) return fail_cl("offer");
    if (pm_metal_cloud_claim((uint64_t)jid, 1) != 0) return fail_cl("claim");

    if (pm_metal_cloud_submit((uint64_t)jid, big_art, sizeof(big_art), 0, NULL) != 0) return fail_cl("submit large");
    if (pm_metal_cloud_job_state((uint64_t)jid, &out) != 0) return fail_cl("js");
    if (out.state != PM_METAL_CLOUD_JOB_FAILED) return fail_cl("not FAILED for too-large artifact");
    return 0;
}

/* 11: deinit resets the table */
static int32_t case_deinit_reset(void) {
    const uint8_t src[] = "reset";

    if (pm_metal_cloud_init(4) != 0) return fail_cl("init");
    (void)pm_metal_cloud_offer("r1", src, 5);
    if (pm_metal_cloud_pending() != 1) return fail_cl("pending before deinit");
    pm_metal_cloud_deinit();
    /* After deinit, counters are zero and stale job_ids unknown. */
    if (pm_metal_cloud_pending() != 0) return fail_cl("pending after deinit");
    if (pm_metal_cloud_running() != 0) return fail_cl("running after deinit");
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.net.cloud, case_fnv1a_deterministic, case_fnv1a_deterministic);
PM_MOD_TEST_C(pymergetic.metal.net.cloud, case_fnv1a_distinct, case_fnv1a_distinct);
PM_MOD_TEST_C(pymergetic.metal.net.cloud, case_job_lifecycle, case_job_lifecycle);
PM_MOD_TEST_C(pymergetic.metal.net.cloud, case_job_fail, case_job_fail);
PM_MOD_TEST_C(pymergetic.metal.net.cloud, case_cancel, case_cancel);
PM_MOD_TEST_C(pymergetic.metal.net.cloud, case_null_reject, case_null_reject);
PM_MOD_TEST_C(pymergetic.metal.net.cloud, case_overflow, case_overflow);
PM_MOD_TEST_C(pymergetic.metal.net.cloud, case_counters, case_counters);
PM_MOD_TEST_C(pymergetic.metal.net.cloud, case_claim_bad_state, case_claim_bad_state);
PM_MOD_TEST_C(pymergetic.metal.net.cloud, case_artifact_too_large, case_artifact_too_large);
PM_MOD_TEST_C(pymergetic.metal.net.cloud, case_deinit_reset, case_deinit_reset);