/* pymergetic.metal.net.dstate — distributed state synchronisation.
 *
 * Every peer holds a local replica of shared key-value state. Updates are
 * versioned with a Lamport clock (peer_id + counter) so a receiver can tell
 * whether an incoming value is newer than its own. The default policy is
 * last-writer-wins; a merge callback registered for a key prefix overrides it
 * so two concurrent updates can be combined rather than one clobbering the
 * other. Updates are published via zenoh put on "dstate/<key>" and every
 * peer subscribes to "dstate/ *" (wildcard) to receive them.
 *
 * Storage is a static array (PM_UTIL_LIMIT_C, max 64 entries). One card, one
 * language (C), one zenoh session.
 */
#ifndef PYMERGETIC_METAL_NET_DSTATE_TYPES_H
#define PYMERGETIC_METAL_NET_DSTATE_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_DSTATE_KEY_MAX 128
#define PM_METAL_DSTATE_VALUE_MAX 4096
#ifndef PM_METAL_DSTATE_ENTRIES_MAX
#define PM_METAL_DSTATE_ENTRIES_MAX 64u
#endif
#ifndef PM_METAL_DSTATE_MERGES_MAX
#define PM_METAL_DSTATE_MERGES_MAX 8u
#endif

typedef enum pm_metal_dstate_op {
    PM_METAL_DSTATE_OP_SET = 0,
    PM_METAL_DSTATE_OP_DEL = 1,
    PM_METAL_DSTATE_OP_MERGE = 2,
} pm_metal_dstate_op_t;

/* One replicated key-value entry. */
typedef struct pm_metal_dstate_entry {
    char key[PM_METAL_DSTATE_KEY_MAX];
    uint8_t value[PM_METAL_DSTATE_VALUE_MAX];
    uint32_t value_len;
    uint64_t version;   /* Lamport clock */
    uint32_t peer_id;   /* origin peer */
} pm_metal_dstate_entry_t;

/* Merge callback: given local and incoming values, produce a merged value.
 * merged_len is in/out: the handler writes at most *merged_len bytes into
 * merged and sets *merged_len to the actual byte count. Returns 0 on
 * success. */
typedef int32_t (*pm_metal_dstate_merge_fn)(
    const uint8_t *local_val, uint32_t local_len,
    const uint8_t *incoming_val, uint32_t incoming_len,
    uint8_t *merged, uint32_t *merged_len,
    void *user);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_DSTATE_TYPES_H */