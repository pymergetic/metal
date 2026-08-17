/* pymergetic.metal.net.http — GET/HEAD/POST on net.ip TCP; park via run_until.
 * Strong pm_metal_wasm_io_* (overrides ports/metal weak DECLINE). https via net.tls. */
#include "pymergetic/metal/net/http/__exports__.h"

#include "pymergetic/metal/async.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/net/tls.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/io.h"

#include <stdio.h>
#include <string.h>

const char *pm_wasmmod_net_cdn_session_id(void) __attribute__((weak));

typedef struct {
    pm_metal_async_coro_t coro;
    uint32_t step;
    int32_t fd;
    uint32_t addr;
    uint16_t port;
    char host[80];
    char path[160];
    char method[8];
    const uint8_t *req_body;
    uint32_t req_body_len;
    const char *ctype;
    uint8_t *acc;
    uint32_t acc_len;
    uint32_t acc_cap;
    uint8_t *out;
    uint32_t out_len;
    int32_t err;
    uint32_t use_tls;
    uint32_t snd_off;
    pm_metal_net_tls_session_t *tls;
} pm_metal_http_fetch_t;

static pm_util_mem_arena_t *s_arena;

static void err_set(char *errbuf, size_t errbuf_len, const char *msg) {
    if (errbuf == NULL || errbuf_len == 0) {
        return;
    }
    snprintf(errbuf, errbuf_len, "%s", msg);
}

static int32_t parse_ipv4(const char *s, uint32_t *out) {
    unsigned a = 0;
    unsigned b = 0;
    unsigned c = 0;
    unsigned d = 0;
    if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return -1;
    }
    if (a > 255u || b > 255u || c > 255u || d > 255u) {
        return -1;
    }
    *out = (a << 24) | (b << 16) | (c << 8) | d;
    return 0;
}

static int32_t http_send(pm_metal_http_fetch_t *f, const uint8_t *buf, uint32_t len) {
    if (f->tls != NULL) {
        return pm_metal_net_tls_send(f->tls, buf, len);
    }
    return pm_metal_net_ip_send(f->fd, buf, len);
}

static int32_t http_recv(pm_metal_http_fetch_t *f, uint8_t *buf, uint32_t len) {
    if (f->tls != NULL) {
        return pm_metal_net_tls_recv(f->tls, buf, len);
    }
    return pm_metal_net_ip_recv(f->fd, buf, len);
}

static void http_close_fd(pm_metal_http_fetch_t *f) {
    if (f->tls != NULL) {
        pm_metal_net_tls_close(f->tls);
        f->tls = NULL;
    }
    if (f->fd >= 0) {
        (void)pm_metal_net_ip_close(f->fd);
        f->fd = -1;
    }
}

static int32_t parse_http_uri(const char *uri, char *host, uint32_t host_cap, uint16_t *port,
    char *path, uint32_t path_cap, uint32_t *use_tls) {
    const char *p;
    if (uri == NULL) {
        return -1;
    }
    if (strncmp(uri, "https://", 8) == 0) {
        p = uri + 8;
        *port = 443;
        *use_tls = 1;
    } else if (strncmp(uri, "http://", 7) == 0) {
        p = uri + 7;
        *port = 80;
        *use_tls = 0;
    } else {
        return -1;
    }
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');
    uint32_t hlen;
    if (colon != NULL && (slash == NULL || colon < slash)) {
        hlen = (uint32_t)(colon - p);
        unsigned pr = 0;
        if (sscanf(colon + 1, "%u", &pr) != 1 || pr == 0 || pr > 65535u) {
            return -1;
        }
        *port = (uint16_t)pr;
    } else {
        hlen = slash != NULL ? (uint32_t)(slash - p) : (uint32_t)strlen(p);
    }
    if (hlen == 0 || hlen >= host_cap) {
        return -1;
    }
    memcpy(host, p, hlen);
    host[hlen] = 0;
    if (slash != NULL) {
        if (strlen(slash) >= path_cap) {
            return -1;
        }
        memcpy(path, slash, strlen(slash) + 1u);
    } else {
        if (path_cap < 2) {
            return -1;
        }
        path[0] = '/';
        path[1] = 0;
    }
    return 0;
}

static int32_t acc_put(pm_metal_http_fetch_t *f, const uint8_t *p, uint32_t n) {
    if (n == 0) {
        return 0;
    }
    if (f->acc_len + n > f->acc_cap) {
        uint32_t cap = f->acc_cap == 0 ? 256u : f->acc_cap;
        while (cap < f->acc_len + n) {
            cap *= 2u;
        }
        uint8_t *np = (uint8_t *)pm_util_mem_realloc(s_arena, f->acc, cap);
        if (np == NULL) {
            return -1;
        }
        f->acc = np;
        f->acc_cap = cap;
    }
    memcpy(f->acc + f->acc_len, p, n);
    f->acc_len += n;
    return 0;
}

