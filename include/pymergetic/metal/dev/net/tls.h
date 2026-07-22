/*
 * TLS client over pm_metal_net_* (mbedTLS stream, async-friendly wire buffer).
 *
 * Host-only: HTTPS is via pm_metal_net_http_get on guests; this header is for
 * host-side TLS wiring (http.c, native_register), not a guest import surface.
 *
 * impl: common — src/pymergetic/metal/dev/net/tls.c
 */
#ifndef PYMERGETIC_METAL_DEV_NET_TLS_H_
#define PYMERGETIC_METAL_DEV_NET_TLS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

#include "pymergetic/metal/dev/net/net.h"

#define PM_METAL_TLS_WIRE_MAX 4096u

typedef struct pm_metal_tls_wire {
	uint8_t buf[PM_METAL_TLS_WIRE_MAX];
	uint32_t len;
	uint32_t off;
} pm_metal_tls_wire_t;

typedef uint32_t pm_metal_tls_h;

#define PM_METAL_TLS_INVALID 0u

/** Same semantics as MBEDTLS_ERR_SSL_WANT_READ / WANT_WRITE. */
#define PM_METAL_TLS_WANT_READ  (-0x6900)
#define PM_METAL_TLS_WANT_WRITE (-0x6880)

void pm_metal_net_tls_wire_reset(pm_metal_tls_wire_t *wire);
void pm_metal_net_tls_wire_feed(pm_metal_tls_wire_t *wire, const void *data,
				uint32_t len);

pm_metal_tls_h pm_metal_net_tls_open(const char *sni_host);
void pm_metal_net_tls_close(pm_metal_tls_h h);

int pm_metal_net_tls_bind(pm_metal_tls_h h, pm_metal_net_sock_h sock,
			  pm_metal_tls_wire_t *wire);

/** 0 done, 1 need more wire I/O, -1 error */
int pm_metal_net_tls_handshake_step(pm_metal_tls_h h);
int pm_metal_net_tls_handshake_done(pm_metal_tls_h h);

/** Returns byte count, 0 EOF, MBEDTLS want codes, or negative error. */
int pm_metal_net_tls_read(pm_metal_tls_h h, void *buf, uint32_t cap);
int pm_metal_net_tls_write(pm_metal_tls_h h, const void *buf, uint32_t len);

int pm_metal_net_tls_native_register(void);
void pm_metal_net_tls_bind_inst(void *module_inst);

#endif /* !__wasm__ */

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_NET_TLS_H_ */
