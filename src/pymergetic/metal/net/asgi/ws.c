/*
 * Minimal RFC6455 server upgrade + echo framing for ASGI prove.
 */
#include "ws.h"

#include <string.h>

#include "mbedtls/base64.h"
#include "mbedtls/sha1.h"

static const char k_ws_guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static int ascii_ieq(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
    }
    return 1;
}

static int line_has_token(const char *line, size_t len, const char *tok, size_t tlen)
{
    size_t i;
    for (i = 0; i + tlen <= len; i++) {
        if (ascii_ieq(line + i, tok, tlen)) {
            return 1;
        }
    }
    return 0;
}

int pm_metal_asgi_ws_request(const char *req, char *key_out, size_t key_cap)
{
    const char *p;
    const char *line;
    int upgrade = 0;
    int connection = 0;

    if (req == NULL || key_out == NULL || key_cap < 24u) {
        return 0;
    }
    key_out[0] = 0;
    p = req;
    while (*p) {
        line = p;
        while (*p && *p != '\n') {
            p++;
        }
        if (p > line) {
            size_t len = (size_t)(p - line);
            if (len > 0 && line[len - 1u] == '\r') {
                len--;
            }
            if (len >= 18 && ascii_ieq(line, "upgrade: websocket", 18)) {
                upgrade = 1;
            }
            if (len >= 11 && ascii_ieq(line, "connection:", 11)) {
                if (line_has_token(line, len, "upgrade", 7)) {
                    connection = 1;
                }
            }
            if (len >= 18 && ascii_ieq(line, "sec-websocket-key:", 18)) {
                const char *k = line + 18;
                size_t n = 0;
                while (*k == ' ') {
                    k++;
                }
                while (k[n] && k[n] != '\r' && k[n] != '\n' && n + 1u < key_cap) {
                    key_out[n] = k[n];
                    n++;
                }
                key_out[n] = 0;
            }
        }
        if (*p == '\n') {
            p++;
        }
    }
    return (upgrade && connection && key_out[0]) ? 1 : 0;
}

int pm_metal_asgi_ws_accept_key(const char *client_key, char *out, size_t out_cap)
{
    unsigned char sha[20];
    char concat[128];
    size_t n = 0;
    size_t olen = 0;
    size_t klen;

    if (client_key == NULL || out == NULL || out_cap < 32u) {
        return -1;
    }
    klen = strlen(client_key);
    if (klen + sizeof(k_ws_guid) > sizeof(concat)) {
        return -1;
    }
    memcpy(concat, client_key, klen);
    memcpy(concat + klen, k_ws_guid, sizeof(k_ws_guid)); /* includes NUL for sha of string w/o NUL */
    n = klen + (sizeof(k_ws_guid) - 1u);
    if (mbedtls_sha1((const unsigned char *)concat, n, sha) != 0) {
        return -1;
    }
    if (mbedtls_base64_encode((unsigned char *)out, out_cap, &olen, sha, sizeof(sha)) != 0) {
        return -1;
    }
    out[olen] = 0;
    return 0;
}

/* Unmask and copy payload; returns payload length or -1. */
int32_t pm_metal_asgi_ws_decode(const uint8_t *frame, uint32_t n, uint8_t *opcode,
                                uint8_t *out, uint32_t out_cap, uint32_t *out_len)
{
    uint8_t b0, b1;
    uint64_t plen;
    uint32_t hdr;
    uint8_t mask[4];
    uint32_t i;

    if (frame == NULL || n < 2u || opcode == NULL || out_len == NULL) {
        return -1;
    }
    b0 = frame[0];
    b1 = frame[1];
    *opcode = (uint8_t)(b0 & 0x0fu);
    if ((b1 & 0x80u) == 0u) {
        return -1; /* client frames must be masked */
    }
    plen = (uint64_t)(b1 & 0x7fu);
    hdr = 2u;
    if (plen == 126u) {
        if (n < 4u) {
            return 0;
        }
        plen = ((uint64_t)frame[2] << 8) | frame[3];
        hdr = 4u;
    } else if (plen == 127u) {
        return -1; /* skip huge frames in v1 */
    }
    if (n < hdr + 4u) {
        return 0;
    }
    memcpy(mask, frame + hdr, 4u);
    hdr += 4u;
    if (n < hdr + (uint32_t)plen) {
        return 0;
    }
    if ((uint32_t)plen > out_cap) {
        return -1;
    }
    for (i = 0; i < (uint32_t)plen; i++) {
        out[i] = (uint8_t)(frame[hdr + i] ^ mask[i & 3u]);
    }
    *out_len = (uint32_t)plen;
    return 1;
}

uint32_t pm_metal_asgi_ws_encode_server(uint8_t opcode, const uint8_t *payload, uint32_t plen,
                                        uint8_t *out, uint32_t out_cap)
{
    uint32_t o = 0;
    if (out == NULL || out_cap < 2u + plen || plen > 125u) {
        return 0;
    }
    out[o++] = (uint8_t)(0x80u | (opcode & 0x0fu));
    out[o++] = (uint8_t)plen;
    if (plen > 0u && payload != NULL) {
        memcpy(out + o, payload, plen);
        o += plen;
    }
    return o;
}
