/** @file
  HTTP/HTTPS GET client over pm_metal_net_* (async host coro + guest imports).
  (impl: efi|bios)
**/
#include <pymergetic/metal/dev/net/http.h>
#include <pymergetic/metal/dev/net/net.h>
#include <pymergetic/metal/dev/net/net_ops.h>
#include <pymergetic/metal/dev/net/tls.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <runtime/coro/coro.h>
#include <runtime/mem/mem.h>
#include <runtime/time/time.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>

#include "wasm_export.h"

#include <stddef.h>
#include <stdint.h>

#define HTTP_URL_MAX   384u
#define HTTP_HOST_MAX  128u
#define HTTP_PATH_MAX  256u
#define HTTP_IO_MAX    4096u
#define HTTP_HDR_MAX   8192u
#define HTTP_REQ_MAX   512u

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

typedef enum {
  CHUNK_SIZE = 0,
  CHUNK_DATA,
  CHUNK_AFTER_DATA,
  CHUNK_DONE
} chunk_step_t;

typedef struct {
  pm_metal_coro_t          coro;
  http_step_t              step;
  pm_metal_async_handle_t  aw;
  CHAR8                    url[HTTP_URL_MAX];
  CHAR8                    host[HTTP_HOST_MAX];
  CHAR8                    path[HTTP_PATH_MAX];
  UINT16                   port;
  INT32                    tls;
  pm_metal_net_sock_h      sock;
  VOID                    *body;
  UINT32                   body_cap;
  UINT32                   body_len;
  UINT32                   http_status;
  CHAR8                    hdr[HTTP_HDR_MAX];
  UINT32                   hdr_len;
  INT32                    hdr_done;
  INT32                    chunked;
  INT32                    body_until_close;
  UINT32                   content_len;
  chunk_step_t             chunk_step;
  UINT32                   chunk_rem;
  INT32                    chunk_zero;
  CHAR8                    chunk_line[16];
  UINT32                   chunk_line_len;
  INT32                    chunk_done;
  pm_metal_tls_wire_t      wire;
  pm_metal_tls_h           tls_h;
} http_get_t;

STATIC wasm_module_inst_t  mHttpInst;

STATIC struct {
  INT32   valid;
  UINT32  status;
  UINT32  body_len;
} mHttpLastDone;

STATIC pm_metal_status_t
HttpAwaitAsync (
  pm_metal_coro_t         *self,
  pm_metal_async_handle_t  aw_h
  )
{
  return (pm_metal_status_t)pm_metal_async_await_coro (self, aw_h);
}

STATIC UINT32
HttpChildResult (
  pm_metal_coro_t  *self
  )
{
  return (UINT32)(UINTN)self->result;
}

STATIC VOID
HttpTlsTeardown (
  http_get_t  *h
  )
{
  if (h == NULL) {
    return;
  }

  if (h->tls_h != PM_METAL_TLS_INVALID) {
    pm_metal_net_tls_close (h->tls_h);
    h->tls_h = PM_METAL_TLS_INVALID;
  }
}

STATIC INT32
HttpHexVal (
  CHAR8  c
  )
{
  if (c >= '0' && c <= '9') {
    return (INT32)(c - '0');
  }

  if (c >= 'a' && c <= 'f') {
    return 10 + (INT32)(c - 'a');
  }

  if (c >= 'A' && c <= 'F') {
    return 10 + (INT32)(c - 'A');
  }

  return -1;
}

STATIC INT32
HttpChunkInit (
  http_get_t  *h
  )
{
  if (h == NULL) {
    return -1;
  }

  h->chunk_step     = CHUNK_SIZE;
  h->chunk_rem      = 0;
  h->chunk_zero     = 0;
  h->chunk_line_len = 0;
  h->chunk_done     = 0;
  return 0;
}

