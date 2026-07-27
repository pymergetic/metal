/*
 * Shared HTTP/1.1 framing helpers (client GET + ASGI server).
 * Host-only; no dual ABI.
 *
 * impl: common — src/pymergetic/metal/net/http/http_parse.c
 */
#ifndef PYMERGETIC_METAL_NET_HTTP_PARSE_H_
#define PYMERGETIC_METAL_NET_HTTP_PARSE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Index past CRLFCRLF, or -1 if incomplete. */
int32_t pm_metal_http_find_hdr_end(const char *buf, uint32_t len);

/** Parse response status code from header block; 0 if not HTTP/. */
uint32_t pm_metal_http_parse_status(const char *hdr, uint32_t hdr_len);

typedef struct {
  uint32_t content_len;
  int32_t  chunked;
  int32_t  body_until_close;
} pm_metal_net_http_body_mode_t;

/** Scan headers for Content-Length / Transfer-Encoding: chunked. */
void pm_metal_net_http_scan_body_mode(const char *hdr, uint32_t hdr_len, pm_metal_net_http_body_mode_t *out);

typedef enum {
  PM_METAL_HTTP_CHUNK_SIZE = 0,
  PM_METAL_HTTP_CHUNK_DATA,
  PM_METAL_HTTP_CHUNK_AFTER_DATA,
  PM_METAL_HTTP_CHUNK_DONE
} pm_metal_http_chunk_step_t;

typedef struct {
  pm_metal_http_chunk_step_t step;
  uint32_t                   rem;
  int32_t                    zero;
  char                       line[16];
  uint32_t                   line_len;
  int32_t                    done;
} pm_metal_http_chunk_dec_t;

void    pm_metal_http_chunk_dec_init(pm_metal_http_chunk_dec_t *d);
int32_t pm_metal_http_chunk_dec_feed(pm_metal_http_chunk_dec_t *d, const uint8_t *data, uint32_t len,
                                     uint8_t *body, uint32_t body_cap, uint32_t *body_len);

/** METHOD SP target SP HTTP/x.y — returns 0 ok, -1 fail. */
int32_t pm_metal_http_parse_request_line(const char *hdr, uint32_t hdr_len, char *method,
                                         uint32_t method_cap, char *target, uint32_t target_cap,
                                         uint32_t *ver_minor);

/** Case-insensitive header value into out (trimmed); 0 ok, -1 missing. */
int32_t pm_metal_http_hdr_get(const char *hdr, uint32_t hdr_len, const char *name, char *out,
                              uint32_t out_cap);

/** Format "HTTP/1.1 %u %s\r\n" into dest; returns bytes written or -1. */
int32_t pm_metal_http_fmt_status(char *dest, uint32_t dest_cap, uint32_t code, const char *reason);

/** Append "Name: value\r\n"; returns new len or -1. */
int32_t pm_metal_http_hdr_append(char *dest, uint32_t dest_cap, uint32_t dest_len, const char *name,
                                 const char *value);

/** Append final \r\n after headers; returns new len or -1. */
int32_t pm_metal_http_hdr_end(char *dest, uint32_t dest_cap, uint32_t dest_len);

/** Write one chunk (size line + data + CRLF) into dest; 0-chunk if data_len==0. */
int32_t pm_metal_http_chunk_encode(char *dest, uint32_t dest_cap, const uint8_t *data,
                                   uint32_t data_len);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_HTTP_PARSE_H_ */
