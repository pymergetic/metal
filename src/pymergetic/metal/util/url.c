#include "pymergetic/kernel/util/url.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int url_bounded_len(const char *s, size_t max_len, size_t *out_len)
{
    if (s == NULL || out_len == NULL) {
        return -1;
    }

    size_t n = 0;
    while (s[n] != '\0') {
        if ((unsigned char)s[n] < 0x20u || (unsigned char)s[n] == 0x7fu) {
            return -1;
        }
        n++;
        if (n > max_len) {
            return -1;
        }
    }

    *out_len = n;
    return 0;
}

int pm_metal_util_url_len(const char *s, size_t *out_len)
{
    return url_bounded_len(s, PM_METAL_UTIL_URL_MAX_CSTR, out_len);
}

static int url_scheme_valid(const char *scheme, size_t len)
{
    if (len == 0 || len > PM_METAL_UTIL_URL_MAX_SCHEME) {
        return -1;
    }
    if (!isalpha((unsigned char)scheme[0])) {
        return -1;
    }
    for (size_t i = 1; i < len; i++) {
        unsigned char c = (unsigned char)scheme[i];
        if (!isalnum(c) && c != '+' && c != '-' && c != '.') {
            return -1;
        }
    }
    return 0;
}

static char *dup_slice(const char *start, size_t len, size_t max_len)
{
    if (len > max_len) {
        return NULL;
    }
    if (len > 0 && start == NULL) {
        return NULL;
    }

    char *out = malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }
    if (len > 0) {
        memcpy(out, start, len);
    }
    out[len] = '\0';
    return out;
}

static char *dup_cstr(const char *s, size_t max_len)
{
    size_t len;
    if (url_bounded_len(s, max_len, &len) != 0) {
        return NULL;
    }
    return dup_slice(s, len, max_len);
}

struct pm_metal_util_url_Tag {
    char *cstr;
    char *scheme;
    char *user;
    char *host;
    int has_port;
    uint16_t port;
    char *path;
    char *query;
    char *fragment;
};

static int parse_authority(const char *authority, size_t authority_len, char **user_out,
                           char **host_out, int *has_port, uint16_t *port)
{
    if (authority == NULL || user_out == NULL || host_out == NULL || has_port == NULL ||
        port == NULL) {
        return -1;
    }
    if (authority_len > PM_METAL_UTIL_URL_MAX_HOST + PM_METAL_UTIL_URL_MAX_USER + 1) {
        return -1;
    }

    const char *user = NULL;
    size_t user_len = 0;
    const char *host_port = authority;
    size_t host_port_len = authority_len;

    const char *at = memchr(authority, '@', authority_len);
    if (at != NULL) {
        user = authority;
        user_len = (size_t)(at - authority);
        if (user_len > PM_METAL_UTIL_URL_MAX_USER) {
            return -1;
        }
        host_port = at + 1;
        host_port_len = authority_len - user_len - 1;
    }

    if (host_port_len == 0 && user != NULL) {
        *host_out = dup_slice("", 0, PM_METAL_UTIL_URL_MAX_HOST);
        if (*host_out == NULL) {
            return -1;
        }
        if (user_len > 0) {
            *user_out = dup_slice(user, user_len, PM_METAL_UTIL_URL_MAX_USER);
            if (*user_out == NULL) {
                return -1;
            }
        }
        return 0;
    }

    if (host_port_len > PM_METAL_UTIL_URL_MAX_HOST) {
        return -1;
    }

    const char *colon = NULL;
    for (size_t i = host_port_len; i > 0; i--) {
        if (host_port[i - 1] == ':') {
            colon = host_port + i - 1;
            break;
        }
    }

    size_t host_len = host_port_len;
    if (colon != NULL && colon > host_port) {
        const char *p = colon + 1;
        size_t port_len = (size_t)((host_port + host_port_len) - p);
        if (port_len == 0 || port_len > 5) {
            return -1;
        }
        int all_digits = 1;
        for (const char *c = p; c < host_port + host_port_len; c++) {
            if (!isdigit((unsigned char)*c)) {
                all_digits = 0;
                break;
            }
        }
        if (all_digits) {
            unsigned long value = strtoul(p, NULL, 10);
            if (value <= 65535UL) {
                *has_port = 1;
                *port = (uint16_t)value;
                host_len = (size_t)(colon - host_port);
            }
        }
    }

    *host_out = dup_slice(host_port, host_len, PM_METAL_UTIL_URL_MAX_HOST);
    if (*host_out == NULL) {
        return -1;
    }
    if (user_len > 0) {
        *user_out = dup_slice(user, user_len, PM_METAL_UTIL_URL_MAX_USER);
        if (*user_out == NULL) {
            return -1;
        }
    }
    return 0;
}

