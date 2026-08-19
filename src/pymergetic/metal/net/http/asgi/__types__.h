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

/* Streaming response (large download; body is not buffered). Declare the total
 * length with size(), then producer() is called repeatedly; each call fills
 * [0,cap) of chunk and sets *len. Return 0 to keep streaming, nonzero on error.
 * *more stays nonzero until the final chunk. */
typedef uint64_t (*pm_metal_net_http_asgi_stream_size_t)(void *ctx);
typedef int32_t (*pm_metal_net_http_asgi_stream_producer_t)(
    void *ctx, uint8_t *chunk, uint32_t *len, uint32_t cap, int32_t *more);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_HTTP_ASGI_TYPES_H */
