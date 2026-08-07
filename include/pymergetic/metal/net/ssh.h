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

/* Clear sent flag so the next established peer gets a fresh banner. */
void pm_metal_ssh_banner_reset(void);

/*
 * Active open to host:port (name or dotted IPv4), send SSH-2.0-metal ident,
 * read peer banner. Returns 0 on SSH-2.0- peer banner, -1 error, -2 timeout,
 * -3 SYN timeout.
 */
int32_t pm_metal_ssh_client_ident(const char *host, uint16_t port, uint8_t *buf, uint32_t cap,
                                  uint32_t *len_out);

/* Same as ident but takes a host-order IPv4 address (no DNS). */
int32_t pm_metal_ssh_client_ident_ip(uint32_t addr, uint16_t port, uint8_t *buf, uint32_t cap,
                                     uint32_t *len_out);

#ifdef __cplusplus
}
#endif

#endif