static int parse_tail(const char *tail, char **path_out, char **query_out, char **fragment_out)
{
    if (tail == NULL || path_out == NULL || query_out == NULL || fragment_out == NULL) {
        return -1;
    }

    size_t tail_len;
    if (url_bounded_len(tail, PM_METAL_UTIL_URL_MAX_PATH + PM_METAL_UTIL_URL_MAX_QUERY +
                                PM_METAL_UTIL_URL_MAX_FRAGMENT + 2,
                        &tail_len) != 0) {
        return -1;
    }

    const char *hash = memchr(tail, '#', tail_len);
    const char *without_frag = tail;
    const char *fragment = NULL;
    if (hash != NULL) {
        fragment = hash + 1;
    }

    size_t without_frag_len = hash != NULL ? (size_t)(hash - tail) : tail_len;
    const char *qmark = memchr(without_frag, '?', without_frag_len);
    const char *path = without_frag;
    size_t path_len = without_frag_len;
    const char *query = NULL;
    size_t query_len = 0;

    if (qmark != NULL) {
        path_len = (size_t)(qmark - without_frag);
        query = qmark + 1;
        query_len = without_frag_len - path_len - 1;
    }

    if (path_len > PM_METAL_UTIL_URL_MAX_PATH || query_len > PM_METAL_UTIL_URL_MAX_QUERY) {
        return -1;
    }

    *path_out = dup_slice(path, path_len, PM_METAL_UTIL_URL_MAX_PATH);
    if (*path_out == NULL) {
        return -1;
    }
    if (query != NULL && query_len > 0) {
        *query_out = dup_slice(query, query_len, PM_METAL_UTIL_URL_MAX_QUERY);
        if (*query_out == NULL) {
            return -1;
        }
    }
    if (fragment != NULL && *fragment != '\0') {
        size_t fragment_len;
        if (url_bounded_len(fragment, PM_METAL_UTIL_URL_MAX_FRAGMENT, &fragment_len) != 0) {
            return -1;
        }
        *fragment_out = dup_slice(fragment, fragment_len, PM_METAL_UTIL_URL_MAX_FRAGMENT);
        if (*fragment_out == NULL) {
            return -1;
        }
    }
    return 0;
}

pm_metal_util_url_Tag *pm_metal_util_url_parse(const char *s)
{
    size_t total_len;
    if (url_bounded_len(s, PM_METAL_UTIL_URL_MAX_CSTR, &total_len) != 0) {
        return NULL;
    }
    if (total_len < 4) {
        return NULL;
    }

    const char *colon = memchr(s, ':', total_len);
    if (colon == NULL || (size_t)(colon - s) + 3 > total_len ||
        memcmp(colon, "://", 3) != 0) {
        return NULL;
    }

    size_t scheme_len = (size_t)(colon - s);
    if (url_scheme_valid(s, scheme_len) != 0) {
        return NULL;
    }

    pm_metal_util_url_Tag *url = calloc(1, sizeof(*url));
    if (url == NULL) {
        return NULL;
    }

    url->cstr = dup_cstr(s, PM_METAL_UTIL_URL_MAX_CSTR);
    if (url->cstr == NULL) {
        free(url);
        return NULL;
    }

    url->scheme = dup_slice(s, scheme_len, PM_METAL_UTIL_URL_MAX_SCHEME);
    if (url->scheme == NULL) {
        pm_metal_util_url_free(url);
        return NULL;
    }

    const char *authority = colon + 3;
    const char *tail = authority;
    for (; (size_t)(tail - s) < total_len; tail++) {
        if (*tail == '/' || *tail == '?' || *tail == '#') {
            break;
        }
    }

    if (parse_authority(authority, (size_t)(tail - authority), &url->user, &url->host,
                        &url->has_port, &url->port) != 0) {
        pm_metal_util_url_free(url);
        return NULL;
    }

    if (parse_tail(tail, &url->path, &url->query, &url->fragment) != 0) {
        pm_metal_util_url_free(url);
        return NULL;
    }

    return url;
}

