#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <pymergetic/metal/async/handle.h>
#include <pymergetic/metal/async/coro.h>
#include <pymergetic/metal/async/await.h>
#include <pymergetic/metal/async/time.h>
#include <pymergetic/metal/net/http/__init__.h>
#include <pymergetic/metal/net/ip/__init__.h>

#include "lwipopts.h" /* IWYU pragma: keep */
#include <lwip/err.h>
#include <lwip/ip4_addr.h>
#include <lwip/ip_addr.h>
#include <lwip/pbuf.h>
#include <lwip/tcp.h>

#define HTTP_URL_MAX     384u
#define HTTP_HOST_MAX    128u
#define HTTP_PATH_MAX    256u
#define HTTP_HEADER_MAX  4096u
#define HTTP_REQUEST_MAX 512u
#define HTTP_CONNECT_US  10000000ull
#define HTTP_IDLE_US     30000000ull

typedef enum {
  HTTP_PARSE = 0,
  HTTP_DNS_WAIT,
  HTTP_CONNECT_START,
  HTTP_CONNECT_WAIT,
  HTTP_SEND,
  HTTP_RECV
} http_phase_t;

typedef struct {
  http_phase_t    phase;
  char            url[HTTP_URL_MAX];
  char            host[HTTP_HOST_MAX];
  char            path[HTTP_PATH_MAX];
  uint16_t        port;
  uint32_t        child_h;
  uint64_t        deadline;
  ip_addr_t       server;
  struct tcp_pcb *pcb;
  int32_t         connected;
  int32_t         transport_error;
  int32_t         remote_closed;
  int32_t         complete;
  char            request[HTTP_REQUEST_MAX];
  uint32_t        request_len;
  char            header[HTTP_HEADER_MAX];
  uint32_t        header_len;
  int32_t         headers_done;
  int32_t         have_content_len;
  uint32_t        content_len;
  uint32_t        status;
  uint8_t        *dest;
  uint32_t        dest_cap;
  uint32_t        body_len;
} http_coro_t;

static struct {
  int32_t  valid;
  uint32_t status;
  uint32_t body_len;
} mHttpLast;

static int32_t HttpAsciiEq(char a, char b)
{
  if (a >= 'A' && a <= 'Z') {
    a = (char)(a + ('a' - 'A'));
  }
  if (b >= 'A' && b <= 'Z') {
    b = (char)(b + ('a' - 'A'));
  }
  return a == b;
}

static int32_t HttpStartsWith(const char *line, uint32_t len, const char *word)
{
  uint32_t i;
  uint32_t word_len;

  word_len = (uint32_t)strlen(word);
  if (len < word_len) {
    return 0;
  }
  for (i = 0; i < word_len; i++) {
    if (!HttpAsciiEq(line[i], word[i])) {
      return 0;
    }
  }
  return 1;
}

static int32_t HttpContains(const char *line, uint32_t len, const char *word)
{
  uint32_t i;
  uint32_t j;
  uint32_t word_len;

  word_len = (uint32_t)strlen(word);
  if (word_len == 0 || word_len > len) {
    return 0;
  }
  for (i = 0; i + word_len <= len; i++) {
    for (j = 0; j < word_len && HttpAsciiEq(line[i + j], word[j]); j++) {
    }
    if (j == word_len) {
      return 1;
    }
  }
  return 0;
}

static int32_t HttpParseUrl(http_coro_t *http)
{
  const char *p;
  const char *start;
  uint32_t    len;
  uint32_t    port;

  p = http->url;
  if (strncmp(p, "http://", 7) != 0) {
    return -1;
  }
  p += 7;
  start = p;
  while (*p != '\0' && *p != ':' && *p != '/') {
    if (*p == '[' || *p == ']') {
      return -1;
    }
    p++;
  }
  len = (uint32_t)(p - start);
  if (len == 0 || len >= sizeof(http->host)) {
    return -1;
  }
  memcpy(http->host, start, len);
  http->host[len] = '\0';
  http->port      = 80;

  if (*p == ':') {
    p++;
    port = 0;
    if (*p < '0' || *p > '9') {
      return -1;
    }
    while (*p >= '0' && *p <= '9') {
      port = port * 10u + (uint32_t)(*p - '0');
      if (port > 65535u) {
        return -1;
      }
      p++;
    }
    if (port == 0 || (*p != '\0' && *p != '/')) {
      return -1;
    }
    http->port = (uint16_t)port;
  }

  if (*p == '\0') {
    snprintf(http->path, sizeof(http->path), "%s", "/");
  } else {
    if (strlen(p) >= sizeof(http->path)) {
      return -1;
    }
    snprintf(http->path, sizeof(http->path), "%s", p);
  }
  return 0;
}

