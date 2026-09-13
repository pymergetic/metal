/* pymergetic.metal.net.rpc — remote procedure calls on a net.zenoh session.
 *
 * An RPC handler is a callback registered under a function key; a caller
 * invokes a key on a peer via a zenoh queryable at "rpc/<key>". The peer's
 * queryable handler deserialises args, dispatches to the registered handler,
 * and the handler's result bytes are the queryable reply. Pending outgoing
 * calls (invoke that hasn't received a result yet) sit in a small ring buffer
 * so the caller can poll for completion. One card, one language (C), one
 * zenoh session.
 */
#ifndef PYMERGETIC_METAL_NET_RPC_TYPES_H
#define PYMERGETIC_METAL_NET_RPC_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_RPC_KEY_MAX 128
#define PM_METAL_RPC_ARG_MAX 4096
#define PM_METAL_RPC_RESULT_MAX 4096
#ifndef PM_METAL_RPC_HANDLERS_MAX
#define PM_METAL_RPC_HANDLERS_MAX 16u
#endif
#ifndef PM_METAL_RPC_CALLS_MAX
#define PM_METAL_RPC_CALLS_MAX 32u
#endif

/* An RPC handler: given serialised args, produce a serialised result.
 * result_len is an in/out: the handler writes at most *result_len bytes into
 * result and sets *result_len to the actual byte count written. Returns 0 on
 * success, negative on error. */
typedef int32_t (*pm_metal_rpc_handler_t)(
    const uint8_t *args, uint32_t args_len,
    uint8_t *result, uint32_t *result_len,
    void *user);

/* An outgoing call descriptor. Filled by invoke(), matched to a result by
 * call_id when the reply comes back. */
typedef struct pm_metal_rpc_call {
    char key[PM_METAL_RPC_KEY_MAX];
    uint8_t args[PM_METAL_RPC_ARG_MAX];
    uint32_t args_len;
    uint64_t call_id;
} pm_metal_rpc_call_t;

/* A result delivered by poll() for one earlier invoke(). */
typedef struct pm_metal_rpc_result {
    uint64_t call_id;
    int32_t status;
    uint8_t result[PM_METAL_RPC_RESULT_MAX];
    uint32_t result_len;
} pm_metal_rpc_result_t;

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_RPC_TYPES_H */