/** @file
  General TLS client (mbedTLS) over pm_metal_net_* sockets.
  (impl: efi|bios)
**/
#include <pymergetic/metal/dev/net/tls.h>
#include <pymergetic/metal/dev/net/net.h>
#include <pymergetic/metal/dev/net/net_ops.h>
#include <pymergetic/metal/dev/net/mbedtls_metal_config.h>
#include <pymergetic/metal/dev/random/random.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

#include <mbedtls/build_info.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/platform.h>
#include <mbedtls/ssl.h>

#include <stddef.h>
#include <stdint.h>

#define TLS_SESS_MAX  8u
#define TLS_SNI_MAX   128u

typedef struct {
	INT32                    valid;
	pm_metal_net_sock_h      sock;
	pm_metal_tls_wire_t     *wire;
	CHAR8                    sni[TLS_SNI_MAX];
	mbedtls_ssl_context      ssl;
	mbedtls_ssl_config       conf;
	INT32                    ready;
	INT32                    done;
} tls_sess_t;

STATIC tls_sess_t               mTls[TLS_SESS_MAX + 1];
STATIC INT32                    mTlsGlobal;
STATIC mbedtls_entropy_context  mEntropy;
STATIC mbedtls_ctr_drbg_context mCtrDrbg;

STATIC INT32
TlsEntropyPoll (
  VOID    *ctx,
  UINT8   *out,
  size_t   len,
  size_t  *olen
  )
{
	UINT32  got;

	(VOID)ctx;
	if (out == NULL || olen == NULL) {
		return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
	}

	got = pm_metal_random (out, (UINT32)len);
	*olen = got;
	return (got == len) ? 0 : MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
}

STATIC INT32
TlsGlobalInit (
  VOID
  )
{
	INT32  e;

	if (mTlsGlobal) {
		return 0;
	}

	pm_metal_mbedtls_runtime_init ();
	mbedtls_entropy_init (&mEntropy);
	mbedtls_ctr_drbg_init (&mCtrDrbg);
	e = mbedtls_entropy_add_source (
	      &mEntropy,
	      TlsEntropyPoll,
	      NULL,
	      32,
	      MBEDTLS_ENTROPY_SOURCE_STRONG
	      );
	if (e != 0) {
		return -1;
	}

	e = mbedtls_ctr_drbg_seed (
	      &mCtrDrbg,
	      mbedtls_entropy_func,
	      &mEntropy,
	      (CONST UINT8 *)"metal-tls",
	      9
	      );
	if (e != 0) {
		return -1;
	}

	mTlsGlobal = 1;
	return 0;
}

STATIC INT32
TlsSockSend (
  VOID          *ctx,
  CONST UINT8   *buf,
  size_t         len
  )
{
	tls_sess_t  *t;
	UINT32       n;

	t = (tls_sess_t *)ctx;
	if (t == NULL || buf == NULL) {
		return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
	}

	n = pm_metal_net_send (t->sock, buf, (UINT32)len);
	if (n == 0) {
		return MBEDTLS_ERR_SSL_WANT_WRITE;
	}

	if (n < len) {
		return MBEDTLS_ERR_SSL_WANT_WRITE;
	}

	return (INT32)len;
}

STATIC INT32
TlsSockRecv (
  VOID    *ctx,
  UINT8   *buf,
  size_t   len
  )
{
	tls_sess_t  *t;
	size_t       n;

	t = (tls_sess_t *)ctx;
	if (t == NULL || buf == NULL || len == 0 || t->wire == NULL) {
		return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
	}

	if (t->wire->off < t->wire->len) {
		n = t->wire->len - t->wire->off;
		if (n > len) {
			n = len;
		}

		CopyMem (buf, t->wire->buf + t->wire->off, n);
		t->wire->off += (UINT32)n;
		return (INT32)n;
	}

	return MBEDTLS_ERR_SSL_WANT_READ;
}

STATIC tls_sess_t *
TlsFromHandle (
  pm_metal_tls_h  h
  )
{
	if (h == 0 || h > TLS_SESS_MAX) {
		return NULL;
	}

	if (!mTls[h].valid) {
		return NULL;
	}

	return &mTls[h];
}

void
pm_metal_net_tls_wire_reset (
  pm_metal_tls_wire_t  *wire
  )
{
	if (wire != NULL) {
		wire->len = 0;
		wire->off = 0;
	}
}

void
pm_metal_net_tls_wire_feed (
  pm_metal_tls_wire_t  *wire,
  CONST VOID           *data,
  UINT32                len
  )
{
	if (wire == NULL || data == NULL || len == 0) {
		return;
	}

	if (len > PM_METAL_TLS_WIRE_MAX) {
		len = PM_METAL_TLS_WIRE_MAX;
	}

	CopyMem (wire->buf, data, len);
	wire->len = len;
	wire->off = 0;
}

