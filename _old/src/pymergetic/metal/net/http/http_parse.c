/*
 * Shared HTTP/1.1 framing — client + ASGI server.
 */
#include <pymergetic/metal/net/http/http_parse.h>

#include <stdio.h>
#include <string.h>

static int32_t hex_val(char c)
{
  if (c >= '0' && c <= '9') {
    return (int32_t)(c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return 10 + (int32_t)(c - 'a');
  }
  if (c >= 'A' && c <= 'F') {
    return 10 + (int32_t)(c - 'A');
  }
  return -1;
}

static int32_t ascii_ieq(char a, char b)
{
  char aa;
  char bb;

  aa = a;
  bb = b;
  if (aa >= 'A' && aa <= 'Z') {
    aa = (char)(aa - 'A' + 'a');
  }
  if (bb >= 'A' && bb <= 'Z') {
    bb = (char)(bb - 'A' + 'a');
  }
  return aa == bb;
}

static int32_t name_ieq(const char *a, const char *b, uint32_t n)
{
  uint32_t i;

  for (i = 0; i < n; i++) {
    if (!ascii_ieq(a[i], b[i])) {
      return 0;
    }
  }
  return 1;
}

int32_t pm_metal_http_find_hdr_end(const char *buf, uint32_t len)
{
  uint32_t i;

  if (buf == NULL) {
    return -1;
  }
  for (i = 0; i + 3 < len; i++) {
    if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
      return (int32_t)(i + 4);
    }
  }
  return -1;
}

uint32_t pm_metal_http_parse_status(const char *hdr, uint32_t hdr_len)
{
  const char *p;
  uint32_t    st;

  if (hdr == NULL || hdr_len < 12) {
    return 0;
  }
  if (strncmp(hdr, "HTTP/", 5) != 0) {
    return 0;
  }
  p = hdr + 5;
  while (*p != '\0' && *p != ' ' && (uint32_t)(p - hdr) < hdr_len) {
    p++;
  }
  while (*p == ' ') {
    p++;
  }
  st = 0;
  while (*p >= '0' && *p <= '9') {
    st = st * 10u + (uint32_t)(*p - '0');
    p++;
  }
  return st;
}

void pm_metal_net_http_scan_body_mode(const char                    *hdr,
                                      uint32_t                       hdr_len,
                                      pm_metal_net_http_body_mode_t *out)
{
  const char *line;
  uint32_t    i;

  if (out == NULL) {
    return;
  }
  out->content_len      = 0;
  out->chunked          = 0;
  out->body_until_close = 0;
  if (hdr == NULL || hdr_len == 0) {
    out->body_until_close = 1;
    return;
  }

  line = hdr;
  for (i = 0; i < hdr_len;) {
    uint32_t j;

    j = i;
    while (j + 1 < hdr_len && !(hdr[j] == '\r' && hdr[j + 1] == '\n')) {
      j++;
    }
    if (j > i) {
      if (j - i >= 15 && name_ieq(line, "Content-Length:", 15)) {
        const char *v;

        v = line + 15;
        while (*v == ' ') {
          v++;
        }
        out->content_len = 0;
        while (*v >= '0' && *v <= '9') {
          out->content_len = out->content_len * 10u + (uint32_t)(*v - '0');
          v++;
        }
      } else if (j - i >= 18 && name_ieq(line, "Transfer-Encoding:", 18)) {
        const char *v;
        uint32_t    k;

        v = line + 18;
        while (*v == ' ') {
          v++;
        }
        for (k = 0; v[k] != '\0' && v[k] != '\r'; k++) {
          if ((v[k] | 0x20) == 'c' && strncmp(v + k, "chunked", 7) == 0) {
            out->chunked = 1;
            break;
          }
        }
      }
    }
    i    = j + 2;
    line = hdr + i;
  }
  if (out->content_len == 0 && !out->chunked) {
    out->body_until_close = 1;
  }
}

void pm_metal_http_chunk_dec_init(pm_metal_http_chunk_dec_t *d)
{
  if (d == NULL) {
    return;
  }
  memset(d, 0, sizeof(*d));
  d->step = PM_METAL_HTTP_CHUNK_SIZE;
}