STATIC INT32
HttpChunkFeedByte (
  http_get_t  *h,
  UINT8        b
  )
{
  INT32  hv;

  if (h == NULL) {
    return -1;
  }

  switch (h->chunk_step) {
  case CHUNK_SIZE:
    if (b == '\n') {
      UINT32  sz;
      UINT32  i;

      sz = 0;
      for (i = 0; i < h->chunk_line_len; i++) {
        CHAR8  c;

        c = h->chunk_line[i];
        if (c == ';') {
          break;
        }

        hv = HttpHexVal (c);
        if (hv < 0) {
          return -1;
        }

        sz = (sz << 4) + (UINT32)hv;
      }

      h->chunk_line_len = 0;
      h->chunk_zero     = (sz == 0) ? 1 : 0;
      if (sz == 0) {
        h->chunk_rem  = 2;
        h->chunk_step = CHUNK_AFTER_DATA;
      } else {
        h->chunk_rem  = sz;
        h->chunk_step = CHUNK_DATA;
      }

      return 0;
    }

    if (b == '\r') {
      return 0;
    }

    if (h->chunk_line_len + 1 >= sizeof (h->chunk_line)) {
      return -1;
    }

    h->chunk_line[h->chunk_line_len++] = (CHAR8)b;
    return 0;

  case CHUNK_DATA:
    if (h->body_len < h->body_cap && h->body != NULL) {
      ((UINT8 *)h->body)[h->body_len++] = b;
    }

    if (h->chunk_rem > 0) {
      h->chunk_rem--;
    }

    if (h->chunk_rem == 0) {
      h->chunk_rem  = 2;
      h->chunk_step = CHUNK_AFTER_DATA;
    }

    return 0;

  case CHUNK_AFTER_DATA:
    if (b != '\r' && b != '\n') {
      return -1;
    }

    if (h->chunk_rem > 0) {
      h->chunk_rem--;
    }

    if (h->chunk_rem == 0) {
      if (h->chunk_zero) {
        h->chunk_done = 1;
        h->chunk_step = CHUNK_DONE;
      } else {
        h->chunk_step = CHUNK_SIZE;
      }
    }

    return 0;

  default:
    return 0;
  }
}

STATIC INT32
HttpChunkFeed (
  http_get_t    *h,
  CONST UINT8   *data,
  UINT32         len
  )
{
  UINT32  i;

  if (h == NULL || data == NULL) {
    return -1;
  }

  for (i = 0; i < len; i++) {
    if (HttpChunkFeedByte (h, data[i]) != 0) {
      return -1;
    }

    if (h->chunk_done) {
      break;
    }
  }

  return 0;
}

STATIC INT32
HttpBodyFeed (
  http_get_t    *h,
  CONST UINT8   *data,
  UINT32         len
  )
{
  if (h == NULL || data == NULL || len == 0) {
    return 0;
  }

  if (h->chunked) {
    return HttpChunkFeed (h, data, len);
  }

  {
    UINT32  room;
    UINT32  copy;

    room = h->body_cap - h->body_len;
    if (h->body_until_close) {
      copy = len;
    } else {
      UINT32  need;

      need = h->content_len - h->body_len;
      copy = len < need ? len : need;
    }

    if (copy > room) {
      copy = room;
    }

    if (copy > 0 && h->body != NULL) {
      CopyMem ((UINT8 *)h->body + h->body_len, data, copy);
      h->body_len += copy;
    }
  }

  return 0;
}

