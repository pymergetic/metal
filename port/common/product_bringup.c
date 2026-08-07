#include "product_bringup.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/async/runner.h"
#include "pymergetic/metal/console.h"
#include "pymergetic/metal/dev/net.h"
#include "pymergetic/metal/mem.h"
#include "pymergetic/metal/net/dhcp.h"
#include "pymergetic/metal/net/ip/__init__.h"
#include "pymergetic/metal/net/pump.h"
#include "pymergetic/metal/net/upy_nic.h"

void uart_puts(const char *s);
void uart_write(const char *s, size_t n);

static uint8_t g_console_ring[8 * 1024];
static uint8_t g_metal_heap[512 * 1024] __attribute__((aligned(16)));

static void uart_sink(const uint8_t *data, size_t n, void *user)
{
    (void)user;
    uart_write((const char *)data, n);
}

static void nic_l2_poll(void)
{
    pm_metal_dev_net_virtio_poll(NULL, NULL);
}

static const pm_metal_net_upy_l2_ops_t g_virtio_l2_ops = {
    .open = pm_metal_dev_net_virtio_open,
    .mac = pm_metal_dev_net_virtio_mac,
    .tx = pm_metal_dev_net_virtio_tx,
    .poll = nic_l2_poll,
};

int pm_metal_product_bringup(void)
{
    uint8_t mac[6];
    pm_metal_net_dhcp_lease_t lease;
    int i;

    if (pm_metal_console_init(g_console_ring, sizeof(g_console_ring)) != 0) {
        uart_puts("bringup: console init fail\n");
        return -1;
    }
    if (pm_metal_console_attach(uart_sink, NULL) != 0) {
        uart_puts("bringup: console attach fail\n");
        return -1;
    }

    if (pm_metal_mem_init(g_metal_heap, sizeof(g_metal_heap)) != 0) {
        uart_puts("bringup: mem init fail\n");
        return -1;
    }
    if (pm_metal_async_start(1) != 0) {
        uart_puts("bringup: async start fail\n");
        return -1;
    }
    pm_metal_net_pump_bind_async();

    if (pm_metal_dev_net_virtio_probe(NULL) != 0 ||
        pm_metal_dev_net_virtio_open(mac) != 0 ||
        !pm_metal_dev_net_virtio_ready()) {
        uart_puts("bringup: net fail\n");
        return -1;
    }
    if (pm_metal_net_upy_nic_register("virtio-net", &g_virtio_l2_ops) != 0) {
        uart_puts("bringup: nic register fail\n");
        return -1;
    }

    if (pm_metal_net_ip_init(0, 0, 0) != 0 || !pm_metal_net_ip_ready()) {
        uart_puts("bringup: ip init fail\n");
        return -1;
    }
    memset(&lease, 0, sizeof(lease));
    if (pm_metal_net_dhcp_run(&lease) != 0) {
        uart_puts("bringup: dhcp fail\n");
        return -1;
    }
    if (pm_metal_net_ip_set_addrs(lease.yiaddr, lease.mask, lease.gw) != 0) {
        uart_puts("bringup: dhcp apply fail\n");
        return -1;
    }
    if (pm_metal_net_ip_set_dns(lease.dns != 0u ? lease.dns : PM_METAL_NET_IP_DEFAULT_DNS) != 0) {
        uart_puts("bringup: dns fail\n");
        return -1;
    }
    (void)pm_metal_net_ip_announce();
    for (i = 0; i < 64; i++) {
        (void)pm_metal_async_run_poll();
    }

    uart_puts("metal repl\n");
    return 0;
}
