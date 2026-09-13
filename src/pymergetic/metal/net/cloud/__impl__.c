/* pymergetic.metal.net.cloud -- remote compilation delegation.
 *
 * Stores jobs in a static array (PM_UTIL_LIMIT_C, max 16). Job IDs are a
 * monotonic counter. src_hash is an FNV-1a 32-bit hash of the source bytes
 * for cache matching across peers. Each job carries a state machine:
 * IDLE -> OFFERED (broadcast) -> CLAIMED (peer took it) -> RUNNING/DONE/FAILED.
 *
 * The zenoh wire (publish to "cloud/jobs/OFFER", subscribe to "cloud/jobs/ *",
 * CLAIM and RESULT pub/sub) is plumbed when a zenoh session is available.
 * For now the data structures and local state machine are provable.
 *
 * All storage is static -- no arena dependency. The peer_id is set at init.
 */
#include "pymergetic/metal/net/cloud/__exports__.h"

#include "pymergetic/util/limits.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#ifndef PM_METAL_CLOUD_JOBS_MAX
#define PM_METAL_CLOUD_JOBS_MAX 16u
#endif

/* ---------------- FNV-1a 32-bit hash ---------------- */
static uint32_t fnv1a_32(const uint8_t *data, uint32_t len) {
    uint32_t hash = 2166136261u;
    uint32_t i;
    if (data == NULL) {
        return hash;
    }
    for (i = 0; i < len; i++) {
        hash ^= (uint32_t)data[i];
        hash *= 16777619u;
    }
    return hash;
}

/* ---------------- job storage ---------------- */
static pm_metal_cloud_job_t s_jobs[PM_METAL_CLOUD_JOBS_MAX];
static uint32_t s_n; /* live job count */
static uint64_t s_next_job_id = 1;
static uint32_t s_peer_id;
static int32_t s_has_change; /* dirty flag set by transitions, cleared by poll */

PM_UTIL_LIMIT_C(pm_cloud_jobs_limit, pymergetic.metal.net.cloud, jobs,
    PM_METAL_CLOUD_JOBS_MAX, 0u, &s_n);

/* ---------------- lifecycle ---------------- */
int32_t pm_metal_cloud_init(uint32_t peer_id) {
    s_peer_id = peer_id;
    (void)s_peer_id;
    s_n = 0;
    s_next_job_id = 1;
    s_has_change = 0;
    memset(s_jobs, 0, sizeof(s_jobs));
    return 0;
}

void pm_metal_cloud_deinit(void) {
    s_n = 0;
    s_next_job_id = 1;
    s_has_change = 0;
    (void)s_peer_id; /* wired by zenoh bridge later */
    memset(s_jobs, 0, sizeof(s_jobs));
}

/* ---------------- find job by id ---------------- */
static int32_t find_job(uint64_t job_id) {
    uint32_t i;
    for (i = 0; i < s_n; i++) {
        if (s_jobs[i].state != PM_METAL_CLOUD_JOB_IDLE &&
            s_jobs[i].job_id == job_id) {
            return (int32_t)i;
        }
    }
    return -1;
}

/* ---------------- offer ---------------- */
int64_t pm_metal_cloud_offer(const char *target, const uint8_t *source, uint32_t source_len) {
    uint32_t src_hash;
    if (target == NULL || target[0] == '\0' || (source_len && source == NULL)) {
        return -1;
    }
    if (s_n >= PM_METAL_CLOUD_JOBS_MAX) {
        return -1;
    }
    src_hash = fnv1a_32(source, source_len);
    memset(&s_jobs[s_n], 0, sizeof(s_jobs[s_n]));
    s_jobs[s_n].job_id = s_next_job_id++;
    /* OFFERED state means unclaimed. peer_id 0 is the loopback host, the
     * same localhost convention used by the services registry. */
    s_jobs[s_n].peer_id = 0;
    strncpy(s_jobs[s_n].target, target, sizeof(s_jobs[s_n].target) - 1);
    s_jobs[s_n].target[sizeof(s_jobs[s_n].target) - 1] = '\0';
    s_jobs[s_n].src_hash = src_hash;
    s_jobs[s_n].state = PM_METAL_CLOUD_JOB_OFFERED;
    s_jobs[s_n].artifact_len = 0;
    s_jobs[s_n].started_us = 0;
    s_jobs[s_n].finished_us = 0;
    s_n++;
    s_has_change = 1;
    return (int64_t)s_jobs[s_n - 1].job_id;
}

/* ---------------- claim ---------------- */
int32_t pm_metal_cloud_claim(uint64_t job_id, uint32_t peer_id) {
    int32_t idx;
    idx = find_job(job_id);
    if (idx < 0) {
        return -1; /* not found */
    }
    if (s_jobs[idx].state != PM_METAL_CLOUD_JOB_OFFERED) {
        return -2; /* not in offerable state */
    }
    s_jobs[idx].peer_id = peer_id;
    s_jobs[idx].state = PM_METAL_CLOUD_JOB_CLAIMED;
    s_has_change = 1;
    return 0;
}

