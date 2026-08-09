/*
 * Seat registry — Meyers circular ring. Static nodes arrive via linker
 * section; packs/users splice with pm_metal_reg_seat_register.
 */
#include <pymergetic/metal/reg/seats.h>
#include <pymergetic/metal/mem/port/__init__.h>

#include "py/builtin.h"
#include "py/frozenmod.h"
#include "py/nlr.h"
#include "py/obj.h"
#include "py/objmodule.h"
#include "py/runtime.h"

#include <string.h>

static pm_metal_reg_seat_t *g_ring; /* any node; NULL if empty */
static int g_booted;

static int path_eq(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return 0;
    }
    return strcmp(a, b) == 0;
}

static int32_t copy_path(char *dst, size_t cap, const char *src)
{
    size_t i;
    if (dst == NULL || cap == 0 || src == NULL || !src[0]) {
        return -1;
    }
    for (i = 0; src[i] && i + 1u < cap; i++) {
        dst[i] = src[i];
    }
    if (src[i]) {
        return -1;
    }
    dst[i] = 0;
    return 0;
}

pm_metal_reg_seat_t *pm_metal_reg_seat_ring(void)
{
    return g_ring;
}

static pm_metal_reg_seat_t *find_seat(const char *path)
{
    pm_metal_reg_seat_t *start;
    pm_metal_reg_seat_t *cur;

    if (path == NULL || !path[0] || g_ring == NULL) {
        return NULL;
    }
    start = g_ring;
    cur = start;
    do {
        if (path_eq(cur->path, path)) {
            return cur;
        }
        cur = cur->next;
    } while (cur != NULL && cur != start);
    return NULL;
}

int32_t pm_metal_reg_seat_splice(pm_metal_reg_seat_t *node)
{
    pm_metal_reg_seat_t *exist;

    if (node == NULL || node->path == NULL || !node->path[0]) {
        return -1;
    }
    exist = find_seat(node->path);
    if (exist != NULL && exist != node) {
        exist->kind = node->kind;
        exist->fw = node->fw;
        exist->browser = node->browser;
        exist->flags = node->flags;
        if (node->test != NULL) {
            exist->test = node->test;
        }
        return 0;
    }
    if (node->linked) {
        return 0;
    }
    if (g_ring == NULL) {
        node->next = node;
        node->prev = node;
        g_ring = node;
    } else {
        pm_metal_reg_seat_t *tail = g_ring->prev;
        tail->next = node;
        node->prev = tail;
        node->next = g_ring;
        g_ring->prev = node;
    }
    node->linked = 1u;
    return 0;
}

/*
 * Default seat proof: path == import. Frozen finder on path (.→/), else
 * progressive nest resolve of the path segments as given — never rewrite.
 */
static int32_t seat_import_test(const char *path)
{
    char parts[12][PM_METAL_REG_SEAT_PATH_MAX];
    uint32_t nparts = 0;
    uint32_t i;
    const char *p;
    size_t seg;
    mp_obj_t cur = MP_OBJ_NULL;
    char dotted[PM_METAL_REG_SEAT_PATH_MAX + 8];
    char fpath[PM_METAL_REG_SEAT_PATH_MAX + 8];
    size_t flen;
    mp_import_stat_t st;

    if (path == NULL || !path[0]) {
        return -1;
    }

    flen = 0;
    for (p = path; *p && flen + 1u < sizeof(fpath); p++) {
        fpath[flen++] = (*p == '.') ? '/' : *p;
    }
    fpath[flen] = 0;
    st = mp_find_frozen_module(fpath, NULL, NULL);
    if (st == MP_IMPORT_STAT_DIR || st == MP_IMPORT_STAT_FILE) {
        return 0;
    }

    p = path;
    while (*p && nparts < 12) {
        seg = 0;
        while (*p && *p != '.' && seg + 1u < sizeof(parts[0])) {
            parts[nparts][seg++] = *p++;
        }
        parts[nparts][seg] = 0;
        if (seg == 0) {
            return -1;
        }
        nparts++;
        if (*p == '.') {
            p++;
        }
    }
    if (*p || nparts == 0) {
        return -1;
    }

    for (i = 0; i < nparts; i++) {
        size_t d = 0;
        uint32_t j;
        mp_obj_t nxt = MP_OBJ_NULL;
        nlr_buf_t nlr;

        for (j = 0; j <= i; j++) {
            const char *s = parts[j];
            if (j > 0) {
                if (d + 1u >= sizeof(dotted)) {
                    return -1;
                }
                dotted[d++] = '.';
            }
            while (*s && d + 1u < sizeof(dotted)) {
                dotted[d++] = *s++;
            }
        }
        dotted[d] = 0;

        nxt = mp_module_get_builtin(qstr_from_str(dotted), false);
        if (nxt == MP_OBJ_NULL) {
            nxt = mp_module_get_builtin(qstr_from_str(dotted), true);
        }
        if (nxt == MP_OBJ_NULL && cur != MP_OBJ_NULL) {
            if (nlr_push(&nlr) == 0) {
                nxt = mp_load_attr(cur, qstr_from_str(parts[i]));
                nlr_pop();
            } else {
                nxt = MP_OBJ_NULL;
            }
        }
        if (nxt == MP_OBJ_NULL) {
            return -1;
        }
        cur = nxt;
    }
    return 0;
}