static int32_t chunk_feed_byte(
  pm_metal_http_chunk_dec_t *d, uint8_t b, uint8_t *body, uint32_t body_cap, uint32_t *body_len)
{
  int32_t hv;

  switch (d->step) {
  case PM_METAL_HTTP_CHUNK_SIZE:
    if (b == '\n') {
      uint32_t sz;
      uint32_t i;

      sz = 0;
      for (i = 0; i < d->line_len; i++) {
        char c;

        c = d->line[i];
        if (c == ';') {
          break;
        }
        hv = hex_val(c);
        if (hv < 0) {
          return -1;
        }
        sz = (sz << 4) + (uint32_t)hv;
      }
      d->line_len = 0;
      d->zero     = (sz == 0) ? 1 : 0;
      if (sz == 0) {
        d->rem  = 2;
        d->step = PM_METAL_HTTP_CHUNK_AFTER_DATA;
      } else {
        d->rem  = sz;
        d->step = PM_METAL_HTTP_CHUNK_DATA;
      }
      return 0;
    }
    if (b == '\r') {
      return 0;
    }
    if (d->line_len + 1 >= sizeof(d->line)) {
      return -1;
    }
    d->line[d->line_len++] = (char)b;
    return 0;

  case PM_METAL_HTTP_CHUNK_DATA:
    if (body != NULL && body_len != NULL && *body_len < body_cap) {
      body[(*body_len)++] = b;
    }
    if (d->rem > 0) {
      d->rem--;
    }
    if (d->rem == 0) {
      d->rem  = 2;
      d->step = PM_METAL_HTTP_CHUNK_AFTER_DATA;
    }
    return 0;

  case PM_METAL_HTTP_CHUNK_AFTER_DATA:
    if (b != '\r' && b != '\n') {
      return -1;
    }
    if (d->rem > 0) {
      d->rem--;
    }
    if (d->rem == 0) {
      if (d->zero) {
        d->done = 1;
        d->step = PM_METAL_HTTP_CHUNK_DONE;
      } else {
        d->step = PM_METAL_HTTP_CHUNK_SIZE;
      }
    }
    return 0;

  default:
    return 0;
  }
}

int32_t pm_metal_http_chunk_dec_feed(pm_metal_http_chunk_dec_t *d,
                                     const uint8_t             *data,
                                     uint32_t                   len,
                                     uint8_t                   *body,
                                     uint32_t                   body_cap,
                                     uint32_t                  *body_len)
{
  uint32_t i;

  if (d == NULL || data == NULL) {
    return -1;
  }
  for (i = 0; i < len; i++) {
    if (chunk_feed_byte(d, data[i], body, body_cap, body_len) != 0) {
      return -1;
    }
    if (d->done) {
      break;
    }
  }
  return 0;
}

int32_t pm_metal_http_parse_request_line(const char *hdr,
                                         uint32_t    hdr_len,
                                         char       *method,
                                         uint32_t    method_cap,
                                         char       *target,
                                         uint32_t    target_cap,
                                         uint32_t   *ver_minor)
{
  uint32_t i;
  uint32_t m0;
  uint32_t t0;
  uint32_t te;
  uint32_t v;

  if (hdr == NULL || method == NULL || target == NULL || method_cap == 0 || target_cap == 0) {
    return -1;
  }
  i = 0;
  while (i < hdr_len && hdr[i] != ' ' && hdr[i] != '\r' && hdr[i] != '\n') {
    i++;
  }
  if (i == 0 || i >= method_cap || i >= hdr_len || hdr[i] != ' ') {
    return -1;
  }
  memcpy(method, hdr, i);
  method[i] = '\0';
  i++;
  while (i < hdr_len && hdr[i] == ' ') {
    i++;
  }
  t0 = i;
  while (i < hdr_len && hdr[i] != ' ' && hdr[i] != '\r' && hdr[i] != '\n') {
    i++;
  }
  te = i;
  if (te <= t0 || (te - t0) >= target_cap) {
    return -1;
  }
  memcpy(target, hdr + t0, te - t0);
  target[te - t0] = '\0';
  if (i >= hdr_len || hdr[i] != ' ') {
    return -1;
  }
  i++;
  while (i < hdr_len && hdr[i] == ' ') {
    i++;
  }
  if (i + 7 > hdr_len || strncmp(hdr + i, "HTTP/1.", 7) != 0) {
    return -1;
  }
  v = (uint32_t)(hdr[i + 7] - '0');
  if (hdr[i + 7] < '0' || hdr[i + 7] > '9') {
    return -1;
  }
  if (ver_minor != NULL) {
    *ver_minor = v;
  }
  (void)m0;
  return 0;
}

