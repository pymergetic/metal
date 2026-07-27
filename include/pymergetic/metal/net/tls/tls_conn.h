/*
 * Host-only persistent TLS/TCP connection slots for Python's
 * pymergetic.metal.tls.* (tls_py_bind.c) — the plumbing http.c already has
 * inline (socket connect + optional mbedTLS handshake + app-data read/write,
 * all wire-pumped through pm_metal_net_recv), pulled out here as a small
 * connection object with a stable handle so separate Python-level await
 * calls (connect(), then write(), then read(), ...) can all operate on the
 * same live socket. Not a guest/wasm surface — no __wasm__ ABI branch,
 * same as tls.h itself.
 *
 * impl: common — src/pymergetic/metal/dev/net/tls_conn.c
 */
#ifndef PYMERGETIC_METAL_DEV_NET_TLS_CONN_H_
#define PYMERGETIC_METAL_DEV_NET_TLS_CONN_H_

#include <stdint.h>

#include "pymergetic/metal/runtime/async/async.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t pm_metal_tls_conn_h;

#define PM_METAL_TLS_CONN_INVALID 0u

/** Largest single read()/write() this connection type will move in one
 * coroutine step — bigger requests are chunked by the Python-facing bind
 * (tls_py_bind.c), matching the wire buffer's own PM_METAL_TLS_WIRE_MAX. */
#define PM_METAL_TLS_CONN_IO_MAX 4096u

/** Claim a connection slot. PM_METAL_TLS_CONN_INVALID if none free. */
pm_metal_tls_conn_h pm_metal_tls_conn_open(void);

/** Tear down (socket + TLS session, if any) and free the slot. Idempotent. */
void pm_metal_tls_conn_close(pm_metal_tls_conn_h ch);

/**
 * DNS (if host isn't a literal) + socket + connect, then an mbedTLS
 * handshake if @a use_tls. After await: pm_metal_async_result_u32() is 1 on
 * success, 0 on any failure (DNS/connect/handshake).
 */
pm_metal_async_handle_t pm_metal_tls_conn_connect(pm_metal_tls_conn_h ch,
                                                  const char         *host,
                                                  uint16_t            port,
                                                  int32_t             use_tls);

/**
 * Write up to @a len bytes (capped to PM_METAL_TLS_CONN_IO_MAX — caller
 * chunks longer buffers across repeated calls). The bytes are copied out of
 * @a data before the first await (safe to pass a pointer into a Python
 * buffer — see py_obj.h's buf_get doc — since nothing here holds it across
 * a yield). After await: pm_metal_async_result_u32() is bytes actually
 * written (may be less than @a len only on error, which also yields 0).
 */
pm_metal_async_handle_t pm_metal_tls_conn_write(pm_metal_tls_conn_h ch,
                                                const void         *data,
                                                uint32_t            len);

/**
 * Read up to @a want bytes (capped to PM_METAL_TLS_CONN_IO_MAX) into this
 * connection's own scratch buffer. After await: pm_metal_async_result_u32()
 * is bytes read (0 on clean EOF); pm_metal_tls_conn_read_buf() then exposes
 * where they landed for the caller to copy out (e.g. into a fresh Python
 * bytes object) — valid only until the next call on this same handle.
 */
pm_metal_async_handle_t pm_metal_tls_conn_read(pm_metal_tls_conn_h ch, uint32_t want);

/** Valid only right after a pm_metal_tls_conn_read() await completes. */
const uint8_t *pm_metal_tls_conn_read_buf(pm_metal_tls_conn_h ch);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_NET_TLS_CONN_H_ */
