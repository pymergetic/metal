/* pymergetic.metal.net.http.asgi — RS HTTP server (microdot-shaped). */
#ifndef PYMERGETIC_METAL_NET_HTTP_ASGI_TYPES_H
#define PYMERGETIC_METAL_NET_HTTP_ASGI_TYPES_H

#include <stdint.h>

#include "pymergetic/util/mem/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 0 = filled out/out_len; nonzero = caller should use the default body. */
typedef int32_t (*pm_metal_net_http_asgi_handler_t)(
    const char *method, const char *path, uint8_t *out, uint32_t out_max, uint32_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_HTTP_ASGI_TYPES_H */
