#ifndef PM_METAL_ASGI_WS_H_
#define PM_METAL_ASGI_WS_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int pm_metal_asgi_ws_request(const char *req, char *key_out, size_t key_cap);
int pm_metal_asgi_ws_accept_key(const char *client_key, char *out, size_t out_cap);
int32_t pm_metal_asgi_ws_decode(const uint8_t *frame, uint32_t n, uint8_t *opcode,
                                uint8_t *out, uint32_t out_cap, uint32_t *out_len);
uint32_t pm_metal_asgi_ws_encode_server(uint8_t opcode, const uint8_t *payload, uint32_t plen,
                                        uint8_t *out, uint32_t out_cap);

#ifdef __cplusplus
}
#endif

#endif
