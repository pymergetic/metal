/* pymergetic.metal.net.zenoh — card-private surface shared by the card's own
 * translation units (__impl__.c, platform_metal_sys.c, platform_metal.c).
 * Never included from outside the card: the public border is __exports__.h.
 *
 * The card keeps one zenoh-pico session. The platform files are compiled
 * against the vendored lib (ZENOH_GENERIC platform); __impl__.c owns the face
 * and the poll() step that drives z_read() + z_send_keep_alive(). */
#ifndef PYMERGETIC_METAL_NET_ZENOH_PRIV_H
#define PYMERGETIC_METAL_NET_ZENOH_PRIV_H

#include "pymergetic/metal/net/zenoh/__types__.h"
#include "pymergetic/metal/async.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/util/mem.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The card arena. Owned by __impl__.c (set in init), read by the platform
 * files (z_malloc/z_free + _z_socket_get_endpoints). */
pm_util_mem_arena_t *pm_metal_net_zenoh_arena(void);

/* Cooperative scheduler bridge. zenoh-pico's open/handshake and the data-path
 * reads are synchronous C calls; when such a call would block waiting on the
 * network (recv returns no data, connect not yet ESTAB), the platform spins
 * this hook for one bounded step instead of busy-waiting alone. __impl__.c sets
 * it to pump net.ip once and run a bounded step of the executor of every
 * session the card has opened (so the peer that must answer the handshake makes
 * progress). NULL means "pump net.ip only". */
typedef void (*pm_metal_net_zenoh_yield_fn)(void);
void pm_metal_net_zenoh_set_yield(pm_metal_net_zenoh_yield_fn fn);
void pm_metal_net_zenoh_yield(void); /* calls the hook, or pumps net.ip alone */

/* Parse a dotted-quad IPv4 string into network-order bytes; 0 on success,
 * -1 on a non-IPv4 or malformed literal. net.ip is IPv4-only, so hostnames are
 * caller-resolved first. */
int pm_metal_net_zenoh_parse_ipv4(const char *host, uint32_t *addr_be_out);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_ZENOH_PRIV_H */