static const uint8_t *find_hdr_end(const uint8_t *p, uint32_t n, uint32_t *end_off) {
    uint32_t i;
    for (i = 0; i + 3u < n; i++) {
        if (p[i] == '\r' && p[i + 1] == '\n' && p[i + 2] == '\r' && p[i + 3] == '\n') {
            *end_off = i + 4u;
            return p + i + 4u;
        }
    }
    return NULL;
}

static int32_t parse_clen(const uint8_t *hdr, uint32_t hdr_len, uint32_t *clen) {
    char tmp[256];
    uint32_t n = hdr_len < sizeof(tmp) - 1u ? hdr_len : (uint32_t)sizeof(tmp) - 1u;
    memcpy(tmp, hdr, n);
    tmp[n] = 0;
    const char *k = strstr(tmp, "Content-Length:");
    if (k == NULL) {
        k = strstr(tmp, "content-length:");
    }
    if (k == NULL) {
        return -1;
    }
    unsigned v = 0;
    if (sscanf(k, "%*[^:]: %u", &v) != 1) {
        return -1;
    }
    *clen = v;
    return 0;
}

static int32_t finish_body(pm_metal_http_fetch_t *f) {
    uint32_t hdr_end = 0;
    if (find_hdr_end(f->acc, f->acc_len, &hdr_end) == NULL) {
        return -1;
    }
    if (f->acc_len < 12u || memcmp(f->acc, "HTTP/", 5) != 0) {
        return -1;
    }
    if (f->acc[9] != '2') {
        return -1;
    }
    uint32_t clen = 0;
    uint32_t have = f->acc_len - hdr_end;
    if (parse_clen(f->acc, hdr_end, &clen) == 0) {
        if (have < clen) {
            return 1;
        }
        have = clen;
    }
    uint8_t *out = (uint8_t *)pm_util_mem_alloc(s_arena, have ? have : 1u);
    if (out == NULL) {
        return -1;
    }
    if (have != 0) {
        memcpy(out, f->acc + hdr_end, have);
    }
    f->out = out;
    f->out_len = have;
    return 0;
}

