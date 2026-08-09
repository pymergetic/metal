/*
 * ustar walk/write (browser/wasm seat) — same ABI as util/tar Rust on firmware.
 * Public face: include/pymergetic/metal/util/tar/__init__.h
 */
#include <pymergetic/metal/util/tar/__init__.h>

#include <string.h>

#define BLOCK 512u
#define NAME_MAX 100u

static int parse_octal(const uint8_t *p, size_t n, uint64_t *out)
{
    uint64_t v = 0;
    int seen = 0;
    size_t i;

    if (out == NULL) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        uint8_t c = p[i];
        if (c == 0 || c == ' ') {
            if (seen) {
                break;
            }
            continue;
        }
        if (c < '0' || c > '7') {
            return -1;
        }
        seen = 1;
        v = (v << 3) | (uint64_t)(c - '0');
    }
    *out = v;
    return 0;
}

static int checksum_ok(const uint8_t hdr[BLOCK])
{
    uint32_t sum_u = 0;
    int32_t sum_s = 0;
    uint64_t stored = 0;
    size_t i;

    for (i = 0; i < BLOCK; i++) {
        uint8_t b = (i >= 148u && i < 156u) ? (uint8_t)' ' : hdr[i];
        sum_u += b;
        sum_s += (int8_t)b;
    }
    if (parse_octal(hdr + 148, 8, &stored) != 0) {
        return 0;
    }
    return stored == (uint64_t)sum_u || stored == (uint64_t)(uint32_t)sum_s;
}