static int32_t HttpParseHeaders(http_coro_t *http)
{
  uint32_t i;
  uint32_t line_start;
  uint32_t line_len;
  uint32_t value;

  if (http->header_len < 12 || strncmp(http->header, "HTTP/1.", 7) != 0) {
    return -1;
  }
  i = 7;
  while (i < http->header_len && http->header[i] != ' ') {
    i++;
  }
  if (i + 3 >= http->header_len || http->header[i + 1] < '0' || http->header[i + 1] > '9' ||
      http->header[i + 2] < '0' || http->header[i + 2] > '9' || http->header[i + 3] < '0' ||
      http->header[i + 3] > '9') {
    return -1;
  }
  http->status = (uint32_t)(http->header[i + 1] - '0') * 100u +
                 (uint32_t)(http->header[i + 2] - '0') * 10u +
                 (uint32_t)(http->header[i + 3] - '0');

  line_start = 0;
  while (line_start + 1 < http->header_len) {
    for (i = line_start; i + 1 < http->header_len; i++) {
      if (http->header[i] == '\r' && http->header[i + 1] == '\n') {
        break;
      }
    }
    if (i + 1 >= http->header_len) {
      break;
    }
    line_len = i - line_start;
    if (line_len == 0) {
      break;
    }
    if (HttpStartsWith(http->header + line_start, line_len, "Content-Length:")) {
      uint32_t at;

      at = (uint32_t)strlen("Content-Length:");
      while (at < line_len &&
             (http->header[line_start + at] == ' ' || http->header[line_start + at] == '\t')) {
        at++;
      }
      value = 0;
      if (at >= line_len) {
        return -1;
      }
      while (at < line_len && http->header[line_start + at] >= '0' &&
             http->header[line_start + at] <= '9') {
        uint32_t digit;

        digit = (uint32_t)(http->header[line_start + at] - '0');
        if (value > (0xffffffffu - digit) / 10u) {
          return -1;
        }
        value = value * 10u + digit;
        at++;
      }
      http->content_len      = value;
      http->have_content_len = 1;
    }
    if (HttpStartsWith(http->header + line_start, line_len, "Transfer-Encoding:") &&
        HttpContains(http->header + line_start, line_len, "chunked")) {
      return -1;
    }
    line_start = i + 2;
  }
  if (http->have_content_len && http->content_len == 0) {
    http->complete = 1;
  }
  return 0;
}

static void HttpBodyByte(http_coro_t *http, uint8_t byte)
{
  uint32_t wanted;

  wanted = http->have_content_len ? http->content_len : http->dest_cap;
  if (wanted > http->dest_cap) {
    wanted = http->dest_cap;
  }
  if (http->body_len < wanted) {
    http->dest[http->body_len++] = byte;
  }
  if (http->body_len >= wanted) {
    http->complete = 1;
  }
}

static void HttpFeed(http_coro_t *http, const uint8_t *data, uint32_t len)
{
  uint32_t i;

  for (i = 0; i < len && !http->transport_error && !http->complete; i++) {
    if (http->headers_done) {
      HttpBodyByte(http, data[i]);
      continue;
    }
    if (http->header_len + 1 >= sizeof(http->header)) {
      http->transport_error = 1;
      return;
    }
    http->header[http->header_len++] = (char)data[i];
    http->header[http->header_len]   = '\0';
    if (http->header_len >= 4 && memcmp(http->header + http->header_len - 4, "\r\n\r\n", 4) == 0) {
      http->headers_done = 1;
      if (HttpParseHeaders(http) != 0) {
        http->transport_error = 1;
      }
    }
  }
}

static err_t HttpRecv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
  http_coro_t *http;
  struct pbuf *part;

  http = (http_coro_t *)arg;
  if (http == NULL) {
    if (p != NULL) {
      pbuf_free(p);
    }
    return ERR_OK;
  }
  if (p == NULL) {
    http->remote_closed = 1;
    return ERR_OK;
  }
  if (err != ERR_OK) {
    http->transport_error = 1;
    pbuf_free(p);
    return ERR_OK;
  }

  for (part = p; part != NULL; part = part->next) {
    HttpFeed(http, (const uint8_t *)part->payload, part->len);
  }
  tcp_recved(pcb, p->tot_len);
  pbuf_free(p);
  http->deadline = pm_metal_time_mono_us() + HTTP_IDLE_US;
  return ERR_OK;
}

static void HttpTcpError(void *arg, err_t err)
{
  http_coro_t *http;

  (void)err;
  http = (http_coro_t *)arg;
  if (http != NULL) {
    http->pcb             = NULL;
    http->transport_error = 1;
  }
}

