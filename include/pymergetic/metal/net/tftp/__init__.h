#ifndef PM_METAL_NET_TFTP_H_
#define PM_METAL_NET_TFTP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Async TFTP RRQ (first DATA block). Await handle → DONE; result_u32 1/0.
 * Then status()/len()/body accessors.
 */
uint32_t pm_metal_net_tftp_get_async(uint32_t server_ip, const char *filename, uint8_t *buf,
                                     uint32_t cap);
void pm_metal_net_tftp_poll(void);
int32_t pm_metal_net_tftp_status(void);
uint32_t pm_metal_net_tftp_len(void);
const uint8_t *pm_metal_net_tftp_body(void);

/*
 * Sync façade: pumps until DONE.
 * Returns 0 on success, -1 error, -2 timeout.
 */
int32_t pm_metal_net_tftp_get(uint32_t server_ip, const char *filename, uint8_t *buf, uint32_t cap,
                              uint32_t *len_out);

#ifdef __cplusplus
}
#endif

#endif
