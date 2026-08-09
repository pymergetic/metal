/*
 * Browser net.tftp — same C ABI; no UDP RRQ in the browser seat.
 */
#include "pymergetic/metal/net/tftp/__init__.h"
#include "pymergetic/metal/async/handle.h"

#include <stddef.h>
#include <stdint.h>

uint32_t pm_metal_net_tftp_get_async(uint32_t server_ip, const char *filename, uint8_t *buf,
                                     uint32_t cap)
{
    (void)server_ip;
    (void)filename;
    (void)buf;
    (void)cap;
    return pm_metal_async_completed_u32(0u);
}

void pm_metal_net_tftp_poll(void) {}

int32_t pm_metal_net_tftp_status(void)
{
    return -1;
}

uint32_t pm_metal_net_tftp_len(void)
{
    return 0u;
}

const uint8_t *pm_metal_net_tftp_body(void)
{
    return NULL;
}

int32_t pm_metal_net_tftp_get(uint32_t server_ip, const char *filename, uint8_t *buf, uint32_t cap,
                              uint32_t *len_out)
{
    (void)server_ip;
    (void)filename;
    (void)buf;
    (void)cap;
    if (len_out) {
        *len_out = 0u;
    }
    return -1;
}