pm_metal_tls_h
pm_metal_net_tls_open (
  CONST CHAR8  *sni_host
  )
{
	UINT32  i;

	if (sni_host == NULL || sni_host[0] == '\0') {
		return PM_METAL_TLS_INVALID;
	}

	if (TlsGlobalInit () != 0) {
		return PM_METAL_TLS_INVALID;
	}

	for (i = 1; i <= TLS_SESS_MAX; i++) {
		if (mTls[i].valid) {
			continue;
		}

		ZeroMem (&mTls[i], sizeof (mTls[i]));
		mTls[i].valid = 1;
		mTls[i].sock  = PM_METAL_NET_SOCK_INVALID;
		AsciiStrCpyS (mTls[i].sni, sizeof (mTls[i].sni), sni_host);
		return (pm_metal_tls_h)i;
	}

	return PM_METAL_TLS_INVALID;
}

STATIC VOID
TlsTeardown (
  tls_sess_t  *t
  )
{
	if (t == NULL) {
		return;
	}

	if (t->ready) {
		mbedtls_ssl_free (&t->ssl);
		mbedtls_ssl_config_free (&t->conf);
		t->ready = 0;
	}

	t->done = 0;
	t->wire = NULL;
	t->sock = PM_METAL_NET_SOCK_INVALID;
}

void
pm_metal_net_tls_close (
  pm_metal_tls_h  h
  )
{
	tls_sess_t  *t;

	t = TlsFromHandle (h);
	if (t == NULL) {
		return;
	}

	TlsTeardown (t);
	t->valid = 0;
}

int
pm_metal_net_tls_bind (
  pm_metal_tls_h         h,
  pm_metal_net_sock_h    sock,
  pm_metal_tls_wire_t   *wire
  )
{
	tls_sess_t  *t;
	INT32        e;

	t = TlsFromHandle (h);
	if (t == NULL || sock == PM_METAL_NET_SOCK_INVALID || wire == NULL) {
		return -1;
	}

	TlsTeardown (t);
	t->sock  = sock;
	t->wire  = wire;
	t->done  = 0;
	pm_metal_net_tls_wire_reset (wire);

	mbedtls_ssl_init (&t->ssl);
	mbedtls_ssl_config_init (&t->conf);
	e = mbedtls_ssl_config_defaults (
	      &t->conf,
	      MBEDTLS_SSL_IS_CLIENT,
	      MBEDTLS_SSL_TRANSPORT_STREAM,
	      MBEDTLS_SSL_PRESET_DEFAULT
	      );
	if (e != 0) {
		TlsTeardown (t);
		return -1;
	}

	mbedtls_ssl_conf_authmode (&t->conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
	mbedtls_ssl_conf_rng (&t->conf, mbedtls_ctr_drbg_random, &mCtrDrbg);
	e = mbedtls_ssl_setup (&t->ssl, &t->conf);
	if (e != 0) {
		TlsTeardown (t);
		return -1;
	}

	e = mbedtls_ssl_set_hostname (&t->ssl, t->sni);
	if (e != 0) {
		TlsTeardown (t);
		return -1;
	}

	mbedtls_ssl_set_bio (&t->ssl, t, TlsSockSend, TlsSockRecv, NULL);
	t->ready = 1;
	return 0;
}

int
pm_metal_net_tls_handshake_step (
  pm_metal_tls_h  h
  )
{
	tls_sess_t  *t;
	INT32        e;

	t = TlsFromHandle (h);
	if (t == NULL || !t->ready) {
		return -1;
	}

	e = mbedtls_ssl_handshake (&t->ssl);
	if (e == 0) {
		t->done = 1;
		return 0;
	}

	if (e == MBEDTLS_ERR_SSL_WANT_READ || e == MBEDTLS_ERR_SSL_WANT_WRITE) {
		return 1;
	}

	return -1;
}

int
pm_metal_net_tls_handshake_done (
  pm_metal_tls_h  h
  )
{
	tls_sess_t  *t;

	t = TlsFromHandle (h);
	if (t == NULL) {
		return 0;
	}

	return t->done;
}

int
pm_metal_net_tls_read (
  pm_metal_tls_h  h,
  VOID           *buf,
  UINT32          cap
  )
{
	tls_sess_t  *t;

	t = TlsFromHandle (h);
	if (t == NULL || !t->ready || !t->done || buf == NULL || cap == 0) {
		return -1;
	}

	return mbedtls_ssl_read (&t->ssl, buf, cap);
}

int
pm_metal_net_tls_write (
  pm_metal_tls_h  h,
  CONST VOID     *buf,
  UINT32          len
  )
{
	tls_sess_t  *t;
	INT32        e;

	t = TlsFromHandle (h);
	if (t == NULL || !t->ready || !t->done || buf == NULL || len == 0) {
		return -1;
	}

	e = mbedtls_ssl_write (&t->ssl, buf, len);
	if (e > 0) {
		return e;
	}

	if (e == MBEDTLS_ERR_SSL_WANT_READ) {
		return PM_METAL_TLS_WANT_READ;
	}

	if (e == MBEDTLS_ERR_SSL_WANT_WRITE) {
		return PM_METAL_TLS_WANT_WRITE;
	}

	return -1;
}

/*
 * TLS is host-only (http.c). Stubs keep wasm.c call sites; no WASI natives —
 * guests use pm_metal_net_http_get for HTTPS.
 */
int
pm_metal_net_tls_native_register (
  VOID
  )
{
	return 0;
}

void
pm_metal_net_tls_bind_inst (
  VOID  *module_inst
  )
{
	(VOID)module_inst;
}
