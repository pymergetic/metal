#include "pymergetic/metal/net/faces/__init__.h"

#include <stddef.h>

#include <pymergetic/metal/reg/mod.h>

/* RegMod declare (C SoT) — loaded via pm_metal_net_faces_reg_load. */
static pm_metal_reg_export_t net_faces_exports[] = {
    PM_METAL_REG_EXPORT(mark),
    PM_METAL_REG_EXPORT(bits),
    PM_METAL_REG_EXPORT(format),
};
PM_METAL_REG_REF(net_faces, mark, 0);
PM_METAL_REG_REF(net_faces, bits, 1);
PM_METAL_REG_REF(net_faces, format, 2);
PM_METAL_REG_MOD(net_faces, "pymergetic.metal.net.faces")

static int32_t net_faces_register_symbols(void *ctx)
{
    (void)ctx;
    pm_metal_reg_export_publish(net_faces_mark, (void *)pm_metal_net_face_mark);
    pm_metal_reg_export_publish(net_faces_bits, (void *)pm_metal_net_face_bits);
    pm_metal_reg_export_publish(net_faces_format, (void *)pm_metal_net_face_format);
    return 0;
}

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
