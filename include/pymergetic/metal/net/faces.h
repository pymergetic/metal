#ifndef PM_METAL_NET_FACES_H_
#define PM_METAL_NET_FACES_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bitmask of protocol faces that have successfully come up this boot. */
#define PM_METAL_NET_FACE_DHCP     (1u << 0)
#define PM_METAL_NET_FACE_HTTP     (1u << 1) /* server GET responder */
#define PM_METAL_NET_FACE_SSH      (1u << 2) /* banner server */
#define PM_METAL_NET_FACE_HTTP_CLI (1u << 3) /* outbound client */
#define PM_METAL_NET_FACE_NTP      (1u << 4)

void pm_metal_net_face_mark(uint32_t bit);
uint32_t pm_metal_net_face_bits(void);

/* Format short face tags into out (e.g. "http ssh ntp"). */
void pm_metal_net_face_format(char *out, uint32_t cap);

#ifdef __cplusplus
}
#endif

#endif