STATIC INT32
HttpParseUrl (
  http_get_t   *h,
  CONST CHAR8  *url
  )
{
  CONST CHAR8  *p;
  CONST CHAR8  *host0;
  CONST CHAR8  *path0;
  UINTN         i;

  if (h == NULL || url == NULL) {
    return -1;
  }

  AsciiStrCpyS (h->url, sizeof (h->url), url);
  p = url;
  h->tls = 0;
  h->port = 80;

  if (AsciiStrnCmp (p, "https://", 8) == 0) {
    h->tls = 1;
    h->port = 443;
    p += 8;
  } else if (AsciiStrnCmp (p, "http://", 7) == 0) {
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

    i = (UINTN)(p - host0);
    if (i >= sizeof (h->host)) {
      return -1;
    }

    CopyMem (h->host, host0, i);
    h->host[i] = '\0';
    p++;
    if (*p == ':') {
      UINT32  port;

      p++;
      port = 0;
      while (*p >= '0' && *p <= '9') {
        port = port * 10u + (UINT32)(*p - '0');
        p++;
      }

      if (port == 0 || port > 65535u) {
        return -1;
      }

      h->port = (UINT16)port;
    }

    path0 = p;
    if (*path0 == '\0') {
      AsciiStrCpyS (h->path, sizeof (h->path), "/");
    } else {
      AsciiStrCpyS (h->path, sizeof (h->path), path0);
    }

    return 0;
  }

  while (*p != '\0' && *p != '/' && *p != ':') {
    p++;
  }

  if (p == host0) {
    return -1;
  }

  i = (UINTN)(p - host0);
  if (i >= sizeof (h->host)) {
    return -1;
  }

  CopyMem (h->host, host0, i);
  h->host[i] = '\0';

  if (*p == ':') {
    UINT32  port;

    p++;
    port = 0;
    while (*p >= '0' && *p <= '9') {
      port = port * 10u + (UINT32)(*p - '0');
      p++;
    }

    if (port == 0 || port > 65535u) {
      return -1;
    }

    h->port = (UINT16)port;
  }

  path0 = p;
  if (*path0 == '\0') {
    AsciiStrCpyS (h->path, sizeof (h->path), "/");
  } else {
    AsciiStrCpyS (h->path, sizeof (h->path), path0);
  }

  return 0;
}

STATIC INT32
HttpHostIsLiteral (
  CONST CHAR8  *host
  )
{
  UINT32        dots;
  CONST CHAR8  *p;

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

STATIC INT32
HttpFindHdrEnd (
  CONST CHAR8  *buf,
  UINT32        len
  )
{
  UINT32  i;

  for (i = 0; i + 3 < len; i++) {
    if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r'
        && buf[i + 3] == '\n')
    {
      return (INT32)(i + 4);
    }
  }

  return -1;
}

STATIC VOID
HttpParseResponse (
  http_get_t  *h
  )
{
  CONST CHAR8  *p;
  CONST CHAR8  *line;
  UINT32        i;

  if (h == NULL || h->hdr_len < 12) {
    return;
  }

  h->http_status = 0;
  if (AsciiStrnCmp (h->hdr, "HTTP/", 5) == 0) {
    p = h->hdr + 5;
    while (*p != '\0' && *p != ' ') {
      p++;
    }

    while (*p == ' ') {
      p++;
    }

    h->http_status = 0;
    while (*p >= '0' && *p <= '9') {
      h->http_status = h->http_status * 10u + (UINT32)(*p - '0');
      p++;
    }
  }

  h->content_len = 0;
  h->chunked     = 0;
  h->body_until_close = 0;
  line           = h->hdr;
  for (i = 0; i < h->hdr_len; ) {
    UINT32  j;

    j = i;
    while (j + 1 < h->hdr_len && !(h->hdr[j] == '\r' && h->hdr[j + 1] == '\n')) {
      j++;
    }

    if (AsciiStrnCmp (line, "Content-Length:", 15) == 0) {
      CONST CHAR8  *v;

      v = line + 15;
      while (*v == ' ') {
        v++;
      }

      h->content_len = 0;
      while (*v >= '0' && *v <= '9') {
        h->content_len = h->content_len * 10u + (UINT32)(*v - '0');
        v++;
      }
    } else if (AsciiStrnCmp (line, "Transfer-Encoding:", 18) == 0) {
      CONST CHAR8  *v;
      UINT32        k;

      v = line + 18;
      while (*v == ' ') {
        v++;
      }

      for (k = 0; v[k] != '\0' && v[k] != '\r'; k++) {
        if ((v[k] | 0x20) == 'c' && AsciiStrnCmp (v + k, "chunked", 7) == 0) {
          h->chunked = 1;
          break;
        }
      }
    }

    i = j + 2;
    line = h->hdr + i;
  }

  if (h->content_len == 0 && !h->chunked) {
    h->body_until_close = 1;
  }
}

