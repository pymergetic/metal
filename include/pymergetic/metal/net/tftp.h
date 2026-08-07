#ifndef PM_METAL_NET_TFTP_H_
#define PM_METAL_NET_TFTP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TFTP RRQ (octet). Reads first DATA block into buf.
 * Returns 0 on success, -1 error, -2 timeout.
 */
int32_t pm_metal_tftp_get(uint32_t server_ip, const char *filename,
                          uint8_t *buf, uint32_t cap, uint32_t *len_out);

#ifdef __cplusplus
}
#endif

#endif