/* ---------------- submit artifact ---------------- */
int32_t pm_metal_cloud_submit(uint64_t job_id, const uint8_t *artifact, uint32_t art_len,
    int32_t status, const char *error) {
    int32_t idx;
    idx = find_job(job_id);
    if (idx < 0) {
        return -1; /* not found */
    }
    if (s_jobs[idx].state != PM_METAL_CLOUD_JOB_CLAIMED &&
        s_jobs[idx].state != PM_METAL_CLOUD_JOB_RUNNING) {
        return -2; /* not in the right state */
    }
    /* Mark running on submit (peer started compiling). */
    if (s_jobs[idx].state == PM_METAL_CLOUD_JOB_CLAIMED) {
        s_jobs[idx].state = PM_METAL_CLOUD_JOB_RUNNING;
    }
    if (status == 0 && artifact != NULL && art_len > 0) {
        if (art_len <= PM_METAL_CLOUD_ARTIFACT_MAX) {
            memcpy(s_jobs[idx].artifact, artifact, art_len);
            s_jobs[idx].artifact_len = art_len;
            s_jobs[idx].state = PM_METAL_CLOUD_JOB_DONE;
        } else {
            /* Artifact too large. */
            s_jobs[idx].state = PM_METAL_CLOUD_JOB_FAILED;
            s_jobs[idx].error[0] = '\0';
            snprintf(s_jobs[idx].error, PM_METAL_CLOUD_ERR_MAX,
                "artifact %u bytes exceeds %u max", (unsigned)art_len,
                (unsigned)PM_METAL_CLOUD_ARTIFACT_MAX);
        }
    } else {
        s_jobs[idx].state = PM_METAL_CLOUD_JOB_FAILED;
        if (error != NULL) {
            strncpy(s_jobs[idx].error, error, PM_METAL_CLOUD_ERR_MAX - 1);
            s_jobs[idx].error[PM_METAL_CLOUD_ERR_MAX - 1] = '\0';
        } else {
            s_jobs[idx].error[0] = '\0';
        }
    }
    s_jobs[idx].finished_us = 0;
    s_has_change = 1;
    return 0;
}

/* ---------------- poll ---------------- */
int32_t pm_metal_cloud_poll(void) {
    (void)s_peer_id;
    if (s_has_change) {
        s_has_change = 0;
        return 1;
    }
    return 0;
}

/* ---------------- job state query ---------------- */
int32_t pm_metal_cloud_job_state(uint64_t job_id, pm_metal_cloud_job_t *out) {
    int32_t idx;
    if (out == NULL) {
        return -1;
    }
    idx = find_job(job_id);
    if (idx < 0) {
        return -1; /* not found */
    }
    *out = s_jobs[idx];
    return 0;
}

/* ---------------- cancel ---------------- */
int32_t pm_metal_cloud_cancel(uint64_t job_id) {
    int32_t idx;
    idx = find_job(job_id);
    if (idx < 0) {
        return -1; /* not found */
    }
    if (s_jobs[idx].state == PM_METAL_CLOUD_JOB_DONE ||
        s_jobs[idx].state == PM_METAL_CLOUD_JOB_FAILED) {
        return 0; /* already terminal */
    }
    /* Compact: swap with last */
    s_n--;
    if (idx < (int32_t)s_n) {
        s_jobs[idx] = s_jobs[s_n];
    }
    memset(&s_jobs[s_n], 0, sizeof(s_jobs[s_n]));
    s_has_change = 1;
    return 0;
}

/* ---------------- counters ---------------- */
uint32_t pm_metal_cloud_pending(void) {
    uint32_t i;
    uint32_t n = 0;
    for (i = 0; i < s_n; i++) {
        if (s_jobs[i].state == PM_METAL_CLOUD_JOB_OFFERED ||
            s_jobs[i].state == PM_METAL_CLOUD_JOB_CLAIMED) {
            n++;
        }
    }
    return n;
}

uint32_t pm_metal_cloud_running(void) {
    uint32_t i;
    uint32_t n = 0;
    for (i = 0; i < s_n; i++) {
        if (s_jobs[i].state == PM_METAL_CLOUD_JOB_RUNNING) {
            n++;
        }
    }
    return n;
}

uint32_t pm_metal_cloud_count(void) {
    return s_n;
}

pm_metal_cloud_job_t *pm_metal_cloud_at(uint32_t idx) {
    return (idx < s_n) ? &s_jobs[idx] : NULL;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.net.cloud, pm_metal_cloud_init, pm_metal_cloud_init, int32_t(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.cloud, pm_metal_cloud_offer, pm_metal_cloud_offer, int64_t(const char *, const uint8_t *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.cloud, pm_metal_cloud_claim, pm_metal_cloud_claim, int32_t(uint64_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.cloud, pm_metal_cloud_submit, pm_metal_cloud_submit, int32_t(uint64_t, const uint8_t *, uint32_t, int32_t, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.net.cloud, pm_metal_cloud_poll, pm_metal_cloud_poll, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.cloud, pm_metal_cloud_job_state, pm_metal_cloud_job_state, int32_t(uint64_t, pm_metal_cloud_job_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.cloud, pm_metal_cloud_cancel, pm_metal_cloud_cancel, int32_t(uint64_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.cloud, pm_metal_cloud_pending, pm_metal_cloud_pending, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.cloud, pm_metal_cloud_running, pm_metal_cloud_running, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.cloud, pm_metal_cloud_deinit, pm_metal_cloud_deinit, void(void));