int32_t pm_metal_http_hdr_get(
  const char *hdr, uint32_t hdr_len, const char *name, char *out, uint32_t out_cap)
{
  uint32_t    nlen;
  uint32_t    i;
  const char *line;

  if (hdr == NULL || name == NULL || out == NULL || out_cap == 0) {
    return -1;
  }
  nlen = (uint32_t)strlen(name);
  line = hdr;
  for (i = 0; i < hdr_len;) {
    uint32_t j;
    uint32_t k;

    j = i;
    while (j + 1 < hdr_len && !(hdr[j] == '\r' && hdr[j + 1] == '\n')) {
      j++;
    }
    if (j > i + nlen + 1 && name_ieq(line, name, nlen) && line[nlen] == ':') {
      k = nlen + 1;
      while (i + k < j && (line[k] == ' ' || line[k] == '\t')) {
        k++;
      }
      {
        uint32_t vlen;

        vlen = j - (i + k);
        if (vlen + 1 > out_cap) {
          vlen = out_cap - 1;
        }
        memcpy(out, line + k, vlen);
        out[vlen] = '\0';
        return 0;
      }
    }
    i    = j + 2;
    line = hdr + i;
  }
  return -1;
}

int32_t pm_metal_http_fmt_status(char *dest, uint32_t dest_cap, uint32_t code, const char *reason)
{
  int32_t n;

  if (dest == NULL || dest_cap < 16 || reason == NULL) {
    return -1;
  }
  n = snprintf(dest, dest_cap, "HTTP/1.1 %u %s\r\n", code, reason);
  if (n < 0 || (uint32_t)n >= dest_cap) {
    return -1;
  }
  return n;
}

int32_t pm_metal_http_hdr_append(
  char *dest, uint32_t dest_cap, uint32_t dest_len, const char *name, const char *value)
{
  int32_t n;

  if (dest == NULL || name == NULL || value == NULL || dest_len >= dest_cap) {
    return -1;
  }
  n = snprintf(dest + dest_len, dest_cap - dest_len, "%s: %s\r\n", name, value);
  if (n < 0 || dest_len + (uint32_t)n >= dest_cap) {
    return -1;
  }
  return (int32_t)(dest_len + (uint32_t)n);
}

int32_t pm_metal_http_hdr_end(char *dest, uint32_t dest_cap, uint32_t dest_len)
{
  if (dest == NULL || dest_len + 2 > dest_cap) {
    return -1;
  }
  dest[dest_len++] = '\r';
  dest[dest_len++] = '\n';
  if (dest_len < dest_cap) {
    dest[dest_len] = '\0';
  }
  return (int32_t)dest_len;
}

int32_t pm_metal_http_chunk_encode(char          *dest,
                                   uint32_t       dest_cap,
                                   const uint8_t *data,
                                   uint32_t       data_len)
{
  int32_t n;

  if (dest == NULL || dest_cap < 8) {
    return -1;
  }
  if (data_len == 0) {
    n = snprintf(dest, dest_cap, "0\r\n\r\n");
    return (n < 0 || (uint32_t)n >= dest_cap) ? -1 : n;
  }
  if (data == NULL) {
    return -1;
  }
  n = snprintf(dest, dest_cap, "%x\r\n", data_len);
  if (n < 0 || (uint32_t)n + data_len + 2 >= dest_cap) {
    return -1;
  }
  memcpy(dest + n, data, data_len);
  dest[n + (int32_t)data_len]     = '\r';
  dest[n + (int32_t)data_len + 1] = '\n';
  return n + (int32_t)data_len + 2;
}
