#include "pymergetic/metal/net/faces.h"

#include <stddef.h>

static uint32_t g_faces;

void pm_metal_net_face_mark(uint32_t bit)
{
    g_faces |= bit;
}

uint32_t pm_metal_net_face_bits(void)
{
    return g_faces;
}

static void append(char *out, uint32_t cap, const char *s)
{
    uint32_t i = 0;
    uint32_t o = 0;

    if (out == NULL || cap == 0u || s == NULL) {
        return;
    }
    while (o + 1u < cap && out[o] != '\0') {
        o++;
    }
    if (o > 0u && o + 1u < cap) {
        out[o++] = ' ';
    }
    while (o + 1u < cap && s[i] != '\0') {
        out[o++] = s[i++];
    }
    out[o] = '\0';
}

void pm_metal_net_face_format(char *out, uint32_t cap)
{
    if (out == NULL || cap == 0u) {
        return;
    }
    out[0] = '\0';
    if ((g_faces & PM_METAL_NET_FACE_HTTP) != 0u) {
        append(out, cap, "http");
    }
    if ((g_faces & PM_METAL_NET_FACE_SSH) != 0u) {
        append(out, cap, "ssh");
    }
    if ((g_faces & PM_METAL_NET_FACE_HTTP_CLI) != 0u) {
        append(out, cap, "httpc");
    }
    if ((g_faces & PM_METAL_NET_FACE_NTP) != 0u) {
        append(out, cap, "ntp");
    }
    if ((g_faces & PM_METAL_NET_FACE_TFTP) != 0u) {
        append(out, cap, "tftp");
    }
    if ((g_faces & PM_METAL_NET_FACE_SSH_CLI) != 0u) {
        append(out, cap, "sshc");
    }
    if (out[0] == '\0') {
        append(out, cap, "faces:none");
    }
}
