/* pymergetic.metal.net.rpc — remote procedure calls on a net.zenoh session.
 *
 * Server side: pm_metal_rpc_register(key, handler, user) registers a handler
 * that answers queries on "rpc/<key>". The card arms one zenoh queryable at
 * "rpc/ *" (wildcard) that dispatches by key to the registered handler list.
 *
 * Client side: pm_metal_rpc_invoke(peer_zenoh_id, key, args, args_len) sends
 * a query on the zenoh session and stores a pending call. pm_metal_rpc_poll(out)
 * returns a completed result when one is ready.
 *
 * Store pending calls in a ring buffer (PM_UTIL_LIMIT_C, max 32). call_id is a
 * monotonic counter. Handlers live in a static table (PM_UTIL_LIMIT_C, max 16).
 * All storage is static — no arena dependency.
 */
#include "pymergetic/metal/net/rpc/__exports__.h"

#include "pymergetic/util/limits.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#ifndef PM_METAL_RPC_HANDLERS_MAX
#define PM_METAL_RPC_HANDLERS_MAX 16u
#endif

#ifndef PM_METAL_RPC_CALLS_MAX
#define PM_METAL_RPC_CALLS_MAX 32u
#endif

/* One registered handler slot. */
struct rpc_handler_slot {
    char key[PM_METAL_RPC_KEY_MAX];
    pm_metal_rpc_handler_t handler;
    void *user;
    uint8_t used; /* 1 when occupied */
};

/* One pending call ring entry. */
struct rpc_pending_call {
    uint8_t valid; /* 1 when this slot is occupied */
    uint64_t call_id;
    /* The result is written here when the reply comes back. */
    int32_t status;
    uint8_t result[PM_METAL_RPC_RESULT_MAX];
    uint32_t result_len;
};

static struct rpc_handler_slot s_handlers[PM_METAL_RPC_HANDLERS_MAX];
static uint32_t s_nh; /* live handler count */

static struct rpc_pending_call s_calls[PM_METAL_RPC_CALLS_MAX];
static uint32_t s_pending_head; /* next write index */
static uint32_t s_pending_count; /* number of live entries */

static uint64_t s_next_call_id = 1;

/* The zenoh queryable is armed once (one card, one session). */
__attribute__((unused)) static uint8_t s_armed;

PM_UTIL_LIMIT_C(pm_rpc_handlers_limit, pymergetic.metal.net.rpc, handlers,
    PM_METAL_RPC_HANDLERS_MAX, 0u, &s_nh);
PM_UTIL_LIMIT_C(pm_rpc_calls_limit, pymergetic.metal.net.rpc, calls,
    PM_METAL_RPC_CALLS_MAX, 0u, &s_pending_count);

