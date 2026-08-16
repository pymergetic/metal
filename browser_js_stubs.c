/* Stubs for sim L2 JS imports when proving the malloc boot path on host. */
#include <stdint.h>

int32_t pm_metal_drivers_net_sim_js_tx(const uint8_t *frame, uint16_t len) {
    (void)frame;
    (void)len;
    return 0;
}

int32_t pm_metal_drivers_net_sim_js_rx(uint8_t *frame, uint16_t max) {
    (void)frame;
    (void)max;
    return 0;
}
