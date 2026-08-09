/*
 * Browser / no-RS twin of util.size (firmware uses RS callee).
 */
#include <pymergetic/metal/util/size/__init__.h>

#include <string.h>

static size_t write_u64_suf(uint8_t *out, size_t cap, uint64_t v, const char *suf)
{
    char digs[20];
    size_t d = 0;
    size_t i = 0;
    size_t sl;

    if (cap == 0u) {
        return 0;
    }
    if (v == 0u) {
        digs[0] = '0';
        d = 1;
    } else {
        while (v > 0u && d < sizeof digs) {
            digs[d++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
    }
    while (d > 0u && i < cap) {
        out[i++] = (uint8_t)digs[--d];
    }
    sl = strlen(suf);
    if (i + sl > cap) {
        return 0;
    }
    memcpy(out + i, suf, sl);
    return i + sl;
}

static size_t format_into(uint8_t *out, size_t cap, uint64_t bytes)
{
    static const struct {
        uint64_t unit;
        const char *suf;
    } units[] = {
        { 1024ull * 1024ull * 1024ull * 1024ull, " TiB" },
        { 1024ull * 1024ull * 1024ull, " GiB" },
        { 1024ull * 1024ull, " MiB" },
        { 1024ull, " KiB" },
    };
    size_t u;

    for (u = 0; u < sizeof units / sizeof units[0]; u++) {
        if (bytes >= units[u].unit) {
            return write_u64_suf(out, cap, bytes / units[u].unit, units[u].suf);
        }
    }
    return write_u64_suf(out, cap, bytes, " B");
}

int32_t pm_metal_util_size_format(uint8_t *out, size_t cap, uint64_t bytes)
{
    uint8_t buf[PM_METAL_UTIL_SIZE_FORMAT_MAX];
    size_t n;

    if (out == NULL || cap == 0u) {
        return -1;
    }
    n = format_into(buf, sizeof buf, bytes);
    if (n + 1u > cap) {
        return -1;
    }
    memcpy(out, buf, n);
    out[n] = 0;
    return (int32_t)n;
}

int32_t pm_metal_util_size_format_bytes(uint8_t *out, size_t cap, uint64_t bytes)
{
    uint8_t human[PM_METAL_UTIL_SIZE_FORMAT_MAX];
    char tmp[48];
    size_t hn;
    size_t i = 0;
    uint64_t v = bytes;

    if (out == NULL || cap == 0u) {
        return -1;
    }
    hn = format_into(human, sizeof human, bytes);
    if (v == 0u) {
        tmp[i++] = '0';
    } else {
        char digs[20];
        size_t d = 0;
        while (v > 0u && d < sizeof digs) {
            digs[d++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
        while (d > 0u) {
            tmp[i++] = digs[--d];
        }
    }
    if (i + 2u + hn + 1u >= sizeof tmp) {
        return -1;
    }
    tmp[i++] = ' ';
    tmp[i++] = '(';
    memcpy(tmp + i, human, hn);
    i += hn;
    tmp[i++] = ')';
    if (i + 1u > cap) {
        return -1;
    }
    memcpy(out, tmp, i);
    out[i] = 0;
    return (int32_t)i;
}
