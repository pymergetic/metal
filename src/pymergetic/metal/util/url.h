#ifndef PM_METAL_UTIL_URL_H
#define PM_METAL_UTIL_URL_H
#include <stddef.h>
#include <stdint.h>

#define PM_METAL_UTIL_URL_MAX_CSTR 2048u
#define PM_METAL_UTIL_URL_MAX_SCHEME 32u
#define PM_METAL_UTIL_URL_MAX_HOST 256u
#define PM_METAL_UTIL_URL_MAX_USER 256u
#define PM_METAL_UTIL_URL_MAX_PATH 1024u
#define PM_METAL_UTIL_URL_MAX_QUERY 1024u
#define PM_METAL_UTIL_URL_MAX_FRAGMENT 256u

typedef struct pm_metal_util_url_Tag pm_metal_util_url_Tag;

#ifdef __cplusplus
extern "C" {
#endif

int pm_metal_util_url_len(const char *s, size_t *out_len);

pm_metal_util_url_Tag *pm_metal_util_url_parse(const char *s);
void pm_metal_util_url_free(pm_metal_util_url_Tag *url);
pm_metal_util_url_Tag *pm_metal_util_url_build(const char *scheme, const char *host, const char *path);
int pm_metal_util_url_format(const pm_metal_util_url_Tag *url, char *buf, size_t cap);
const char *pm_metal_util_url_cstr(const pm_metal_util_url_Tag *url);
const char *pm_metal_util_url_scheme(const pm_metal_util_url_Tag *url);
const char *pm_metal_util_url_user(const pm_metal_util_url_Tag *url);
const char *pm_metal_util_url_host(const pm_metal_util_url_Tag *url);
int pm_metal_util_url_port(const pm_metal_util_url_Tag *url, uint16_t *port_out);
const char *pm_metal_util_url_path(const pm_metal_util_url_Tag *url);
const char *pm_metal_util_url_query(const pm_metal_util_url_Tag *url);
const char *pm_metal_util_url_fragment(const pm_metal_util_url_Tag *url);
int pm_metal_util_url_is_file(const pm_metal_util_url_Tag *url);
const char *pm_metal_util_url_file_path(const pm_metal_util_url_Tag *url);

#ifdef __cplusplus
}

namespace pm::kernel::util {

struct Url {
    static int len(const char *s, size_t *out_len) { return pm_metal_util_url_len(s, out_len); }
    static pm_metal_util_url_Tag *parse(const char *s) { return pm_metal_util_url_parse(s); }
    static void free(pm_metal_util_url_Tag *url) { pm_metal_util_url_free(url); }
    static pm_metal_util_url_Tag *build(const char *scheme, const char *host, const char *path) {
        return pm_metal_util_url_build(scheme, host, path);
    }
    static int format(const pm_metal_util_url_Tag *url, char *buf, size_t cap) {
        return pm_metal_util_url_format(url, buf, cap);
    }
    static const char *cstr(const pm_metal_util_url_Tag *url) { return pm_metal_util_url_cstr(url); }
    static const char *scheme(const pm_metal_util_url_Tag *url) { return pm_metal_util_url_scheme(url); }
    static const char *user(const pm_metal_util_url_Tag *url) { return pm_metal_util_url_user(url); }
    static const char *host(const pm_metal_util_url_Tag *url) { return pm_metal_util_url_host(url); }
    static int port(const pm_metal_util_url_Tag *url, uint16_t *port_out) {
        return pm_metal_util_url_port(url, port_out);
    }
    static const char *path(const pm_metal_util_url_Tag *url) { return pm_metal_util_url_path(url); }
    static const char *query(const pm_metal_util_url_Tag *url) { return pm_metal_util_url_query(url); }
    static const char *fragment(const pm_metal_util_url_Tag *url) {
        return pm_metal_util_url_fragment(url);
    }
    static int file(const pm_metal_util_url_Tag *url) { return pm_metal_util_url_is_file(url); }
    static const char *local(const pm_metal_util_url_Tag *url) {
        return pm_metal_util_url_file_path(url);
    }
};

inline constexpr Url url{};

} /* namespace pm::kernel::util */
#endif

#endif /* PM_METAL_UTIL_URL_H */
