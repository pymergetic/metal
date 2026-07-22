/** @file
  General TLS client (mbedTLS) over pm_metal_net_* sockets.
  (impl: efi|bios)
**/
#include <pymergetic/metal/dev/net/tls.h>
#include <pymergetic/metal/dev/net/net.h>
#include <pymergetic/metal/dev/net/net_ops.h>
#include <pymergetic/metal/dev/random/random.h>
#include <runtime/mem/mem.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>

#include <mbedtls/build_info.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/platform.h>
#include <mbedtls/ssl.h>

#include "wasm_export.h"

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
STATIC wasm_module_inst_t       mTlsInst;

STATIC VOID *
TlsCalloc (
  size_t  n,
  size_t  sz
  )
{
	size_t  t;
	VOID   *p;

	t = n * sz;
	if (t == 0) {
		return NULL;
	}

	p = pm_metal_mem_alloc (t, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
	if (p != NULL) {
		ZeroMem (p, t);
	}

	return p;
}

STATIC VOID
TlsFree (
  VOID  *p
  )
{
	pm_metal_mem_free (p);
}

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

	mbedtls_platform_set_calloc_free (TlsCalloc, TlsFree);
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
	size_t       off;

	t = TlsFromHandle (h);
	if (t == NULL || !t->ready || !t->done || buf == NULL || len == 0) {
		return -1;
	}

	off = 0;
	while (off < len) {
		e = mbedtls_ssl_write (
		      &t->ssl,
		      (CONST UINT8 *)buf + off,
		      len - (UINT32)off
		      );
		if (e <= 0) {
			if (e == MBEDTLS_ERR_SSL_WANT_READ
			    || e == MBEDTLS_ERR_SSL_WANT_WRITE)
			{
				pm_metal_net_poll ();
				continue;
			}

			return -1;
		}

		off += (size_t)e;
	}

	return 0;
}

#if !defined(__wasm__)

STATIC INT32
TlsGuestCopyStr (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *src,
  CHAR8           *out,
  UINTN            out_sz
  )
{
	wasm_module_inst_t  inst;
	UINTN               i;

	inst = wasm_runtime_get_module_inst (exec_env);
	if (inst == NULL || src == NULL || out == NULL || out_sz == 0) {
		return -1;
	}

	if (!wasm_runtime_validate_native_addr (inst, (VOID *)src, 1)) {
		return -1;
	}

	for (i = 0; i + 1 < out_sz; i++) {
		if (!wasm_runtime_validate_native_addr (inst, (VOID *)(src + i), 1)) {
			return -1;
		}

		out[i] = src[i];
		if (src[i] == '\0') {
			return 0;
		}
	}

	return -1;
}

STATIC UINT32
pm_metal_net_tls_open_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *sni
  )
{
	CHAR8  cleaned[TLS_SNI_MAX];

	if (mTlsInst == NULL) {
		return PM_METAL_TLS_INVALID;
	}

	if (TlsGuestCopyStr (exec_env, sni, cleaned, sizeof (cleaned)) != 0) {
		return PM_METAL_TLS_INVALID;
	}

	return pm_metal_net_tls_open (cleaned);
}

STATIC VOID
pm_metal_net_tls_close_native (
  wasm_exec_env_t  exec_env,
  UINT32           h
  )
{
	(VOID)exec_env;
	pm_metal_net_tls_close (h);
}

STATIC NativeSymbol g_pm_metal_net_tls_native_symbols[] = {
  { "pm_metal_net_tls_open", (VOID *)pm_metal_net_tls_open_native, "($)i", NULL },
  { "pm_metal_net_tls_close", (VOID *)pm_metal_net_tls_close_native, "(i)", NULL },
};

int
pm_metal_net_tls_native_register (
  VOID
  )
{
	if (!wasm_runtime_register_natives (
	       "pymergetic.metal.net.tls",
	       g_pm_metal_net_tls_native_symbols,
	       sizeof (g_pm_metal_net_tls_native_symbols)
	         / sizeof (g_pm_metal_net_tls_native_symbols[0])
	       ))
	{
		return -1;
	}

	return 0;
}

void
pm_metal_net_tls_bind_inst (
  VOID  *module_inst
  )
{
	mTlsInst = (wasm_module_inst_t)module_inst;
}

#endif