typedef struct {
    pm_metal_reg_seat_t seat;
    char path[];
} dyn_seat_t;

int32_t pm_metal_reg_seat_register_ex(const char *path, pm_metal_reg_seat_kind_t kind, uint8_t fw,
                                      uint8_t browser, uint8_t flags, pm_metal_reg_seat_test_fn test)
{
    pm_metal_reg_seat_t *exist;
    dyn_seat_t *dyn;
    size_t plen;
    size_t nbytes;

    if (path == NULL || !path[0]) {
        return -1;
    }
    pm_metal_reg_seats_boot();
    exist = find_seat(path);
    if (exist != NULL) {
        exist->kind = kind;
        exist->fw = fw ? 1u : 0u;
        exist->browser = browser ? 1u : 0u;
        exist->flags = flags;
        if (test != NULL) {
            exist->test = test;
        }
        return 0;
    }
    plen = strlen(path);
    nbytes = sizeof(dyn_seat_t) + plen + 1u;
    dyn = (dyn_seat_t *)pm_metal_mem_alloc(nbytes);
    if (dyn == NULL) {
        return -1;
    }
    memset(dyn, 0, nbytes);
    memcpy(dyn->path, path, plen + 1u);
    dyn->seat.path = dyn->path;
    dyn->seat.kind = kind;
    dyn->seat.fw = fw ? 1u : 0u;
    dyn->seat.browser = browser ? 1u : 0u;
    dyn->seat.flags = flags;
    dyn->seat.test = test;
    return pm_metal_reg_seat_splice(&dyn->seat);
}

int32_t pm_metal_reg_seat_register(const char *path, pm_metal_reg_seat_kind_t kind, uint8_t fw,
                                   uint8_t browser, pm_metal_reg_seat_test_fn test)
{
    return pm_metal_reg_seat_register_ex(path, kind, fw, browser, 0, test);
}

int32_t pm_metal_reg_seat_set_test(const char *path, pm_metal_reg_seat_test_fn fn)
{
    pm_metal_reg_seat_t *s;

    if (path == NULL || fn == NULL) {
        return -1;
    }
    pm_metal_reg_seats_boot();
    s = find_seat(path);
    if (s == NULL) {
        return -1;
    }
    s->test = fn;
    return 0;
}

