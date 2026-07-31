/*
 * Shared I/O size budget (wire vs fat server).
 *
 * Values come from Kconfig (config/metal/net/) via build/autoconf.h.
 * See docs/KCONFIG.md. Fallback defaults match config/defconfig when
 * autoconf.h is not -include'd (clangd / host tools).
 *
 * Wire: TLS/HTTP-client/py-recv chunk unit — one TCP window-ish copy.
 * ASGI: server request/response scratch so sync reply/static can move
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

#if defined(__has_include)
#if __has_include("autoconf.h")
#include "autoconf.h"
#endif
#endif

#ifndef CONFIG_PM_METAL_IO_WIRE_MAX
#define CONFIG_PM_METAL_IO_WIRE_MAX (1024u * 1024u)
#endif
#ifndef CONFIG_PM_METAL_ASGI_IO_MAX
#define CONFIG_PM_METAL_ASGI_IO_MAX 4194304
#endif

/** Stream client / TLS / HTTP-get / py recv chunk. */
#define PM_METAL_IO_WIRE_MAX ((uint32_t)CONFIG_PM_METAL_IO_WIRE_MAX)

/** ASGI listen iobuf / sync send_simple body cap / static file chunk. */
#define PM_METAL_ASGI_IO_MAX ((uint32_t)CONFIG_PM_METAL_ASGI_IO_MAX)

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_IO_BUDGET_H_ */