STATIC INT32
HttpTlsHandshakeStep (
  http_get_t  *h
  )
{
  if (h == NULL || h->tls_h == PM_METAL_TLS_INVALID) {
    return -1;
  }

  return pm_metal_net_tls_handshake_step (h->tls_h);
}

STATIC INT32
HttpSendBytes (
  http_get_t   *h,
  CONST VOID   *data,
  UINT32        len
  )
{
  if (h == NULL || data == NULL || len == 0) {
    return -1;
  }

  if (h->tls) {
    return pm_metal_net_tls_write (h->tls_h, data, len);
  }

  {
    UINT32  sent;
    UINT32  spins;

    sent  = 0;
    spins = 0;
    while (sent < len) {
      UINT32  n;

      n = pm_metal_net_send (h->sock, (CONST UINT8 *)data + sent, len - sent);
      if (n > 0) {
        sent += n;
        spins = 0;
        continue;
      }

      pm_metal_net_poll ();
      if (++spins > 100000u) {
        return -1;
      }
    }
  }

  return 0;
}

STATIC pm_metal_status_t
HttpAfterHeadersParsed (
  http_get_t         *h,
  pm_metal_coro_t    *self,
  INT32               he
  )
{
  h->body_len = 0;

  if (h->chunked) {
    if (HttpChunkInit (h) != 0) {
      return PM_METAL_ERROR;
    }

    if (he >= 0 && (UINT32)he < h->hdr_len) {
      if (HttpBodyFeed (h, (CONST UINT8 *)h->hdr + he, h->hdr_len - (UINT32)he)
          != 0)
      {
        return PM_METAL_ERROR;
      }
    }

    if (h->chunk_done) {
      h->step = HTTP_STEP_DONE;
      (VOID)self;
      return PM_METAL_PENDING;
    }

    h->step = HTTP_STEP_RECV_BODY;
    (VOID)self;
    return PM_METAL_PENDING;
  }

  if (h->content_len > h->body_cap) {
    h->content_len = h->body_cap;
  }

  if (h->content_len > 0) {
    UINT32  body_in_hdr;

    body_in_hdr = h->hdr_len - (UINT32)he;
    if (body_in_hdr > 0) {
      UINT32  copy;

      copy = body_in_hdr;
      if (copy > h->content_len) {
        copy = h->content_len;
      }

      if (copy > 0 && h->body != NULL) {
        CopyMem (h->body, h->hdr + he, copy);
      }

      h->body_len = copy;
    }
  }

  if (h->body_until_close) {
    h->step = HTTP_STEP_RECV_BODY;
    (VOID)self;
    return PM_METAL_PENDING;
  }

  if (h->body_len >= h->content_len) {
    h->step = HTTP_STEP_DONE;
    return PM_METAL_PENDING;
  }

  h->step = HTTP_STEP_RECV_BODY;
  (VOID)self;
  return PM_METAL_PENDING;
}