static pm_metal_async_status_t step_fetch(pm_metal_async_coro_t *self) {
    pm_metal_http_fetch_t *f = (pm_metal_http_fetch_t *)self;
    if (f->step == 0) {
        f->fd = pm_metal_net_ip_socket(PM_METAL_NET_IP_SOCK_STREAM);
        if (f->fd < 0) {
            f->err = 1;
            return PM_METAL_ASYNC_ERROR;
        }
        int32_t st = pm_metal_net_ip_connect(f->fd, f->addr, f->port);
        if (st == 0) {
            f->step = 1;
            return PM_METAL_ASYNC_WAITING;
        }
        if (st < 0) {
            f->err = 1;
            return PM_METAL_ASYNC_ERROR;
        }
        f->step = 1;
    }
    if (f->step == 1 && f->use_tls) {
        if (f->tls == NULL) {
            f->tls = pm_metal_net_tls_client(f->fd, f->host);
            if (f->tls == NULL) {
                f->err = 1;
                return PM_METAL_ASYNC_ERROR;
            }
        }
        int32_t st = pm_metal_net_tls_handshake(f->tls);
        if (st == 1) {
            return PM_METAL_ASYNC_WAITING;
        }
        if (st < 0) {
            f->err = 1;
            return PM_METAL_ASYNC_ERROR;
        }
        f->step = 2;
    } else if (f->step == 1) {
        f->step = 2;
    }
    if (f->step == 2) {
        char req[1792];
        const char *tok = pm_wasmmod_io_auth_bearer();
        const char *sid = pm_wasmmod_net_cdn_session_id ? pm_wasmmod_net_cdn_session_id() : NULL;
        int n = snprintf(req, sizeof(req),
            "%s %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n", f->method, f->path, f->host);
        if (n < 0 || (uint32_t)n >= sizeof(req)) {
            f->err = 1;
            return PM_METAL_ASYNC_ERROR;
        }
        if (tok != NULL && tok[0] != '\0') {
            int k = snprintf(req + n, sizeof(req) - (uint32_t)n, "Authorization: Bearer %s\r\n", tok);
            if (k < 0 || (uint32_t)k >= sizeof(req) - (uint32_t)n) {
                f->err = 1;
                return PM_METAL_ASYNC_ERROR;
            }
            n += k;
        }
        if (sid != NULL && sid[0] != '\0') {
            int k = snprintf(req + n, sizeof(req) - (uint32_t)n, "X-Shell-Session-Id: %s\r\n", sid);
            if (k < 0 || (uint32_t)k >= sizeof(req) - (uint32_t)n) {
                f->err = 1;
                return PM_METAL_ASYNC_ERROR;
            }
            n += k;
        }
        if (f->req_body_len != 0) {
            int k = snprintf(req + n, sizeof(req) - (uint32_t)n, "Content-Length: %u\r\n",
                (unsigned)f->req_body_len);
            if (k < 0 || (uint32_t)k >= sizeof(req) - (uint32_t)n) {
                f->err = 1;
                return PM_METAL_ASYNC_ERROR;
            }
            n += k;
        }
        if (f->ctype != NULL && f->ctype[0] != 0) {
            int k = snprintf(req + n, sizeof(req) - (uint32_t)n, "Content-Type: %s\r\n", f->ctype);
            if (k < 0 || (uint32_t)k >= sizeof(req) - (uint32_t)n) {
                f->err = 1;
                return PM_METAL_ASYNC_ERROR;
            }
            n += k;
        }
        {
            int k = snprintf(req + n, sizeof(req) - (uint32_t)n, "\r\n");
            if (k < 0 || (uint32_t)k >= sizeof(req) - (uint32_t)n) {
                f->err = 1;
                return PM_METAL_ASYNC_ERROR;
            }
            n += k;
        }
        while (f->snd_off < (uint32_t)n) {
            int32_t k = http_send(f, (const uint8_t *)req + f->snd_off, (uint32_t)n - f->snd_off);
            if (k == 0) {
                return PM_METAL_ASYNC_WAITING;
            }
            if (k < 0) {
                f->err = 1;
                return PM_METAL_ASYNC_ERROR;
            }
            f->snd_off += (uint32_t)k;
        }
        uint32_t body_off = f->snd_off - (uint32_t)n;
        while (body_off < f->req_body_len) {
            int32_t k = http_send(f, f->req_body + body_off, f->req_body_len - body_off);
            if (k == 0) {
                return PM_METAL_ASYNC_WAITING;
            }
            if (k < 0) {
                f->err = 1;
                return PM_METAL_ASYNC_ERROR;
            }
            f->snd_off += (uint32_t)k;
            body_off += (uint32_t)k;
        }
        f->step = 3;
    }
    for (;;) {
        uint8_t buf[256];
        int32_t n = http_recv(f, buf, sizeof(buf));
        if (n == 0) {
            return PM_METAL_ASYNC_WAITING;
        }
        if (n == -2) {
            if (finish_body(f) != 0) {
                f->err = 1;
                return PM_METAL_ASYNC_ERROR;
            }
            http_close_fd(f);
            return PM_METAL_ASYNC_DONE;
        }
        if (n < 0) {
            f->err = 1;
            return PM_METAL_ASYNC_ERROR;
        }
        if (acc_put(f, buf, (uint32_t)n) != 0) {
            f->err = 1;
            return PM_METAL_ASYNC_ERROR;
        }
        uint32_t hdr_end = 0;
        if (find_hdr_end(f->acc, f->acc_len, &hdr_end) != NULL) {
            uint32_t clen = 0;
            if (parse_clen(f->acc, hdr_end, &clen) == 0 && f->acc_len - hdr_end >= clen) {
                if (finish_body(f) != 0) {
                    f->err = 1;
                    return PM_METAL_ASYNC_ERROR;
                }
                http_close_fd(f);
                return PM_METAL_ASYNC_DONE;
            }
        }
    }
}

