/* pymergetic.metal.net.zenoh — Zenoh v1 (vendored zenoh-pico) on net.ip socks.
 * The card face is one session driven by a bounded poll() step. */
#ifndef PYMERGETIC_METAL_NET_ZENOH_TYPES_H
#define PYMERGETIC_METAL_NET_ZENOH_TYPES_H

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/util/mem/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_NET_ZENOH_ZID_LEN 16

/* Scouting (SCOUT/HELLO) — Zenoh discovery over UDP multicast. The group an
 * endpoint scouting / hello-answering peer joins is 224.0.0.224:7446, the net.ip
 * multicast group a card slots into so a live peer's datagrams demux here. */
#define PM_METAL_NET_ZENOH_SCOUT_PORT 7446u
#define PM_METAL_NET_ZENOH_SCOUT_GROUP 0xe00000e0u /* 224.0.0.224, network order */
#define PM_METAL_NET_ZENOH_SCOUT_BUFFER 256

/* Bounded stack sample buffer for subscriber delivery: a sample payload is
 * copied here before the callback runs, so no pointer is held across a poll()
 * step. Longer payloads are truncated. */
#define PM_METAL_NET_ZENOH_MAX_SAMPLE 512

/* A subscribe/queryable callback fires with one value inside a poll() step.
 * `payload`/`klen` are valid only for the duration of the call; the card does
 * not hold them across steps (metal-no-pointer-across-await). */
typedef void (*pm_metal_net_zenoh_value_cb_t)(const char *key, size_t klen,
    const uint8_t *payload, size_t plen, void *arg);

/* Queryable reply sink: append a bytes value to the in-flight reply. The
 * layout is private to the card (__impl__.c); the query callback only ever sees
 * the pm_metal_net_zenoh_reply_append face below. */
typedef struct pm_metal_net_zenoh_reply pm_metal_net_zenoh_reply_t;

/* Append bytes to the in-flight query reply. Returns 0 when the reply buffer is
 * full (the reply is already truncated enough to stay bounded). Valid only
 * inside a query callback. */
int32_t pm_metal_net_zenoh_reply_append(pm_metal_net_zenoh_reply_t *reply, const uint8_t *data, size_t len);

typedef void (*pm_metal_net_zenoh_query_cb_t)(const char *key, size_t klen,
    const uint8_t *params, size_t plen, pm_metal_net_zenoh_reply_t *reply, void *arg);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_ZENOH_TYPES_H */