static int is_zero_block(const uint8_t hdr[BLOCK])
{
    size_t i;
    for (i = 0; i < BLOCK; i++) {
        if (hdr[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static size_t padded(uint64_t size)
{
    size_t s = (size_t)size;
    size_t rem = s % BLOCK;
    return rem == 0u ? s : s + (BLOCK - rem);
}

static void put_octal(uint8_t *dst, uint64_t v, size_t width)
{
    size_t i;
    for (i = width; i > 0u; i--) {
        if (i == width) {
            dst[i - 1u] = 0;
            continue;
        }
        dst[i - 1u] = (uint8_t)('0' + (v & 7u));
        v >>= 3;
    }
}

static void fill_checksum(uint8_t hdr[BLOCK])
{
    uint32_t sum = 0;
    size_t i;
    for (i = 148; i < 156u; i++) {
        hdr[i] = ' ';
    }
    for (i = 0; i < BLOCK; i++) {
        sum += hdr[i];
    }
    put_octal(hdr + 148, sum, 8);
    hdr[155] = ' ';
}

typedef int (*emit_fn)(void *ctx, const uint8_t *name, uint64_t size, int32_t is_dir,
                       uint64_t header_off, uint64_t payload_off, const uint8_t *data,
                       size_t data_len);

static int32_t walk(const uint8_t *archive, size_t len, emit_fn emit, void *ctx)
{
    size_t off = 0;
    int32_t count = 0;
    uint32_t zero_run = 0;

    if (archive == NULL || emit == NULL) {
        return -1;
    }
    while (off + BLOCK <= len) {
        uint8_t hdr[BLOCK];
        uint64_t header_off = (uint64_t)off;
        uint64_t size = 0;
        uint8_t typeflag;
        size_t name_end = 0;
        int name_is_dir;
        int32_t is_dir;
        uint8_t name[NAME_MAX + 1u];
        size_t nlen = 0;
        size_t data_off;
        size_t data_len;
        const uint8_t *data;
        int ustar;
        int empty_magic;
        int rc;

        memcpy(hdr, archive + off, BLOCK);
        off += BLOCK;

        if (is_zero_block(hdr)) {
            zero_run++;
            if (zero_run >= 2u) {
                return count;
            }
            continue;
        }
        zero_run = 0;
        if (!checksum_ok(hdr)) {
            return -1;
        }
        ustar = hdr[257] == 'u' && hdr[258] == 's' && hdr[259] == 't' && hdr[260] == 'a' &&
                hdr[261] == 'r';
        empty_magic = hdr[257] == 0 && hdr[258] == 0 && hdr[259] == 0 && hdr[260] == 0 &&
                      hdr[261] == 0;
        if (!ustar && !empty_magic) {
            return -1;
        }
        if (parse_octal(hdr + 124, 12, &size) != 0) {
            return -1;
        }
        typeflag = hdr[156];
        while (name_end < NAME_MAX && hdr[name_end] != 0) {
            name_end++;
        }
        name_is_dir = name_end > 0u && hdr[name_end - 1u] == '/';
        is_dir = (typeflag == '5' || ((typeflag == 0 || typeflag == '0') && name_is_dir)) ? 1 : 0;
        while (nlen < NAME_MAX && hdr[nlen] != 0) {
            name[nlen] = hdr[nlen];
            nlen++;
        }
        name[nlen] = 0;

        data_off = off;
        data_len = is_dir ? 0u : (size_t)size;
        if (data_off + data_len > len) {
            return -1;
        }
        data = data_len == 0u ? NULL : archive + data_off;
        rc = emit(ctx, name, size, is_dir, header_off, (uint64_t)data_off, data, data_len);
        if (rc != 0) {
            return -1;
        }
        count++;
        off += padded(size);
        if (off > len) {
            return -1;
        }
    }
    if (off == len || zero_run > 0u) {
        return count;
    }
    return -1;
}

typedef struct {
    pm_metal_util_tar_foreach_fn cb;
    uint8_t *user;
} foreach_ctx_t;

typedef struct {
    pm_metal_util_tar_foreach_ex_fn cb;
    uint8_t *user;
} foreach_ex_ctx_t;

static int emit_foreach(void *ctx, const uint8_t *name, uint64_t size, int32_t is_dir,
                        uint64_t header_off, uint64_t payload_off, const uint8_t *data,
                        size_t data_len)
{
    foreach_ctx_t *c = (foreach_ctx_t *)ctx;
    (void)header_off;
    (void)payload_off;
    return c->cb(c->user, name, size, is_dir, data, data_len);
}

static int emit_foreach_ex(void *ctx, const uint8_t *name, uint64_t size, int32_t is_dir,
                           uint64_t header_off, uint64_t payload_off, const uint8_t *data,
                           size_t data_len)
{
    foreach_ex_ctx_t *c = (foreach_ex_ctx_t *)ctx;
    return c->cb(c->user, name, size, is_dir, header_off, payload_off, data, data_len);
}

int32_t pm_metal_util_tar_foreach(const uint8_t *archive, size_t len,
                                  pm_metal_util_tar_foreach_fn cb, uint8_t *ctx)
{
    foreach_ctx_t c;
    if (cb == NULL) {
        return -1;
    }
    c.cb = cb;
    c.user = ctx;
    return walk(archive, len, emit_foreach, &c);
}

int32_t pm_metal_util_tar_foreach_ex(const uint8_t *archive, size_t len,
                                     pm_metal_util_tar_foreach_ex_fn cb, uint8_t *ctx)
{
    foreach_ex_ctx_t c;
    if (cb == NULL) {
        return -1;
    }
    c.cb = cb;
    c.user = ctx;
    return walk(archive, len, emit_foreach_ex, &c);
}

int32_t pm_metal_util_tar_write_header(uint8_t *out, size_t out_cap, const uint8_t *name,
                                       uint64_t size, uint8_t typeflag)
{
    uint8_t hdr[BLOCK];
    size_t nlen = 0;

    if (out == NULL || name == NULL || out_cap < BLOCK) {
        return -1;
    }
    memset(hdr, 0, sizeof hdr);
    while (nlen < NAME_MAX && name[nlen] != 0) {
        hdr[nlen] = name[nlen];
        nlen++;
    }
    if (nlen == 0u || nlen >= NAME_MAX) {
        return -1;
    }
    put_octal(hdr + 100, 0644ull, 8);
    put_octal(hdr + 108, 0, 8);
    put_octal(hdr + 116, 0, 8);
    put_octal(hdr + 124, size, 12);
    put_octal(hdr + 136, 0, 12);
    hdr[156] = typeflag;
    hdr[257] = 'u';
    hdr[258] = 's';
    hdr[259] = 't';
    hdr[260] = 'a';
    hdr[261] = 'r';
    hdr[262] = 0;
    hdr[263] = '0';
    hdr[264] = '0';
    fill_checksum(hdr);
    memcpy(out, hdr, BLOCK);
    return (int32_t)BLOCK;
}

size_t pm_metal_util_tar_pad_len(uint64_t size)
{
    size_t rem = (size_t)size % BLOCK;
    return rem == 0u ? 0u : BLOCK - rem;
}

int32_t pm_metal_util_tar_write_end(uint8_t *out, size_t out_cap)
{
    if (out == NULL || out_cap < BLOCK * 2u) {
        return -1;
    }
    memset(out, 0, BLOCK * 2u);
    return (int32_t)(BLOCK * 2u);
}