void pm_metal_util_url_free(pm_metal_util_url_Tag *url)
{
    if (url == NULL) {
        return;
    }
    free(url->cstr);
    free(url->scheme);
    free(url->user);
    free(url->host);
    free(url->path);
    free(url->query);
    free(url->fragment);
    free(url);
}

const char *pm_metal_util_url_cstr(const pm_metal_util_url_Tag *url)
{
    return url != NULL ? url->cstr : NULL;
}

int pm_metal_util_url_format(const pm_metal_util_url_Tag *url, char *buf, size_t cap)
{
    if (url == NULL || buf == NULL || cap == 0) {
        return -1;
    }
    const char *cstr = pm_metal_util_url_cstr(url);
    if (cstr == NULL) {
        return -1;
    }
    size_t len;
    if (url_bounded_len(cstr, PM_METAL_UTIL_URL_MAX_CSTR, &len) != 0) {
        return -1;
    }
    if (cap <= len) {
        return -1;
    }
    memcpy(buf, cstr, len + 1);
    return 0;
}

const char *pm_metal_util_url_scheme(const pm_metal_util_url_Tag *url)
{
    return url != NULL ? url->scheme : NULL;
}

const char *pm_metal_util_url_user(const pm_metal_util_url_Tag *url)
{
    return url != NULL ? url->user : NULL;
}

const char *pm_metal_util_url_host(const pm_metal_util_url_Tag *url)
{
    return url != NULL ? url->host : NULL;
}

int pm_metal_util_url_port(const pm_metal_util_url_Tag *url, uint16_t *port_out)
{
    if (url == NULL || port_out == NULL || !url->has_port) {
        return -1;
    }
    *port_out = url->port;
    return 0;
}

const char *pm_metal_util_url_path(const pm_metal_util_url_Tag *url)
{
    return url != NULL && url->path != NULL ? url->path : "";
}

const char *pm_metal_util_url_query(const pm_metal_util_url_Tag *url)
{
    return url != NULL ? url->query : NULL;
}

const char *pm_metal_util_url_fragment(const pm_metal_util_url_Tag *url)
{
    return url != NULL ? url->fragment : NULL;
}

int pm_metal_util_url_is_file(const pm_metal_util_url_Tag *url)
{
    const char *scheme = pm_metal_util_url_scheme(url);
    return scheme != NULL && strcmp(scheme, "file") == 0;
}

const char *pm_metal_util_url_file_path(const pm_metal_util_url_Tag *url)
{
    if (!pm_metal_util_url_is_file(url)) {
        return NULL;
    }
    const char *path = pm_metal_util_url_path(url);
    if (path[0] == '\0') {
        return NULL;
    }
    return path;
}

pm_metal_util_url_Tag *pm_metal_util_url_build(const char *scheme, const char *host, const char *path)
{
    size_t scheme_len;
    size_t host_len;
    size_t path_len = 0;

    if (url_bounded_len(scheme, PM_METAL_UTIL_URL_MAX_SCHEME, &scheme_len) != 0 ||
        url_scheme_valid(scheme, scheme_len) != 0) {
        return NULL;
    }
    if (url_bounded_len(host, PM_METAL_UTIL_URL_MAX_HOST, &host_len) != 0) {
        return NULL;
    }
    if (path != NULL &&
        url_bounded_len(path, PM_METAL_UTIL_URL_MAX_PATH, &path_len) != 0) {
        return NULL;
    }

    size_t need_slash = (path_len > 0 && path[0] != '/') ? 1 : 0;
    if (scheme_len + 3 + host_len + need_slash + path_len >= PM_METAL_UTIL_URL_MAX_CSTR) {
        return NULL;
    }

    size_t cap = scheme_len + 3 + host_len + need_slash + path_len + 1;
    char *buf = malloc(cap);
    if (buf == NULL) {
        return NULL;
    }

    if (path_len == 0) {
        snprintf(buf, cap, "%.*s://%.*s", (int)scheme_len, scheme, (int)host_len, host);
    } else if (need_slash) {
        snprintf(buf, cap, "%.*s://%.*s/%.*s", (int)scheme_len, scheme, (int)host_len, host,
                 (int)path_len, path);
    } else {
        snprintf(buf, cap, "%.*s://%.*s%.*s", (int)scheme_len, scheme, (int)host_len, host,
                 (int)path_len, path);
    }

    pm_metal_util_url_Tag *url = pm_metal_util_url_parse(buf);
    free(buf);
    return url;
}