static err_t HttpConnected(void *arg, struct tcp_pcb *pcb, err_t err)
{
  http_coro_t *http;

  (void)pcb;
  http = (http_coro_t *)arg;
  if (http != NULL) {
    if (err == ERR_OK) {
      http->connected = 1;
    } else {
      http->transport_error = 1;
    }
  }
  return ERR_OK;
}

static void HttpCleanup(http_coro_t *http)
{
  if (http->pcb == NULL) {
    return;
  }
  tcp_arg(http->pcb, NULL);
  tcp_recv(http->pcb, NULL);
  tcp_sent(http->pcb, NULL);
  tcp_poll(http->pcb, NULL, 0);
  tcp_err(http->pcb, NULL);
  if (tcp_close(http->pcb) != ERR_OK) {
    tcp_abort(http->pcb);
  }
  http->pcb = NULL;
}

static uint32_t HttpFinish(http_coro_t *http, uint32_t self_h, int32_t ok)
{
  HttpCleanup(http);
  mHttpLast.valid    = 1;
  mHttpLast.status   = ok ? http->status : 0;
  mHttpLast.body_len = http->body_len;
  pm_metal_async_set_result_u32(self_h, ok ? http->body_len : 0);
  return ok ? PM_METAL_ASYNC_DONE : PM_METAL_ASYNC_ERROR;
}

static int32_t HttpFinishChild(uint32_t self_h, http_coro_t *http, uint32_t *result)
{
  pm_metal_async_status_t status;

  status = pm_metal_async_await(self_h, http->child_h);
  if (status == PM_METAL_ASYNC_WAITING) {
    return 0;
  }
  if (result != NULL) {
    *result = pm_metal_async_result_u32(http->child_h);
  }
  pm_metal_async_coro_close(http->child_h);
  http->child_h = 0;
  return status == PM_METAL_ASYNC_DONE ? 1 : -1;
}

static uint32_t HttpStartSleep(uint32_t self_h, http_coro_t *http)
{
  http->child_h = pm_metal_async_sleep_us(2000);
  if (http->child_h == 0) {
    return HttpFinish(http, self_h, 0);
  }
  return (uint32_t)pm_metal_async_await(self_h, http->child_h);
}

