/** @file
  HTTP/HTTPS GET client over pm_metal_net_* (async host coro + guest imports).
  (impl: efi|bios)
**/
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/net/http/http.h>
#include <pymergetic/metal/net/http/http_parse.h>
#include <pymergetic/metal/net/ip/ip.h>
#include <pymergetic/metal/net/ip/ip_ops.h>
#include <pymergetic/metal/net/tls/tls.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/runtime/mem/mem.h>
#include <runtime/time/time.h>

#include "wasm_export.h"

#include <stddef.h>
#include <stdint.h>

#include <pymergetic/metal/net/io_budget.h>

#define HTTP_URL_MAX  384u
#define HTTP_HOST_MAX 128u
#define HTTP_PATH_MAX 256u
#define HTTP_IO_MAX   PM_METAL_IO_WIRE_MAX
#define HTTP_HDR_MAX  8192u
#define HTTP_REQ_MAX  512u

typedef enum {
  HTTP_STEP_PARSE = 0,
  HTTP_STEP_DNS,
  HTTP_STEP_DNS_AW,
  HTTP_STEP_SOCK,
  HTTP_STEP_CONNECT,
  HTTP_STEP_CONNECT_AW,
  HTTP_STEP_TLS,
  HTTP_STEP_SEND,
  HTTP_STEP_RECV_HDR,
  HTTP_STEP_RECV_HDR_AW,
  HTTP_STEP_RECV_BODY,
  HTTP_STEP_RECV_BODY_AW,
  HTTP_STEP_WIRE_AW,
  HTTP_STEP_DONE
} http_step_t;

typedef struct {
  http_step_t             step;
  pm_metal_async_handle_t aw;
  char                    url[HTTP_URL_MAX];
  char                    host[HTTP_HOST_MAX];
  char                    path[HTTP_PATH_MAX];
  uint16_t                port;
  int32_t                 tls;
  pm_metal_net_ip_sock_h     sock;
  void                   *body;
  uint32_t                body_cap;
  uint32_t                body_len;
  uint32_t                http_status;
  char                    hdr[HTTP_HDR_MAX];
  uint32_t                hdr_len;
  int32_t                 hdr_done;
  int32_t                 chunked;
  int32_t                 body_until_close;
  uint32_t                content_len;
  pm_metal_http_chunk_dec_t chunk;
  char                    req[HTTP_REQ_MAX];
  uint32_t                req_len;
  uint32_t                req_off;
  pm_metal_net_tls_wire_t     wire;
  pm_metal_net_tls_h          tls_h;
  uint8_t                     io[HTTP_IO_MAX];
} http_get_t;

static struct {
  int32_t  valid;
  uint32_t status;
  uint32_t body_len;
} mHttpLastDone;

static pm_metal_status_t HttpAwaitAsync(pm_metal_async_handle_t self_h,
                                        pm_metal_async_handle_t aw_h)
{
  return pm_metal_async_await(self_h, aw_h);
}

static void HttpTlsTeardown(http_get_t *h)
{
  if (h == NULL) {
    return;
  }

  if (h->tls_h != PM_METAL_TLS_INVALID) {
    pm_metal_net_tls_close(h->tls_h);
    h->tls_h = PM_METAL_TLS_INVALID;
  }
}

static int32_t HttpBodyFeed(http_get_t *h, const uint8_t *data, uint32_t len)
{
  if (h == NULL || data == NULL || len == 0) {
    return 0;
  }

  if (h->chunked) {
    return pm_metal_http_chunk_dec_feed(
      &h->chunk, data, len, h->body, h->body_cap, &h->body_len);
  }

  {
    uint32_t room;
    uint32_t copy;

    room = h->body_cap - h->body_len;
    if (h->body_until_close) {
      copy = len;
    } else {
      uint32_t need;

      need = h->content_len - h->body_len;
      copy = len < need ? len : need;
    }

    if (copy > room) {
      copy = room;
    }

    if (copy > 0 && h->body != NULL) {
      memcpy((uint8_t *)h->body + h->body_len, data, copy);
      h->body_len += copy;
    }
  }

  return 0;
}