static pm_wasmmod_io_result_t http_do(const char *method, const char *uri, const uint8_t *body,
    uint32_t body_len, const char *ctype, uint8_t **out_bytes, uint32_t *out_len, char *errbuf,
    size_t errbuf_len) {
    if (s_arena == NULL || uri == NULL) {
        err_set(errbuf, errbuf_len, "http not inited");
        return PM_WASMMOD_IO_ERR;
    }
    if (strncmp(uri, "https://", 8) == 0) {
        if (!pm_metal_net_tls_ready()) {
            return PM_WASMMOD_IO_DECLINE;
        }
    } else if (strncmp(uri, "http://", 7) != 0) {
        return PM_WASMMOD_IO_DECLINE;
    }
    pm_metal_http_fetch_t *f =
        (pm_metal_http_fetch_t *)pm_metal_async_coro_create(step_fetch, sizeof(*f));
    if (f == NULL) {
        err_set(errbuf, errbuf_len, "coro");
        return PM_WASMMOD_IO_ERR;
    }
    if (parse_http_uri(uri, f->host, sizeof(f->host), &f->port, f->path, sizeof(f->path),
            &f->use_tls)
        != 0) {
        err_set(errbuf, errbuf_len, "uri");
        return PM_WASMMOD_IO_ERR;
    }
    if (parse_ipv4(f->host, &f->addr) != 0) {
        err_set(errbuf, errbuf_len, "host not ipv4");
        return PM_WASMMOD_IO_ERR;
    }
    snprintf(f->method, sizeof(f->method), "%s", method != NULL ? method : "GET");
    f->req_body = body;
    f->req_body_len = body_len;
    f->ctype = ctype;
    f->fd = -1;
    if (pm_metal_async_create_task(&f->coro) == NULL) {
        err_set(errbuf, errbuf_len, "task");
        return PM_WASMMOD_IO_ERR;
    }
    if (pm_metal_async_run_until(&f->coro) != 0 || f->out == NULL) {
        err_set(errbuf, errbuf_len, "fetch failed");
        return PM_WASMMOD_IO_ERR;
    }
    if (out_bytes != NULL) {
        *out_bytes = f->out;
    }
    if (out_len != NULL) {
        *out_len = f->out_len;
    }
    return PM_WASMMOD_IO_OK;
}

int32_t pm_metal_net_http_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    return 0;
}

void pm_metal_net_http_deinit(void) {
    s_arena = NULL;
}

pm_wasmmod_io_result_t pm_metal_net_http_fetch(const char *uri, uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len) {
    return http_do("GET", uri, NULL, 0, NULL, out_bytes, out_len, errbuf, errbuf_len);
}

pm_wasmmod_io_result_t pm_metal_net_http_probe(const char *uri) {
    uint8_t *b = NULL;
    uint32_t n = 0;
    char err[32];
    pm_wasmmod_io_result_t st = http_do("HEAD", uri, NULL, 0, NULL, &b, &n, err, sizeof(err));
    if (b != NULL) {
        pm_util_mem_free(s_arena, b);
    }
    return st;
}

pm_wasmmod_io_result_t pm_metal_net_http_request(const char *method, const char *uri,
    const uint8_t *body, uint32_t body_len, const char *content_type, uint8_t **out_bytes,
    uint32_t *out_len, char *errbuf, size_t errbuf_len) {
    return http_do(method, uri, body, body_len, content_type, out_bytes, out_len, errbuf, errbuf_len);
}

/* Strong fills for wasmmod's freestanding io hooks (ports/freestanding/io_ops.h).
 * Link-level border, not card exports: wasmmod asks for pm_wasmmod_host_io_*
 * and never names a card of ours. */
pm_wasmmod_io_result_t pm_wasmmod_host_io_fetch(const char *uri, uint8_t **out_bytes,
    uint32_t *out_len, char *errbuf, size_t errbuf_len) {
    return pm_metal_net_http_fetch(uri, out_bytes, out_len, errbuf, errbuf_len);
}

pm_wasmmod_io_result_t pm_wasmmod_host_io_probe(const char *uri) {
    return pm_metal_net_http_probe(uri);
}

pm_wasmmod_io_result_t pm_wasmmod_host_io_request(const char *method, const char *uri,
    const uint8_t *body, uint32_t body_len, const char *content_type, uint8_t **out_bytes,
    uint32_t *out_len, char *errbuf, size_t errbuf_len) {
    return pm_metal_net_http_request(method, uri, body, body_len, content_type, out_bytes,
        out_len, errbuf, errbuf_len);
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.net.http, pm_metal_net_http_init, pm_metal_net_http_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.http, pm_metal_net_http_deinit, pm_metal_net_http_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.http, pm_metal_net_http_fetch, pm_metal_net_http_fetch, pm_wasmmod_io_result_t(const char *, uint8_t **, uint32_t *, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.http, pm_metal_net_http_probe, pm_metal_net_http_probe, pm_wasmmod_io_result_t(const char *));
PM_MOD_EXPORT_C(pymergetic.metal.net.http, pm_metal_net_http_request, pm_metal_net_http_request, pm_wasmmod_io_result_t(const char *, const char *, const uint8_t *, uint32_t, const char *, uint8_t **, uint32_t *, char *, size_t));

PM_MOD_BOOT_C(pymergetic.metal.net.http, pm_metal_net_http_init, pm_metal_net_http_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.net.http, pymergetic.metal.net.tls);