/* Find a handler slot by key, returning its index or -1. */
static int32_t find_handler(const char *key) {
    uint32_t i;
    if (key == NULL) {
        return -1;
    }
    for (i = 0; i < s_nh; i++) {
        if (s_handlers[i].used && strcmp(s_handlers[i].key, key) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

/* Find a free handler slot, compacting dead entries. */
static int32_t find_free_handler(void) {
    uint32_t i;
    uint32_t n = s_nh;
    /* Compact dead entries first. */
    for (i = 0; i < n; i++) {
        if (!s_handlers[i].used) {
            n--;
            if (i < n) {
                s_handlers[i] = s_handlers[n];
            }
            memset(&s_handlers[n], 0, sizeof(s_handlers[n]));
            i--;
        }
    }
    s_nh = n;
    if (n >= PM_METAL_RPC_HANDLERS_MAX) {
        return -1;
    }
    return (int32_t)n;
}

int32_t pm_metal_rpc_register(const char *key, pm_metal_rpc_handler_t handler, void *user) {
    int32_t slot;
    if (key == NULL || key[0] == '\0' || handler == NULL) {
        return -1;
    }
    /* Already registered? Replace. */
    slot = find_handler(key);
    if (slot >= 0) {
        s_handlers[slot].handler = handler;
        s_handlers[slot].user = user;
        return 0;
    }
    slot = find_free_handler();
    if (slot < 0) {
        return -1; /* no room */
    }
    memset(&s_handlers[slot], 0, sizeof(s_handlers[slot]));
    strncpy(s_handlers[slot].key, key, sizeof(s_handlers[slot].key) - 1);
    s_handlers[slot].key[sizeof(s_handlers[slot].key) - 1] = '\0';
    s_handlers[slot].handler = handler;
    s_handlers[slot].user = user;
    s_handlers[slot].used = 1;
    s_nh++;
    return 0;
}

int32_t pm_metal_rpc_unregister(const char *key) {
    int32_t slot;
    if (key == NULL || key[0] == '\0') {
        return -1;
    }
    slot = find_handler(key);
    if (slot < 0) {
        return -1;
    }
    s_handlers[slot].used = 0;
    if (slot < (int32_t)(s_nh - 1)) {
        s_handlers[slot] = s_handlers[s_nh - 1];
    }
    memset(&s_handlers[s_nh - 1], 0, sizeof(s_handlers[s_nh - 1]));
    s_nh--;
    return 0;
}

/* Find a free pending-call slot. The ring is head/tail with count; a compact
 * pass removes consumed entries. */
static int32_t alloc_pending(void) {
    uint32_t i;
    uint32_t read;
    uint32_t n;
    /* Compact: shift valid but consumed entries out. */
    read = s_pending_head;
    n = s_pending_count;
    for (i = 0; i < n; i++) {
        if (!s_calls[read].valid) {
            /* Consume by skipping. */
            read = (read + 1) % PM_METAL_RPC_CALLS_MAX;
            continue;
        }
        break;
    }
    s_pending_head = read;
    /* Count only valid entries past the head. */
    {
        uint32_t count = 0;
        uint32_t pos = s_pending_head;
        for (i = 0; i < n; i++) {
            if (s_calls[pos].valid) {
                count++;
            }
            pos = (pos + 1) % PM_METAL_RPC_CALLS_MAX;
        }
        s_pending_count = count;
    }
    if (s_pending_count >= PM_METAL_RPC_CALLS_MAX) {
        return -1;
    }
    return (int32_t)((s_pending_head + s_pending_count) % PM_METAL_RPC_CALLS_MAX);
}

int64_t pm_metal_rpc_invoke(const char *peer_zenoh_id, const char *key,
    const uint8_t *args, uint32_t args_len) {
    int32_t slot;
    uint64_t cid;
    (void)peer_zenoh_id; /* zenoh session dispatch by peer is via put for now */
    if (key == NULL || key[0] == '\0' || (args_len && args == NULL)) {
        return -1;
    }
    if (args_len > PM_METAL_RPC_ARG_MAX) {
        return -1;
    }
    slot = alloc_pending();
    if (slot < 0) {
        return -1;
    }
    cid = s_next_call_id++;
    s_calls[slot].valid = 1;
    s_calls[slot].call_id = cid;
    s_calls[slot].status = -100; /* pending sentinel */
    s_calls[slot].result_len = 0;
    s_pending_count++;
    /* TODO: send the actual zenoh put/query for "rpc/<key>" with args as
     * payload. For the data-structure prove (no running zenoh needed) we
     * store the pending slot; the network dispatch is wired when zenoh is
     * up. */
    return (int64_t)cid;
}

int32_t pm_metal_rpc_poll(pm_metal_rpc_result_t *out) {
    uint32_t i;
    uint32_t count = s_pending_count;
    if (out == NULL) {
        return -1;
    }
    i = s_pending_head;
    while (count > 0) {
        if (s_calls[i].valid && s_calls[i].status != -100) {
            /* This one has a result. */
            out->call_id = s_calls[i].call_id;
            out->status = s_calls[i].status;
            if (s_calls[i].result_len > PM_METAL_RPC_RESULT_MAX) {
                out->result_len = PM_METAL_RPC_RESULT_MAX;
            } else {
                out->result_len = s_calls[i].result_len;
            }
            if (out->result_len > 0) {
                memcpy(out->result, s_calls[i].result, out->result_len);
            }
            s_calls[i].valid = 0;
            s_pending_count--;
            return 1;
        }
        i = (i + 1) % PM_METAL_RPC_CALLS_MAX;
        count--;
    }
    return 0;
}

/* Feed a result into the pending ring for a call_id. Internal; the zenoh
 * reply callback calls this when a "rpc/<key>" response arrives. */
__attribute__((unused)) static int32_t deliver_result(uint64_t call_id, int32_t status,
    const uint8_t *result, uint32_t result_len) {
    uint32_t i;
    uint32_t n = s_pending_count;
    i = s_pending_head;
    while (n > 0) {
        if (s_calls[i].valid && s_calls[i].call_id == call_id) {
            s_calls[i].status = status;
            if (result_len > PM_METAL_RPC_RESULT_MAX) {
                s_calls[i].result_len = PM_METAL_RPC_RESULT_MAX;
            } else {
                s_calls[i].result_len = result_len;
            }
            if (s_calls[i].result_len > 0 && result != NULL) {
                memcpy(s_calls[i].result, result, s_calls[i].result_len);
            }
            return 0;
        }
        i = (i + 1) % PM_METAL_RPC_CALLS_MAX;
        n--;
    }
    return -1;
}

int32_t pm_metal_rpc_handle(const char *key, const uint8_t *args, uint32_t args_len,
    uint8_t *result, uint32_t *result_len) {
    int32_t slot;
    if (key == NULL || args == NULL || result == NULL || result_len == NULL) {
        return -1;
    }
    slot = find_handler(key);
    if (slot < 0) {
        return -2; /* no handler registered */
    }
    return s_handlers[slot].handler(args, args_len, result, result_len,
        s_handlers[slot].user);
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.net.rpc, pm_metal_rpc_register, pm_metal_rpc_register, int32_t(const char *, pm_metal_rpc_handler_t, void *));
PM_MOD_EXPORT_C(pymergetic.metal.net.rpc, pm_metal_rpc_unregister, pm_metal_rpc_unregister, int32_t(const char *));
PM_MOD_EXPORT_C(pymergetic.metal.net.rpc, pm_metal_rpc_invoke, pm_metal_rpc_invoke, int64_t(const char *, const char *, const uint8_t *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.rpc, pm_metal_rpc_poll, pm_metal_rpc_poll, int32_t(pm_metal_rpc_result_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.rpc, pm_metal_rpc_handle, pm_metal_rpc_handle, int32_t(const char *, const uint8_t *, uint32_t, uint8_t *, uint32_t *));