static int32_t HttpParseUrl(http_get_t *h, const char *url)
{
  const char *p;
  const char *host0;
  const char *path0;
  uintptr_t   i;

  if (h == NULL || url == NULL) {
    return -1;
  }

  snprintf(h->url, sizeof(h->url), "%s", url);
  p       = url;
  h->tls  = 0;
  h->port = 80;

  if (strncmp(p, "https://", 8) == 0) {
    h->tls  = 1;
    h->port = 443;
    p += 8;
  } else if (strncmp(p, "http://", 7) == 0) {
    p += 7;
  } else {
    return -1;
  }

  host0 = p;
  if (*p == '[') {
    p++;
    host0 = p;
    while (*p != '\0' && *p != ']') {
      p++;
    }

    if (*p != ']') {
      return -1;
    }

    i = (uintptr_t)(p - host0);
    if (i >= sizeof(h->host)) {
      return -1;
    }

    memcpy(h->host, host0, i);
    h->host[i] = '\0';
    p++;
    if (*p == ':') {
      uint32_t port;

      p++;
      port = 0;
      while (*p >= '0' && *p <= '9') {
        port = port * 10u + (uint32_t)(*p - '0');
        p++;
      }

      if (port == 0 || port > 65535u) {
        return -1;
      }

      h->port = (uint16_t)port;
    }

    path0 = p;
    if (*path0 == '\0') {
      snprintf(h->path, sizeof(h->path), "%s", "/");
    } else {
      snprintf(h->path, sizeof(h->path), "%s", path0);
    }

    return 0;
  }

  while (*p != '\0' && *p != '/' && *p != ':') {
    p++;
  }

  if (p == host0) {
    return -1;
  }

  i = (uintptr_t)(p - host0);
  if (i >= sizeof(h->host)) {
    return -1;
  }

  memcpy(h->host, host0, i);
  h->host[i] = '\0';

  if (*p == ':') {
    uint32_t port;

    p++;
    port = 0;
    while (*p >= '0' && *p <= '9') {
      port = port * 10u + (uint32_t)(*p - '0');
      p++;
    }

    if (port == 0 || port > 65535u) {
      return -1;
    }

    h->port = (uint16_t)port;
  }

  path0 = p;
  if (*path0 == '\0') {
    snprintf(h->path, sizeof(h->path), "%s", "/");
  } else {
    snprintf(h->path, sizeof(h->path), "%s", path0);
  }

  return 0;
}