void pm_metal_reg_seat_on_mod_load(const char *full_module)
{
    if (full_module == NULL || !full_module[0]) {
        return;
    }
    /* path == import; never strip a metal prefix. */
    (void)pm_metal_reg_seat_register(full_module, PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
}

uint32_t pm_metal_reg_seat_count(void)
{
    pm_metal_reg_seat_t *start;
    pm_metal_reg_seat_t *cur;
    uint32_t n = 0;

    pm_metal_reg_seats_boot();
    if (g_ring == NULL) {
        return 0;
    }
    start = g_ring;
    cur = start;
    do {
        n++;
        cur = cur->next;
    } while (cur != NULL && cur != start);
    return n;
}

int32_t pm_metal_reg_seat_at(uint32_t index, char *path_buf, uint32_t path_cap, int32_t *kind,
                             int32_t *fw, int32_t *browser, int32_t *has_test)
{
    pm_metal_reg_seat_t *start;
    pm_metal_reg_seat_t *cur;
    uint32_t i = 0;

    pm_metal_reg_seats_boot();
    if (g_ring == NULL) {
        return -1;
    }
    start = g_ring;
    cur = start;
    do {
        if (i == index) {
            if (path_buf != NULL && path_cap > 0) {
                if (copy_path(path_buf, path_cap, cur->path) != 0) {
                    return -1;
                }
            }
            if (kind != NULL) {
                *kind = (int32_t)cur->kind;
            }
            if (fw != NULL) {
                *fw = (int32_t)cur->fw;
            }
            if (browser != NULL) {
                *browser = (int32_t)cur->browser;
            }
            if (has_test != NULL) {
                *has_test = cur->test != NULL ? 1 : 0;
            }
            return 0;
        }
        i++;
        cur = cur->next;
    } while (cur != NULL && cur != start);
    return -1;
}

int32_t pm_metal_reg_seats_json(char *buf, uint32_t buf_len)
{
    pm_metal_reg_seat_t *start;
    pm_metal_reg_seat_t *cur;
    uint32_t pos = 0;
    int first = 1;

    pm_metal_reg_seats_boot();
    if (buf == NULL || buf_len < 3) {
        return -1;
    }
#define PUT(ch) \
    do { \
        if (pos + 1u >= buf_len) { \
            return -1; \
        } \
        buf[pos++] = (char)(ch); \
    } while (0)
#define PUTS(s) \
    do { \
        const char *_p = (s); \
        while (*_p) { \
            PUT(*_p++); \
        } \
    } while (0)
    PUT('[');
    if (g_ring != NULL) {
        start = g_ring;
        cur = start;
        do {
            const char *p;
            if (!first) {
                PUT(',');
            }
            first = 0;
            PUTS("{\"path\":\"");
            for (p = cur->path; p && *p; p++) {
                if (*p == '"' || *p == '\\') {
                    PUT('\\');
                }
                PUT(*p);
            }
            PUTS("\",\"kind\":\"");
            PUTS(cur->kind == PM_METAL_REG_SEAT_FROZEN ? "frozen" : "glue");
            PUTS("\",\"fw\":");
            PUTS(cur->fw ? "true" : "false");
            PUTS(",\"browser\":");
            PUTS(cur->browser ? "true" : "false");
            PUTS(",\"has_test\":");
            PUTS(cur->test ? "true" : "false");
            PUT('}');
            cur = cur->next;
        } while (cur != NULL && cur != start);
    }
    PUT(']');
    buf[pos] = 0;
    return (int32_t)pos;
#undef PUT
#undef PUTS
}

static void reg_puts(const char *s)
{
#if defined(PM_METAL_CFG_FW_BROWSER) && PM_METAL_CFG_FW_BROWSER
    (void)s;
#else
    extern void uart_puts(const char *s);
    uart_puts(s);
#endif
}

int32_t pm_metal_reg_run_tests(void)
{
    pm_metal_reg_seat_t *start;
    pm_metal_reg_seat_t *cur;
    uint32_t ok = 0;
    char msg[160];
    uint32_t n;

    pm_metal_reg_seats_boot();
    if (g_ring == NULL) {
        reg_puts("reg seats ok 0\n");
        reg_puts("upy ok\n");
        return 0;
    }
    start = g_ring;
    cur = start;
    do {
        int32_t rc;
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            if (cur->test != NULL) {
                rc = cur->test();
            } else if (cur->flags & PM_METAL_REG_SEAT_F_TEST_ONLY) {
                rc = -1;
            } else {
                rc = seat_import_test(cur->path);
            }
            nlr_pop();
        } else {
            rc = -1;
        }
        if (rc != 0) {
            n = 0;
            {
                const char *p = (cur->flags & PM_METAL_REG_SEAT_F_TEST_ONLY) ? "reg test fail "
                                                                             : "reg seat fail ";
                while (*p && n + 1u < sizeof(msg)) {
                    msg[n++] = *p++;
                }
            }
            {
                const char *p = cur->path ? cur->path : "?";
                while (*p && n + 1u < sizeof(msg)) {
                    msg[n++] = *p++;
                }
            }
            msg[n++] = '\n';
            msg[n] = 0;
            reg_puts(msg);
            return -1;
        }
        if (!(cur->flags & PM_METAL_REG_SEAT_F_TEST_ONLY)) {
            ok++;
        }
        cur = cur->next;
    } while (cur != NULL && cur != start);

    n = 0;
    {
        const char *p = "reg seats ok ";
        while (*p && n + 1u < sizeof(msg)) {
            msg[n++] = *p++;
        }
    }
    {
        char digits[12];
        uint32_t v = ok;
        int d = 0;
        if (v == 0) {
            digits[d++] = '0';
        } else {
            while (v && d < (int)sizeof(digits)) {
                digits[d++] = (char)('0' + (v % 10u));
                v /= 10u;
            }
            for (int a = 0, b = d - 1; a < b; a++, b--) {
                char t = digits[a];
                digits[a] = digits[b];
                digits[b] = t;
            }
        }
        for (int j = 0; j < d && n + 1u < sizeof(msg); j++) {
            msg[n++] = digits[j];
        }
    }
    msg[n++] = '\n';
    msg[n] = 0;
    reg_puts(msg);
    reg_puts("upy ok\n");
    return 0;
}

void pm_metal_reg_seats_boot(void)
{
    /* Linker seat section retired — seats are registered from RegMod floor
     * load / smoke helpers via pm_metal_reg_seat_register(_ex). */
    g_booted = 1;
}