STATIC pm_metal_status_t
HttpGetCoro (
  pm_metal_coro_t  *self
  )
{
  http_get_t  *h;
  INT32        he;
  UINT32       n;

  h = (http_get_t *)self;

  switch (h->step) {
  case HTTP_STEP_PARSE:
    if (HttpParseUrl (h, h->url) != 0) {
      return PM_METAL_ERROR;
    }

    h->sock      = PM_METAL_NET_SOCK_INVALID;
    h->body_len  = 0;
    h->http_status = 0;
    h->hdr_len   = 0;
    h->hdr_done  = 0;
    h->body_until_close = 0;
    h->chunked          = 0;
    h->chunk_done       = 0;
    h->wire.len         = 0;
    h->wire.off         = 0;
    h->tls_h            = PM_METAL_TLS_INVALID;

    if (HttpHostIsLiteral (h->host)) {
      h->step = HTTP_STEP_SOCK;
    } else {
      h->step = HTTP_STEP_DNS;
    }

    return PM_METAL_PENDING;

  case HTTP_STEP_DNS:
    h->aw = pm_metal_net_dns (h->host);
    if (h->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
      return PM_METAL_ERROR;
    }

    h->step = HTTP_STEP_DNS_AW;
    return HttpAwaitAsync (self, h->aw);

  case HTTP_STEP_DNS_AW:
    if (HttpChildResult (self) == 0) {
      return PM_METAL_ERROR;
    }

    h->step = HTTP_STEP_SOCK;
    return PM_METAL_PENDING;

  case HTTP_STEP_SOCK:
    {
      UINT32  domain;

      domain = PM_METAL_NET_AF_INET;
      if (AsciiStrStr (h->host, ":") != NULL) {
        domain = PM_METAL_NET_AF_INET6;
      }

      h->sock = pm_metal_net_socket (domain, PM_METAL_NET_SOCK_STREAM);
    }
    if (h->sock == PM_METAL_NET_SOCK_INVALID) {
      return PM_METAL_ERROR;
    }

    h->step = HTTP_STEP_CONNECT;
    return PM_METAL_PENDING;

  case HTTP_STEP_CONNECT:
    h->aw = pm_metal_net_connect (h->sock, h->host, h->port);
    if (h->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
      return PM_METAL_ERROR;
    }

    h->step = HTTP_STEP_CONNECT_AW;
    return HttpAwaitAsync (self, h->aw);

  case HTTP_STEP_CONNECT_AW:
    if (HttpChildResult (self) == 0) {
      return PM_METAL_ERROR;
    }

    if (h->tls) {
      h->tls_h = pm_metal_net_tls_open (h->host);
      if (h->tls_h == PM_METAL_TLS_INVALID) {
        return PM_METAL_ERROR;
      }

      if (pm_metal_net_tls_bind (h->tls_h, h->sock, &h->wire) != 0) {
        return PM_METAL_ERROR;
      }

      h->step = HTTP_STEP_TLS;
    } else {
      h->step = HTTP_STEP_SEND;
    }

    return PM_METAL_PENDING;

  case HTTP_STEP_TLS:
    he = HttpTlsHandshakeStep (h);
    if (he == 0) {
      h->step = HTTP_STEP_SEND;
      return PM_METAL_PENDING;
    }

    if (he < 0) {
      return PM_METAL_ERROR;
    }

    h->wire.len = 0;
    h->wire.off = 0;
    h->aw       = pm_metal_net_recv (h->sock, h->wire.buf, sizeof (h->wire.buf));
    if (h->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
      return PM_METAL_ERROR;
    }

    h->step = HTTP_STEP_WIRE_AW;
    return HttpAwaitAsync (self, h->aw);

  case HTTP_STEP_WIRE_AW:
    n = HttpChildResult (self);
    if (n == 0) {
      if (pm_metal_net_tls_handshake_done (h->tls_h) && h->hdr_done) {
        h->step = HTTP_STEP_DONE;
        return PM_METAL_PENDING;
      }

      if (!h->tls || pm_metal_net_tls_handshake_done (h->tls_h)) {
        if (h->body_until_close || h->chunked) {
          h->step = HTTP_STEP_DONE;
          return PM_METAL_PENDING;
        }
      }

      return PM_METAL_ERROR;
    }

    h->wire.len = n;
    h->wire.off = 0;
    if (h->tls && !pm_metal_net_tls_handshake_done (h->tls_h)) {
      h->step = HTTP_STEP_TLS;
    } else if (!h->hdr_done) {
      h->step = HTTP_STEP_RECV_HDR;
    } else {
      h->step = HTTP_STEP_RECV_BODY;
    }

    return PM_METAL_PENDING;

  case HTTP_STEP_SEND:
    {
      CHAR8  req[HTTP_REQ_MAX];
      INT32  v6;

      v6 = (AsciiStrStr (h->host, ":") != NULL) ? 1 : 0;
      if (v6) {
        AsciiSPrint (
          req,
          sizeof (req),
          "GET %a HTTP/1.1\r\nHost: [%a]\r\nConnection: close\r\n\r\n",
          h->path,
          h->host
          );
      } else {
        AsciiSPrint (
          req,
          sizeof (req),
          "GET %a HTTP/1.1\r\nHost: %a\r\nConnection: close\r\n\r\n",
          h->path,
          h->host
          );
      }
      if (HttpSendBytes (h, req, (UINT32)AsciiStrLen (req)) != 0) {
        return PM_METAL_ERROR;
      }

      h->hdr_len  = 0;
      h->hdr_done = 0;
      h->step     = HTTP_STEP_RECV_HDR;
    }

    return PM_METAL_PENDING;

  case HTTP_STEP_RECV_HDR:
    if (h->tls) {
      INT32  e;

      e = pm_metal_net_tls_read (
            h->tls_h,
            h->hdr + h->hdr_len,
            sizeof (h->hdr) - h->hdr_len - 1
            );
      if (e > 0) {
        h->hdr_len += (UINT32)e;
        h->hdr[h->hdr_len] = '\0';
      } else if (e == 0) {
        return PM_METAL_ERROR;
      } else if (e == PM_METAL_TLS_WANT_READ || e == PM_METAL_TLS_WANT_WRITE) {
        h->aw = pm_metal_net_recv (h->sock, h->wire.buf, sizeof (h->wire.buf));
        if (h->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
          return PM_METAL_ERROR;
        }

        h->step = HTTP_STEP_WIRE_AW;
        return HttpAwaitAsync (self, h->aw);
      } else {
        return PM_METAL_ERROR;
      }
    } else {
      h->aw = pm_metal_net_recv (
                h->sock,
                h->hdr + h->hdr_len,
                sizeof (h->hdr) - h->hdr_len - 1
                );
      if (h->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
        return PM_METAL_ERROR;
      }

      h->step = HTTP_STEP_RECV_HDR_AW;
      return HttpAwaitAsync (self, h->aw);
    }

    he = HttpFindHdrEnd (h->hdr, h->hdr_len);
    if (he < 0) {
      if (h->hdr_len + 256 >= sizeof (h->hdr)) {
        return PM_METAL_ERROR;
      }

      h->step = HTTP_STEP_RECV_HDR;
      return PM_METAL_PENDING;
    }

    h->hdr_done = 1;
    HttpParseResponse (h);
    return HttpAfterHeadersParsed (h, self, he);

  case HTTP_STEP_RECV_HDR_AW:
    n = HttpChildResult (self);
    if (n == 0) {
      return PM_METAL_ERROR;
    }

    h->hdr_len += n;
    h->hdr[h->hdr_len] = '\0';
    he = HttpFindHdrEnd (h->hdr, h->hdr_len);
    if (he < 0) {
      h->step = HTTP_STEP_RECV_HDR;
      return PM_METAL_PENDING;
    }

    h->hdr_done = 1;
    HttpParseResponse (h);
    return HttpAfterHeadersParsed (h, self, he);

  case HTTP_STEP_RECV_BODY:
    {
      UINT8   tmp[HTTP_IO_MAX];
      UINT32  want;
      INT32   got;

      if (h->chunked && h->chunk_done) {
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
        INT32  e;

        e = pm_metal_net_tls_read (h->tls_h, tmp, want);
        if (e > 0) {
          got = e;
        } else if (e == 0) {
          h->step = HTTP_STEP_DONE;
          return PM_METAL_PENDING;
        } else if (e == PM_METAL_TLS_WANT_READ || e == PM_METAL_TLS_WANT_WRITE) {
          h->aw = pm_metal_net_recv (h->sock, h->wire.buf, sizeof (h->wire.buf));
          if (h->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
            return PM_METAL_ERROR;
          }

          h->step = HTTP_STEP_WIRE_AW;
          return HttpAwaitAsync (self, h->aw);
        } else {
          return PM_METAL_ERROR;
        }
      } else {
        h->aw = pm_metal_net_recv (h->sock, h->wire.buf, want);
        if (h->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
          return PM_METAL_ERROR;
        }

        h->step = HTTP_STEP_RECV_BODY_AW;
        return HttpAwaitAsync (self, h->aw);
      }

      if (HttpBodyFeed (h, tmp, (UINT32)got) != 0) {
        return PM_METAL_ERROR;
      }

      if (h->chunked) {
        h->step = h->chunk_done ? HTTP_STEP_DONE : HTTP_STEP_RECV_BODY;
      } else if (h->body_until_close) {
        h->step = (h->body_len >= h->body_cap) ? HTTP_STEP_DONE : HTTP_STEP_RECV_BODY;
      } else {
        h->step = (h->body_len >= h->content_len) ? HTTP_STEP_DONE
                                                  : HTTP_STEP_RECV_BODY;
      }

      return PM_METAL_PENDING;
    }

  case HTTP_STEP_RECV_BODY_AW:
    n = HttpChildResult (self);
    if (n == 0) {
      h->step = HTTP_STEP_DONE;
      return PM_METAL_PENDING;
    }

    if (HttpBodyFeed (h, h->wire.buf, n) != 0) {
      return PM_METAL_ERROR;
    }

    if (h->chunked) {
      h->step = h->chunk_done ? HTTP_STEP_DONE : HTTP_STEP_RECV_BODY;
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
    if (h->sock != PM_METAL_NET_SOCK_INVALID) {
      pm_metal_net_close (h->sock);
      h->sock = PM_METAL_NET_SOCK_INVALID;
    }

    HttpTlsTeardown (h);
    mHttpLastDone.valid    = 1;
    mHttpLastDone.status   = h->http_status;
    mHttpLastDone.body_len = h->body_len;
    self->result = (VOID *)(UINTN)h->body_len;
    return PM_METAL_DONE;

  default:
    return PM_METAL_ERROR;
  }
}

STATIC VOID
HttpGetRelease (
  pm_metal_coro_t  *self
  )
{
  http_get_t  *h;

  h = (http_get_t *)self;
  if (h->sock != PM_METAL_NET_SOCK_INVALID) {
    pm_metal_net_close (h->sock);
    h->sock = PM_METAL_NET_SOCK_INVALID;
  }

  HttpTlsTeardown (h);
}

STATIC http_get_t *
HttpGetFromHandle (
  pm_metal_async_handle_t  hnd
  )
{
  return (http_get_t *)pm_metal_async_host_coro (hnd);
}

pm_metal_async_handle_t
pm_metal_net_http_get (
  CONST CHAR8  *url,
  VOID         *dest,
  UINT32        dest_cap
  )
{
  http_get_t  *h;

  if (url == NULL || dest_cap == 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  h = (http_get_t *)pm_metal_coro (HttpGetCoro, sizeof (*h));
  if (h == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  mHttpLastDone.valid = 0;
  h->coro.release = HttpGetRelease;
  h->step         = HTTP_STEP_PARSE;
  h->body         = dest;
  h->body_cap     = dest_cap;
  AsciiStrCpyS (h->url, sizeof (h->url), url);

  return pm_metal_async_adopt_host_coro (&h->coro);
}

uint32_t
pm_metal_net_http_status (
  pm_metal_async_handle_t  hnd
  )
{
  http_get_t  *h;

  h = HttpGetFromHandle (hnd);
  if (h != NULL) {
    return h->http_status;
  }

  if (mHttpLastDone.valid) {
    return mHttpLastDone.status;
  }

  return 0;
}

uint32_t
pm_metal_net_http_body_len (
  pm_metal_async_handle_t  hnd
  )
{
  http_get_t  *h;

  h = HttpGetFromHandle (hnd);
  if (h != NULL) {
    if (h->coro.status == PM_METAL_DONE && h->coro.result != NULL) {
      return (uint32_t)(UINTN)h->coro.result;
    }

    return h->body_len;
  }

  if (mHttpLastDone.valid) {
    return mHttpLastDone.body_len;
  }

  return 0;
}

STATIC INT32
HttpGuestCopyUrl (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *url,
  CHAR8           *out,
  UINTN            out_sz
  )
{
  wasm_module_inst_t  inst;
  UINTN               i;

  inst = wasm_runtime_get_module_inst (exec_env);
  if (inst == NULL || url == NULL || out == NULL || out_sz == 0) {
    return -1;
  }

  if (!wasm_runtime_validate_native_addr (inst, (VOID *)url, 1)) {
    return -1;
  }

  for (i = 0; i + 1 < out_sz; i++) {
    if (!wasm_runtime_validate_native_addr (inst, (VOID *)(url + i), 1)) {
      return -1;
    }

    out[i] = url[i];
    if (url[i] == '\0') {
      return 0;
    }
  }

  return -1;
}

STATIC UINT32
pm_metal_net_http_get_native (
  wasm_exec_env_t  exec_env,
  CONST CHAR8     *url,
  UINT32           dest,
  UINT32           dest_cap
  )
{
  CHAR8  cleaned[HTTP_URL_MAX];
  VOID  *native;

  if (mHttpInst == NULL || dest_cap == 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  if (HttpGuestCopyUrl (exec_env, url, cleaned, sizeof (cleaned)) != 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  if (!wasm_runtime_validate_app_addr (mHttpInst, dest, dest_cap)) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  native = wasm_runtime_addr_app_to_native (mHttpInst, dest);
  return pm_metal_net_http_get (cleaned, native, dest_cap);
}

STATIC UINT32
pm_metal_net_http_status_native (
  wasm_exec_env_t  exec_env,
  UINT32           hnd
  )
{
  (VOID)exec_env;
  return pm_metal_net_http_status (hnd);
}

STATIC UINT32
pm_metal_net_http_body_len_native (
  wasm_exec_env_t  exec_env,
  UINT32           hnd
  )
{
  (VOID)exec_env;
  return pm_metal_net_http_body_len (hnd);
}

STATIC NativeSymbol g_pm_metal_net_http_native_symbols[] = {
  { "pm_metal_net_http_get", (VOID *)pm_metal_net_http_get_native, "($ii)i", NULL },
  { "pm_metal_net_http_status", (VOID *)pm_metal_net_http_status_native, "(i)i", NULL },
  { "pm_metal_net_http_body_len", (VOID *)pm_metal_net_http_body_len_native, "(i)i", NULL },
};

int
pm_metal_net_http_native_register (
  VOID
  )
{
  if (!wasm_runtime_register_natives (
         PM_METAL_NET_HTTP_WASI_MODULE,
         g_pm_metal_net_http_native_symbols,
         sizeof (g_pm_metal_net_http_native_symbols)
           / sizeof (g_pm_metal_net_http_native_symbols[0])
         ))
  {
    return -1;
  }

  return 0;
}

void
pm_metal_net_http_bind_inst (
  VOID  *module_inst
  )
{
  mHttpInst = (wasm_module_inst_t)module_inst;
}
