#ifndef PM_METAL_NET_SSH_H_
#define PM_METAL_NET_SSH_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* After TCP is ESTABLISHED, send the SSH identification string once. */
int32_t pm_metal_ssh_banner_send(void);

/* 1 after banner TX succeeded. */
int32_t pm_metal_ssh_banner_sent(void);

#ifdef __cplusplus
}
#endif

#endif