static uint32_t HttpStep(uint32_t self_h)
{
  http_coro_t *http;
  int32_t      child;

  http = (http_coro_t *)pm_metal_async_coro_state(self_h);
  if (http == NULL) {
    return PM_METAL_ASYNC_ERROR;
  }

  switch (http->phase) {
  case HTTP_PARSE: {
    ip4_addr_t literal;

    if (HttpParseUrl(http) != 0) {
      return HttpFinish(http, self_h, 0);
    }
    if (ip4addr_aton(http->host, &literal)) {
      ip_addr_copy_from_ip4(http->server, literal);
      http->phase = HTTP_CONNECT_START;
      return PM_METAL_ASYNC_PENDING;
    }
    http->child_h = pm_metal_net_ip_dns(http->host);
    if (http->child_h == 0) {
      return HttpFinish(http, self_h, 0);
    }
    http->phase = HTTP_DNS_WAIT;
    return (uint32_t)pm_metal_async_await(self_h, http->child_h);
  }

  case HTTP_DNS_WAIT: {
    uint32_t   ok;
    char       text[16];
    ip4_addr_t addr;

    child = HttpFinishChild(self_h, http, &ok);
    if (child == 0) {
      return PM_METAL_ASYNC_WAITING;
    }
    if (child < 0 || ok == 0 || pm_metal_net_ip_dns_last_ntoa(text, sizeof(text)) != 0 ||
        !ip4addr_aton(text, &addr)) {
      return HttpFinish(http, self_h, 0);
    }
    ip_addr_copy_from_ip4(http->server, addr);
    http->phase = HTTP_CONNECT_START;
    return PM_METAL_ASYNC_PENDING;
  }

  case HTTP_CONNECT_START:
    http->pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (http->pcb == NULL) {
      return HttpFinish(http, self_h, 0);
    }
    tcp_arg(http->pcb, http);
    tcp_err(http->pcb, HttpTcpError);
    tcp_recv(http->pcb, HttpRecv);
    if (tcp_connect(http->pcb, &http->server, http->port, HttpConnected) != ERR_OK) {
      return HttpFinish(http, self_h, 0);
    }
    http->deadline = pm_metal_time_mono_us() + HTTP_CONNECT_US;
    http->phase    = HTTP_CONNECT_WAIT;
    return PM_METAL_ASYNC_PENDING;

  case HTTP_CONNECT_WAIT:
    if (http->child_h != 0) {
      child = HttpFinishChild(self_h, http, NULL);
      if (child == 0) {
        return PM_METAL_ASYNC_WAITING;
      }
      if (child < 0) {
        return HttpFinish(http, self_h, 0);
      }
    }
    pm_metal_net_ip_poll();
    if (http->transport_error) {
      return HttpFinish(http, self_h, 0);
    }
    if (http->connected) {
      http->phase = HTTP_SEND;
      return PM_METAL_ASYNC_PENDING;
    }
    if (pm_metal_time_mono_us() >= http->deadline) {
      return HttpFinish(http, self_h, 0);
    }
    return HttpStartSleep(self_h, http);

  case HTTP_SEND: {
    err_t err;

    if (http->child_h != 0) {
      child = HttpFinishChild(self_h, http, NULL);
      if (child == 0) {
        return PM_METAL_ASYNC_WAITING;
      }
      if (child < 0) {
        return HttpFinish(http, self_h, 0);
      }
    }
    if (http->request_len == 0) {
      int32_t request_len;

      if (http->port == 80) {
        request_len = snprintf(http->request,
                               sizeof(http->request),
                               "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                               http->path,
                               http->host);
      } else {
        request_len = snprintf(http->request,
                               sizeof(http->request),
                               "GET %s HTTP/1.1\r\nHost: %s:%u\r\nConnection: close\r\n\r\n",
                               http->path,
                               http->host,
                               (uint32_t)http->port);
      }
      if (request_len <= 0 || (uint32_t)request_len >= sizeof(http->request)) {
        return HttpFinish(http, self_h, 0);
      }
      http->request_len = (uint32_t)request_len;
    }
    err = tcp_write(http->pcb, http->request, (uint16_t)http->request_len, TCP_WRITE_FLAG_COPY);
    if (err == ERR_MEM) {
      return HttpStartSleep(self_h, http);
    }
    if (err != ERR_OK || tcp_output(http->pcb) != ERR_OK) {
      return HttpFinish(http, self_h, 0);
    }
    http->deadline = pm_metal_time_mono_us() + HTTP_IDLE_US;
    http->phase    = HTTP_RECV;
    return PM_METAL_ASYNC_PENDING;
  }

  case HTTP_RECV:
    if (http->child_h != 0) {
      child = HttpFinishChild(self_h, http, NULL);
      if (child == 0) {
        return PM_METAL_ASYNC_WAITING;
      }
      if (child < 0) {
        return HttpFinish(http, self_h, 0);
      }
    }
    pm_metal_net_ip_poll();
    if (http->transport_error) {
      return HttpFinish(http, self_h, 0);
    }
    if (http->complete) {
      return HttpFinish(http, self_h, 1);
    }
    if (http->remote_closed) {
      if (!http->headers_done) {
        return HttpFinish(http, self_h, 0);
      }
      if (http->have_content_len && http->body_len < http->content_len &&
          http->body_len < http->dest_cap) {
        return HttpFinish(http, self_h, 0);
      }
      return HttpFinish(http, self_h, 1);
    }
    if (pm_metal_time_mono_us() >= http->deadline) {
      return HttpFinish(http, self_h, 0);
    }
    return HttpStartSleep(self_h, http);
  }

  return HttpFinish(http, self_h, 0);
}

uint32_t pm_metal_net_http_get(const char *url, void *dest, uint32_t dest_cap)
{
  http_coro_t *http;
  uint32_t     h;

  if (url == NULL || dest == NULL || dest_cap == 0 || strlen(url) >= HTTP_URL_MAX) {
    return 0;
  }
  h = pm_metal_async_coro_create(HttpStep, sizeof(*http));
  if (h == 0) {
    return 0;
  }
  http = (http_coro_t *)pm_metal_async_coro_state(h);
  if (http == NULL) {
    pm_metal_async_coro_close(h);
    return 0;
  }
  http->phase    = HTTP_PARSE;
  http->dest     = (uint8_t *)dest;
  http->dest_cap = dest_cap;
  snprintf(http->url, sizeof(http->url), "%s", url);
  mHttpLast.valid = 0;
  return h;
}

uint32_t pm_metal_net_http_status(uint32_t h)
{
  http_coro_t *http;

  http = (http_coro_t *)pm_metal_async_coro_state(h);
  if (http != NULL) {
    return http->status;
  }
  return mHttpLast.valid ? mHttpLast.status : 0;
}

uint32_t pm_metal_net_http_body_len(uint32_t h)
{
  http_coro_t *http;

  http = (http_coro_t *)pm_metal_async_coro_state(h);
  if (http != NULL) {
    return http->body_len;
  }
  return mHttpLast.valid ? mHttpLast.body_len : 0;
}