static int32_t HttpHostIsLiteral(const char *host)
{
  uint32_t    dots;
  const char *p;

  dots = 0;
  for (p = host; *p != '\0'; p++) {
    if (*p == '.') {
      dots++;
    } else if (*p == ':') {
      return 1;
    } else if (*p < '0' || *p > '9') {
      if ((*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
        return 1;
      }

      return 0;
    }
  }

  return dots == 3;
}

static void HttpParseResponse(http_get_t *h)
{
  pm_metal_net_http_body_mode_t mode;

  if (h == NULL) {
    return;
  }

  h->http_status = pm_metal_http_parse_status(h->hdr, h->hdr_len);
  pm_metal_net_http_scan_body_mode(h->hdr, h->hdr_len, &mode);
  h->content_len      = mode.content_len;
  h->chunked          = mode.chunked;
  h->body_until_close = mode.body_until_close;
}

static int32_t HttpTlsHandshakeStep(http_get_t *h)
{
  if (h == NULL || h->tls_h == PM_METAL_TLS_INVALID) {
    return -1;
  }

  return pm_metal_net_tls_handshake_step(h->tls_h);
}

static int32_t HttpAfterHeadersParsed(http_get_t *h, int32_t he)
{
  h->body_len = 0;

  if (h->chunked) {
    pm_metal_http_chunk_dec_init(&h->chunk);

    if (he >= 0 && (uint32_t)he < h->hdr_len) {
      if (HttpBodyFeed(h, (const uint8_t *)h->hdr + he, h->hdr_len - (uint32_t)he) != 0) {
        return PM_METAL_ERROR;
      }
    }

    if (h->chunk.done) {
      h->step = HTTP_STEP_DONE;
      return PM_METAL_PENDING;
    }

    h->step = HTTP_STEP_RECV_BODY;
    return PM_METAL_PENDING;
  }

  if (h->content_len > h->body_cap) {
    h->content_len = h->body_cap;
  }

  if (h->content_len > 0) {
    uint32_t body_in_hdr;

    body_in_hdr = h->hdr_len - (uint32_t)he;
    if (body_in_hdr > 0) {
      uint32_t copy;

      copy = body_in_hdr;
      if (copy > h->content_len) {
        copy = h->content_len;
      }

      if (copy > 0 && h->body != NULL) {
        memcpy(h->body, h->hdr + he, copy);
      }

      h->body_len = copy;
    }
  }

  if (h->body_until_close) {
    h->step = HTTP_STEP_RECV_BODY;
    return PM_METAL_PENDING;
  }

  if (h->body_len >= h->content_len) {
    h->step = HTTP_STEP_DONE;
    return PM_METAL_PENDING;
  }

  h->step = HTTP_STEP_RECV_BODY;
  return PM_METAL_PENDING;
}

static pm_metal_status_t HttpGetStep(pm_metal_async_handle_t self_h)
{
  http_get_t *h;
  int32_t     he;
  uint32_t    n;

  h = (http_get_t *)(uintptr_t)pm_metal_async_coro_state(self_h);
  if (h == NULL) {
    return PM_METAL_ERROR;
  }

  switch (h->step) {
  case HTTP_STEP_PARSE:
    if (HttpParseUrl(h, h->url) != 0) {
      return PM_METAL_ERROR;
    }

    h->sock             = PM_METAL_NET_IP_SOCK_INVALID;
    h->body_len         = 0;
    h->http_status      = 0;
    h->hdr_len          = 0;
    h->hdr_done         = 0;
    h->body_until_close = 0;
    h->chunked          = 0;
    pm_metal_http_chunk_dec_init(&h->chunk);
    h->wire.len         = 0;
    h->wire.off         = 0;
    h->tls_h            = PM_METAL_TLS_INVALID;
    h->req_len          = 0;
    h->req_off          = 0;

    if (HttpHostIsLiteral(h->host)) {
      h->step = HTTP_STEP_SOCK;
    } else {
      h->step = HTTP_STEP_DNS;
    }

    return PM_METAL_PENDING;

  case HTTP_STEP_DNS:
    h->aw = pm_metal_net_ip_dns(h->host);
    if (h->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
      return PM_METAL_ERROR;
    }

    h->step = HTTP_STEP_DNS_AW;
    return HttpAwaitAsync(self_h, h->aw);

  case HTTP_STEP_DNS_AW:
    if (pm_metal_async_result_u32(self_h) == 0) {
      return PM_METAL_ERROR;
    }

    h->step = HTTP_STEP_SOCK;
    return PM_METAL_PENDING;

  case HTTP_STEP_SOCK: {
    uint32_t domain;

    domain = PM_METAL_NET_IP_AF_INET;
    if (strstr(h->host, ":") != NULL) {
      domain = PM_METAL_NET_IP_AF_INET6;
    }

    h->sock = pm_metal_net_ip_socket(domain, PM_METAL_NET_IP_SOCK_STREAM);
  }
    if (h->sock == PM_METAL_NET_IP_SOCK_INVALID) {
      return PM_METAL_ERROR;
    }

    h->step = HTTP_STEP_CONNECT;
    return PM_METAL_PENDING;

  case HTTP_STEP_CONNECT:
    h->aw = pm_metal_net_ip_connect(h->sock, h->host, h->port);
    if (h->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
      return PM_METAL_ERROR;
    }

    h->step = HTTP_STEP_CONNECT_AW;
    return HttpAwaitAsync(self_h, h->aw);

  case HTTP_STEP_CONNECT_AW:
    if (pm_metal_async_result_u32(self_h) == 0) {
      return PM_METAL_ERROR;
    }

    if (h->tls) {
      h->tls_h = pm_metal_net_tls_open(h->host);
      if (h->tls_h == PM_METAL_TLS_INVALID) {
        return PM_METAL_ERROR;
      }

      if (pm_metal_net_tls_bind(h->tls_h, h->sock, &h->wire) != 0) {
        return PM_METAL_ERROR;
      }

      h->step = HTTP_STEP_TLS;
    } else {
      h->step = HTTP_STEP_SEND;
    }

    return PM_METAL_PENDING;

  case HTTP_STEP_TLS:
    he = HttpTlsHandshakeStep(h);
    if (he == 0) {
      h->step = HTTP_STEP_SEND;
      return PM_METAL_PENDING;
    }

    if (he < 0) {
      return PM_METAL_ERROR;
    }

    h->wire.len = 0;
    h->wire.off = 0;
    h->aw       = pm_metal_net_ip_recv(h->sock, h->wire.buf, sizeof(h->wire.buf));
    if (h->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
      return PM_METAL_ERROR;
    }

    h->step = HTTP_STEP_WIRE_AW;
    return HttpAwaitAsync(self_h, h->aw);

  case HTTP_STEP_WIRE_AW:
    n = pm_metal_async_result_u32(self_h);
    if (n == 0) {
      /* Mid-request send may only need a write flush; retry SEND. */
      if (h->req_len > 0 && h->req_off < h->req_len) {
        h->step = HTTP_STEP_SEND;
        return PM_METAL_PENDING;
      }

      if (pm_metal_net_tls_handshake_done(h->tls_h) && h->hdr_done) {
        h->step = HTTP_STEP_DONE;
        return PM_METAL_PENDING;
      }

      if (!h->tls || pm_metal_net_tls_handshake_done(h->tls_h)) {
        if (h->body_until_close || h->chunked) {
          h->step = HTTP_STEP_DONE;
          return PM_METAL_PENDING;
        }
      }

      return PM_METAL_ERROR;
    }

    h->wire.len = n;
    h->wire.off = 0;
    if (h->req_len > 0 && h->req_off < h->req_len) {
      h->step = HTTP_STEP_SEND;
    } else if (h->tls && !pm_metal_net_tls_handshake_done(h->tls_h)) {
      h->step = HTTP_STEP_TLS;
    } else if (!h->hdr_done) {
      h->step = HTTP_STEP_RECV_HDR;
    } else {
      h->step = HTTP_STEP_RECV_BODY;
    }

    return PM_METAL_PENDING;

  case HTTP_STEP_SEND:
    if (h->req_len == 0) {
      int32_t v6;

      v6 = (strstr(h->host, ":") != NULL) ? 1 : 0;
      if (v6) {
        snprintf(h->req,
                 sizeof(h->req),
                 "GET %s HTTP/1.1\r\nHost: [%s]\r\nConnection: close\r\n\r\n",
                 h->path,
                 h->host);
      } else {
        snprintf(h->req,
                 sizeof(h->req),
                 "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                 h->path,
                 h->host);
      }

      h->req_len = (uint32_t)strlen(h->req);
      h->req_off = 0;
      if (h->req_len == 0) {
        return PM_METAL_ERROR;
      }
    }

    if (h->tls) {
      int32_t e;

      e = pm_metal_net_tls_write(h->tls_h, h->req + h->req_off, h->req_len - h->req_off);
      if (e > 0) {
        h->req_off += (uint32_t)e;
        if (h->req_off >= h->req_len) {
          h->hdr_len  = 0;
          h->hdr_done = 0;
          h->step     = HTTP_STEP_RECV_HDR;
        }

        return PM_METAL_PENDING;
      }

      if (e == PM_METAL_TLS_WANT_READ) {
        h->aw = pm_metal_net_ip_recv(h->sock, h->wire.buf, sizeof(h->wire.buf));
        if (h->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
          return PM_METAL_ERROR;
        }

        h->step = HTTP_STEP_WIRE_AW;
        return HttpAwaitAsync(self_h, h->aw);
      }

      if (e == PM_METAL_TLS_WANT_WRITE) {
        return pm_metal_async_await(self_h, pm_metal_async_sleep_us(2000));
      }

      return PM_METAL_ERROR;
    }

    {
      uint32_t nsend;

      nsend = pm_metal_net_ip_send(h->sock, h->req + h->req_off, h->req_len - h->req_off);
      if (nsend > 0) {
        h->req_off += nsend;
        if (h->req_off >= h->req_len) {
          h->hdr_len  = 0;
          h->hdr_done = 0;
          h->step     = HTTP_STEP_RECV_HDR;
        }

        return PM_METAL_PENDING;
      }
    }

    /* TCP send buffer full — cooperative backoff. */
    return pm_metal_async_await(self_h, pm_metal_async_sleep_us(2000));

  case HTTP_STEP_RECV_HDR:
    if (h->tls) {
      int32_t e;

      e = pm_metal_net_tls_read(h->tls_h, h->hdr + h->hdr_len, sizeof(h->hdr) - h->hdr_len - 1);
      if (e > 0) {
        h->hdr_len += (uint32_t)e;
        h->hdr[h->hdr_len] = '\0';
      } else if (e == 0) {
        return PM_METAL_ERROR;
      } else if (e == PM_METAL_TLS_WANT_READ || e == PM_METAL_TLS_WANT_WRITE) {
        h->aw = pm_metal_net_ip_recv(h->sock, h->wire.buf, sizeof(h->wire.buf));
        if (h->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
          return PM_METAL_ERROR;
        }

        h->step = HTTP_STEP_WIRE_AW;
        return HttpAwaitAsync(self_h, h->aw);
      } else {
        return PM_METAL_ERROR;
      }
    } else {
      h->aw = pm_metal_net_ip_recv(h->sock, h->hdr + h->hdr_len, sizeof(h->hdr) - h->hdr_len - 1);
      if (h->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
        return PM_METAL_ERROR;
      }

      h->step = HTTP_STEP_RECV_HDR_AW;
      return HttpAwaitAsync(self_h, h->aw);
    }

    he = pm_metal_http_find_hdr_end(h->hdr, h->hdr_len);
    if (he < 0) {
      if (h->hdr_len + 256 >= sizeof(h->hdr)) {
        return PM_METAL_ERROR;
      }

      h->step = HTTP_STEP_RECV_HDR;
      return PM_METAL_PENDING;
    }

    h->hdr_done = 1;
    HttpParseResponse(h);
    return HttpAfterHeadersParsed(h, he);

  case HTTP_STEP_RECV_HDR_AW:
    n = pm_metal_async_result_u32(self_h);
    if (n == 0) {
      return PM_METAL_ERROR;
    }

    h->hdr_len += n;
    h->hdr[h->hdr_len] = '\0';
    he                 = pm_metal_http_find_hdr_end(h->hdr, h->hdr_len);
    if (he < 0) {
      h->step = HTTP_STEP_RECV_HDR;
      return PM_METAL_PENDING;
    }

    h->hdr_done = 1;
    HttpParseResponse(h);
    return HttpAfterHeadersParsed(h, he);

  case HTTP_STEP_RECV_BODY: {
    uint8_t *tmp;
    uint32_t want;
    int32_t  got;

    tmp = h->io;
    if (h->chunked && h->chunk.done) {
      h->step = HTTP_STEP_DONE;
      return PM_METAL_PENDING;
    }

    want = HTTP_IO_MAX;
    if (!h->chunked) {
      if (h->body_until_close) {
        want = h->body_cap - h->body_len;
      } else {
        want = h->content_len - h->body_len;
      }
    }

    if (want > HTTP_IO_MAX) {
      want = HTTP_IO_MAX;
    }

    if (!h->chunked && want == 0) {
      h->step = HTTP_STEP_DONE;
      return PM_METAL_PENDING;
    }

    got = 0;
    if (h->tls) {
      int32_t e;

      e = pm_metal_net_tls_read(h->tls_h, tmp, want);
      if (e > 0) {
        got = e;
      } else if (e == 0) {
        h->step = HTTP_STEP_DONE;
        return PM_METAL_PENDING;
      } else if (e == PM_METAL_TLS_WANT_READ || e == PM_METAL_TLS_WANT_WRITE) {
        h->aw = pm_metal_net_ip_recv(h->sock, h->wire.buf, sizeof(h->wire.buf));
        if (h->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
          return PM_METAL_ERROR;
        }

        h->step = HTTP_STEP_WIRE_AW;
        return HttpAwaitAsync(self_h, h->aw);
      } else {
        return PM_METAL_ERROR;
      }
    } else {
      h->aw = pm_metal_net_ip_recv(h->sock, h->wire.buf, want);
      if (h->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
        return PM_METAL_ERROR;
      }

      h->step = HTTP_STEP_RECV_BODY_AW;
      return HttpAwaitAsync(self_h, h->aw);
    }

    if (HttpBodyFeed(h, tmp, (uint32_t)got) != 0) {
      return PM_METAL_ERROR;
    }

    if (h->chunked) {
      h->step = h->chunk.done ? HTTP_STEP_DONE : HTTP_STEP_RECV_BODY;
    } else if (h->body_until_close) {
      h->step = (h->body_len >= h->body_cap) ? HTTP_STEP_DONE : HTTP_STEP_RECV_BODY;
    } else {
      h->step = (h->body_len >= h->content_len) ? HTTP_STEP_DONE : HTTP_STEP_RECV_BODY;
    }

    return PM_METAL_PENDING;
  }

  case HTTP_STEP_RECV_BODY_AW:
    n = pm_metal_async_result_u32(self_h);
    if (n == 0) {
      h->step = HTTP_STEP_DONE;
      return PM_METAL_PENDING;
    }

    if (HttpBodyFeed(h, h->wire.buf, n) != 0) {
      return PM_METAL_ERROR;
    }

    if (h->chunked) {
      h->step = h->chunk.done ? HTTP_STEP_DONE : HTTP_STEP_RECV_BODY;
    } else if (h->body_until_close) {
      if (h->body_len >= h->body_cap) {
        h->step = HTTP_STEP_DONE;
      } else {
        h->step = HTTP_STEP_RECV_BODY;
      }
    } else if (h->body_len >= h->content_len) {
      h->step = HTTP_STEP_DONE;
    } else {
      h->step = HTTP_STEP_RECV_BODY;
    }

    return PM_METAL_PENDING;

  case HTTP_STEP_DONE:
    if (h->sock != PM_METAL_NET_IP_SOCK_INVALID) {
      pm_metal_net_ip_close(h->sock);
      h->sock = PM_METAL_NET_IP_SOCK_INVALID;
    }

    HttpTlsTeardown(h);
    mHttpLastDone.valid    = 1;
    mHttpLastDone.status   = h->http_status;
    mHttpLastDone.body_len = h->body_len;
    pm_metal_async_set_result_u32(self_h, h->body_len);
    return PM_METAL_DONE;

  default:
    return PM_METAL_ERROR;
  }
}

static void HttpGetRelease(void *state)
{
  http_get_t *h;

  h = (http_get_t *)state;
  if (h->sock != PM_METAL_NET_IP_SOCK_INVALID) {
    pm_metal_net_ip_close(h->sock);
    h->sock = PM_METAL_NET_IP_SOCK_INVALID;
  }

  HttpTlsTeardown(h);
}

static http_get_t *HttpGetFromHandle(pm_metal_async_handle_t hnd)
{
  return (http_get_t *)(uintptr_t)pm_metal_async_coro_state(hnd);
}

pm_metal_async_handle_t pm_metal_net_http_get(const char *url, void *dest, uint32_t dest_cap)
{
  http_get_t             *h;
  pm_metal_async_handle_t ah;

  if (url == NULL || dest_cap == 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  ah = pm_metal_async_coro_create(HttpGetStep, sizeof(*h));
  if (ah == PM_METAL_ASYNC_HANDLE_INVALID) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  h = (http_get_t *)(uintptr_t)pm_metal_async_coro_state(ah);
  if (h == NULL) {
    pm_metal_async_coro_close(ah);
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  mHttpLastDone.valid = 0;
  h->step             = HTTP_STEP_PARSE;
  h->body             = dest;
  h->body_cap         = dest_cap;
  snprintf(h->url, sizeof(h->url), "%s", url);
  pm_metal_async_coro_set_release(ah, HttpGetRelease);
  return ah;
}

uint32_t pm_metal_net_http_status(pm_metal_async_handle_t hnd)
{
  http_get_t *h;

  h = HttpGetFromHandle(hnd);
  if (h != NULL) {
    return h->http_status;
  }

  if (mHttpLastDone.valid) {
    return mHttpLastDone.status;
  }

  return 0;
}

uint32_t pm_metal_net_http_body_len(pm_metal_async_handle_t hnd)
{
  http_get_t *h;

  h = HttpGetFromHandle(hnd);
  if (h != NULL) {
    return h->body_len;
  }

  if (mHttpLastDone.valid) {
    return mHttpLastDone.body_len;
  }

  return 0;
}

static int32_t HttpGuestCopyUrl(wasm_exec_env_t exec_env,
                                const char     *url,
                                char           *out,
                                uintptr_t       out_sz)
{
  wasm_module_inst_t inst;
  uintptr_t          i;

  inst = wasm_runtime_get_module_inst(exec_env);
  if (inst == NULL || url == NULL || out == NULL || out_sz == 0) {
    return -1;
  }

  if (!wasm_runtime_validate_native_addr(inst, (void *)url, 1)) {
    return -1;
  }

  for (i = 0; i + 1 < out_sz; i++) {
    if (!wasm_runtime_validate_native_addr(inst, (void *)(url + i), 1)) {
      return -1;
    }

    out[i] = url[i];
    if (url[i] == '\0') {
      return 0;
    }
  }

  return -1;
}

static uint32_t pm_metal_net_http_get_native(wasm_exec_env_t exec_env,
                                             const char     *url,
                                             uint32_t        dest,
                                             uint32_t        dest_cap)
{
  char  cleaned[HTTP_URL_MAX];
  void *native;

  if (dest_cap == 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  if (HttpGuestCopyUrl(exec_env, url, cleaned, sizeof(cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  native = pm_metal_async_guest_buf_durable(exec_env, dest, dest_cap);
  if (native == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_net_http_get(cleaned, native, dest_cap);
}

static uint32_t pm_metal_net_http_status_native(wasm_exec_env_t exec_env, uint32_t hnd)
{
  (void)exec_env;
  return pm_metal_net_http_status(hnd);
}

static uint32_t pm_metal_net_http_body_len_native(wasm_exec_env_t exec_env, uint32_t hnd)
{
  (void)exec_env;
  return pm_metal_net_http_body_len(hnd);
}

static NativeSymbol g_pm_metal_net_http_native_symbols[] = {
  { "pm_metal_net_http_get", (void *)pm_metal_net_http_get_native, "($ii)i", NULL },
  { "pm_metal_net_http_status", (void *)pm_metal_net_http_status_native, "(i)i", NULL },
  { "pm_metal_net_http_body_len", (void *)pm_metal_net_http_body_len_native, "(i)i", NULL },
};

int32_t pm_metal_net_http_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_NET_HTTP_WASI_MODULE,
                                     g_pm_metal_net_http_native_symbols,
                                     sizeof(g_pm_metal_net_http_native_symbols) /
                                       sizeof(g_pm_metal_net_http_native_symbols[0]))) {
    return -1;
  }

  return 0;
}
