/* pymergetic.metal.net.cloud -- remote compilation delegation (cloud compile).
 *
 * A seat can offer compilation jobs to the fleet via zenoh pub/sub. Peers
 * with matching compile capability auto-claim jobs, compile them, and
 * submit artifacts back. Jobs are stored in a static array bounded by
 * PM_UTIL_LIMIT_C (max 16 concurrent). FNV-1a hash identifies source for
 * cache matching across peers.
 */
#ifndef PYMERGETIC_METAL_NET_CLOUD_TYPES_H
#define PYMERGETIC_METAL_NET_CLOUD_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_CLOUD_TARGET_MAX 128
#define PM_METAL_CLOUD_ERR_MAX 256
#define PM_METAL_CLOUD_ARTIFACT_MAX (512u * 1024u) /* 512KB max artifact */

#ifndef PM_METAL_CLOUD_JOBS_MAX
#define PM_METAL_CLOUD_JOBS_MAX 16u
#endif

typedef enum pm_metal_cloud_job_state {
    PM_METAL_CLOUD_JOB_IDLE = 0,
    PM_METAL_CLOUD_JOB_OFFERED = 1,    /* broadcast to fleet */
    PM_METAL_CLOUD_JOB_CLAIMED = 2,    /* a peer took it */
    PM_METAL_CLOUD_JOB_RUNNING = 3,     /* peer is compiling */
    PM_METAL_CLOUD_JOB_DONE = 4,        /* artifact available */
    PM_METAL_CLOUD_JOB_FAILED = 5,      /* compilation failed */
} pm_metal_cloud_job_state_t;

typedef struct pm_metal_cloud_job {
    uint64_t job_id;
    uint32_t peer_id;           /* peer that claimed it (0 = unclaimed) */
    char target[PM_METAL_CLOUD_TARGET_MAX];  /* what to compile */
    uint32_t src_hash;          /* hash of source (for cache matching) */
    pm_metal_cloud_job_state_t state;
    uint8_t artifact[PM_METAL_CLOUD_ARTIFACT_MAX];
    uint32_t artifact_len;
    char error[PM_METAL_CLOUD_ERR_MAX];
    int64_t started_us;         /* when compilation started */
    int64_t finished_us;        /* when it finished */
} pm_metal_cloud_job_t;

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_CLOUD_TYPES_H */