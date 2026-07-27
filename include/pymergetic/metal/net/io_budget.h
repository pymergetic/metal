/*
 * Shared I/O size budget (wire vs fat server).
 *
 * Wire (32 KiB): TLS/HTTP-client/py-recv chunk unit — one TCP window-ish copy.
 * ASGI (4 MiB): server request/response scratch so sync reply/static can move
 * large bodies without mid-send abort.
 *
 * L2 stays MTU-class (1514 / pbuf 1600) in virtio_net / lwipopts — not here.
 */
#ifndef PYMERGETIC_METAL_NET_IO_BUDGET_H_
#define PYMERGETIC_METAL_NET_IO_BUDGET_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Stream client / TLS / HTTP-get / py recv chunk. */
#define PM_METAL_IO_WIRE_MAX (32u * 1024u)

/** ASGI listen iobuf / sync send_simple body cap / static file chunk. */
#define PM_METAL_ASGI_IO_MAX (4u * 1024u * 1024u)

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_IO_BUDGET_H_ */
