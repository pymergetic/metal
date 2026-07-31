/*
 * Minimal WebSocket upgrade + text/binary echo (RFC6455).
 */
#include "asgi_internal.h"

#include <string.h>

#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>

#include <pymergetic/metal/net/http/http_parse.h>

static const char kWsGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

int32_t pm_metal_net_asgi_ws_wanted(const char *hdr, uint32_t hdr_len)
{
  char up[32];
  char conn[64];

  if (pm_metal_http_hdr_get(hdr, hdr_len, "Upgrade", up, sizeof(up)) != 0) {
    return 0;
  }
  if (strstr(up, "websocket") == NULL && strstr(up, "WebSocket") == NULL) {
    return 0;
  }
  if (pm_metal_http_hdr_get(hdr, hdr_len, "Connection", conn, sizeof(conn)) != 0) {
    return 0;
  }
  if (strstr(conn, "Upgrade") == NULL && strstr(conn, "upgrade") == NULL) {
    return 0;
  }
  return 1;
}

static int32_t ws_accept_key(const char *key, char *out, uint32_t out_cap)
{
  char    cat[128];
  uint8_t digest[20];
  size_t  olen;
  size_t  klen;

  if (key == NULL || out == NULL || out_cap < 32u) {
    return -1;
  }
  klen = strlen(key);
  if (klen + sizeof(kWsGuid) > sizeof(cat)) {
    return -1;
  }
  memcpy(cat, key, klen);
  memcpy(cat + klen, kWsGuid, sizeof(kWsGuid) - 1u);
  if (mbedtls_sha1((const unsigned char *)cat, klen + sizeof(kWsGuid) - 1u, digest) != 0) {
    return -1;
  }
  olen = 0;
  if (mbedtls_base64_encode((uint8_t *)out, out_cap, &olen, digest, sizeof(digest)) != 0) {
    return -1;
  }
  out[olen] = '\0';
  return 0;
}

int32_t pm_metal_net_asgi_ws_handshake(const char *hdr, uint32_t hdr_len)
{
  char    key[64];
  char    accept[40];
  char    resp[256];
  int32_t n;

  if (pm_metal_http_hdr_get(hdr, hdr_len, "Sec-WebSocket-Key", key, sizeof(key)) != 0) {
    return -1;
  }
  if (ws_accept_key(key, accept, sizeof(accept)) != 0) {
    return -1;
  }
  n = pm_metal_http_fmt_status(resp, sizeof(resp), 101, "Switching Protocols");
  if (n < 0) {
    return -1;
  }
  n = pm_metal_http_hdr_append(resp, sizeof(resp), (uint32_t)n, "Upgrade", "websocket");
  if (n < 0) {
    return -1;
  }
  n = pm_metal_http_hdr_append(resp, sizeof(resp), (uint32_t)n, "Connection", "Upgrade");
  if (n < 0) {
    return -1;
  }
  n = pm_metal_http_hdr_append(resp, sizeof(resp), (uint32_t)n, "Sec-WebSocket-Accept", accept);
  if (n < 0) {
    return -1;
  }
  n = pm_metal_http_hdr_end(resp, sizeof(resp), (uint32_t)n);
  if (n < 0) {
    return -1;
  }
  return pm_metal_net_asgi_conn_send(resp, (uint32_t)n);
}

/* Masked client frame -> unmasked payload echo as server text/binary. */
int32_t pm_metal_net_asgi_ws_echo_frame(
  const uint8_t *in, uint32_t in_len, uint8_t *out, uint32_t out_cap, uint32_t *out_len)
{
  uint8_t  opcode;
  uint8_t  masked;
  uint64_t plen;
  uint32_t off;
  uint8_t  mask[4];
  uint32_t i;

  if (in == NULL || out == NULL || out_len == NULL || in_len < 2u) {
    return -1;
  }
  opcode = in[0] & 0x0fu;
  masked = (in[1] & 0x80u) ? 1u : 0u;
  plen   = (uint64_t)(in[1] & 0x7fu);
  off    = 2;
  if (plen == 126u) {
    if (in_len < 4u) {
      return -1;
    }
    plen = ((uint64_t)in[2] << 8) | (uint64_t)in[3];
    off  = 4;
  } else if (plen == 127u) {
    return -1; /* reject huge frames */
  }
  if (!masked) {
    return -1;
  }
  if (in_len < off + 4u + (uint32_t)plen) {
    return -1;
  }
  memcpy(mask, in + off, 4);
  off += 4;
  if (opcode == 0x8u) {
    /* close */
    if (out_cap < 2u) {
      return -1;
    }
    out[0]   = 0x88u;
    out[1]   = 0u;
    *out_len = 2u;
    return 1; /* signal close */
  }
  if (out_cap < 2u + (uint32_t)plen) {
    return -1;
  }
  out[0] = (uint8_t)(0x80u | (opcode & 0x0fu));
  if (plen < 126u) {
    out[1] = (uint8_t)plen;
    for (i = 0; i < (uint32_t)plen; i++) {
      out[2u + i] = (uint8_t)(in[off + i] ^ mask[i & 3u]);
    }
    *out_len = 2u + (uint32_t)plen;
  } else {
    out[1] = 126u;
    out[2] = (uint8_t)((plen >> 8) & 0xffu);
    out[3] = (uint8_t)(plen & 0xffu);
    for (i = 0; i < (uint32_t)plen; i++) {
      out[4u + i] = (uint8_t)(in[off + i] ^ mask[i & 3u]);
    }
    *out_len = 4u + (uint32_t)plen;
  }
  return 0;
}
