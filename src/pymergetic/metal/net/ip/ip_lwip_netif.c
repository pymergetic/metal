#include "ip_lwip_internal.h"

#include <stdio.h>

#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/etharp.h"
#include "lwip/prot/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/icmp.h"
#include "lwip/raw.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/dhcp.h"
#include "netif/ethernet.h"

#include "pymergetic/metal/async/await.h"
#include "pymergetic/metal/async/handle.h"
#include "pymergetic/metal/dev/net.h"
#include "pymergetic/metal/dev/net/bge/bge_netif.h"
#include "pymergetic/metal/net/ip/__init__.h"

#include <pymergetic/metal/reg/mod.h>

/* RegMod declare (C SoT) — loaded via pm_metal_net_ip_reg_load. */
static pm_metal_reg_export_t net_ip_exports[] = {
    PM_METAL_REG_EXPORT(init),
    PM_METAL_REG_EXPORT(ready),
    PM_METAL_REG_EXPORT(poll),
    PM_METAL_REG_EXPORT(addr),
    PM_METAL_REG_EXPORT(gw),
    PM_METAL_REG_EXPORT(mask),
    PM_METAL_REG_EXPORT(dns),
    PM_METAL_REG_EXPORT(ping),
    PM_METAL_REG_EXPORT(close),
    PM_METAL_REG_EXPORT(bind),
    PM_METAL_REG_EXPORT(connect),
    PM_METAL_REG_EXPORT(listen),
    PM_METAL_REG_EXPORT(accept),
    PM_METAL_REG_EXPORT(recv),
    PM_METAL_REG_EXPORT(send),
};
PM_METAL_REG_REF(net_ip, init, 0);
PM_METAL_REG_REF(net_ip, ready, 1);
PM_METAL_REG_REF(net_ip, poll, 2);
PM_METAL_REG_REF(net_ip, addr, 3);
PM_METAL_REG_REF(net_ip, gw, 4);
PM_METAL_REG_REF(net_ip, mask, 5);
PM_METAL_REG_REF(net_ip, dns, 6);
PM_METAL_REG_REF(net_ip, ping, 7);
PM_METAL_REG_REF(net_ip, close, 8);
PM_METAL_REG_REF(net_ip, bind, 9);
PM_METAL_REG_REF(net_ip, connect, 10);
PM_METAL_REG_REF(net_ip, listen, 11);
PM_METAL_REG_REF(net_ip, accept, 12);
PM_METAL_REG_REF(net_ip, recv, 13);
PM_METAL_REG_REF(net_ip, send, 14);
PM_METAL_REG_MOD(net_ip, "pymergetic.metal.net.ip")

static int32_t net_ip_register_symbols(void *ctx)
{
    (void)ctx;
    pm_metal_reg_export_publish(net_ip_init, (void *)pm_metal_net_ip_init);
    pm_metal_reg_export_publish(net_ip_ready, (void *)pm_metal_net_ip_ready);
    pm_metal_reg_export_publish(net_ip_poll, (void *)pm_metal_net_ip_poll);
    pm_metal_reg_export_publish(net_ip_addr, (void *)pm_metal_net_ip_addr);
    pm_metal_reg_export_publish(net_ip_gw, (void *)pm_metal_net_ip_gw);
    pm_metal_reg_export_publish(net_ip_mask, (void *)pm_metal_net_ip_mask);
    pm_metal_reg_export_publish(net_ip_dns, (void *)pm_metal_net_ip_dns);
    pm_metal_reg_export_publish(net_ip_ping, (void *)pm_metal_net_ip_ping);
    pm_metal_reg_export_publish(net_ip_close, (void *)pm_metal_net_ip_close);
    pm_metal_reg_export_publish(net_ip_bind, (void *)pm_metal_net_ip_bind);
    pm_metal_reg_export_publish(net_ip_connect, (void *)pm_metal_net_ip_connect);
    pm_metal_reg_export_publish(net_ip_listen, (void *)pm_metal_net_ip_listen);
    pm_metal_reg_export_publish(net_ip_accept, (void *)pm_metal_net_ip_accept);
    pm_metal_reg_export_publish(net_ip_recv, (void *)pm_metal_net_ip_recv);
    pm_metal_reg_export_publish(net_ip_send, (void *)pm_metal_net_ip_send);
    return 0;
}
metal_net_iface_t g_metal_ifaces[METAL_NET_MAX_IFACES];
uint32_t g_metal_iface_count;
uint32_t g_metal_eth_count;
int32_t g_metal_default_idx = -1;
uint32_t g_metal_if_gen;
int32_t g_metal_lwip_inited;
msock_t g_metal_socks[METAL_NET_MAX_SOCKS + 1u];
char g_metal_dns_last[64];
uint32_t g_metal_if_wait_h;
uint32_t g_metal_if_wait_since;

static uint32_t g_ping_replies;
static uint16_t g_ping_id;
static uint16_t g_ping_seq;

void pm_metal_ip_bump_if_gen(void)
{
    g_metal_if_gen++;
    if (g_metal_if_wait_h != 0u && g_metal_if_gen != g_metal_if_wait_since) {
        pm_metal_async_set_result_u32(g_metal_if_wait_h, g_metal_if_gen);
        pm_metal_async_wake(g_metal_if_wait_h);
        g_metal_if_wait_h = 0u;
    }
}

void pm_metal_net_ip_bump_if_gen(void)
{
    pm_metal_ip_bump_if_gen();
}

void *pm_metal_net_ip_register_netif(const char *name, const char *backend, void *unused)
{
    uint32_t i;
    metal_net_iface_t *mif;
    (void)unused;
    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < METAL_NET_MAX_IFACES; i++) {
        if (g_metal_ifaces[i].used && strcmp(g_metal_ifaces[i].name, name) == 0) {
            return &g_metal_ifaces[i].netif;
        }
    }
    for (i = 0; i < METAL_NET_MAX_IFACES; i++) {
        if (!g_metal_ifaces[i].used) {
            break;
        }
    }
    if (i >= METAL_NET_MAX_IFACES) {
        return NULL;
    }
    mif = &g_metal_ifaces[i];
    memset(mif, 0, sizeof(*mif));
    snprintf(mif->name, sizeof(mif->name), "%s", name);
    snprintf(mif->backend, sizeof(mif->backend), "%s", backend ? backend : "virtual");
    mif->used = 1;
    g_metal_iface_count++;
    return &mif->netif;
}

void pm_metal_net_ip_unregister_named(const char *name)
{
    metal_net_iface_t *mif = pm_metal_ip_iface_by_name(name);
    if (mif == NULL) {
        return;
    }
    mif->used = 0;
    if (g_metal_iface_count > 0) {
        g_metal_iface_count--;
    }
}

metal_net_iface_t *pm_metal_ip_iface_by_name(const char *name)
{
    uint32_t i;
    if (name == NULL) {
        return pm_metal_ip_iface_default();
    }
    for (i = 0; i < METAL_NET_MAX_IFACES; i++) {
        if (g_metal_ifaces[i].used && strcmp(g_metal_ifaces[i].name, name) == 0) {
            return &g_metal_ifaces[i];
        }
    }
    return NULL;
}

metal_net_iface_t *pm_metal_ip_iface_default(void)
{
    uint32_t i;
    if (g_metal_default_idx >= 0 && g_metal_default_idx < (int32_t)METAL_NET_MAX_IFACES &&
        g_metal_ifaces[g_metal_default_idx].used) {
        return &g_metal_ifaces[g_metal_default_idx];
    }
    for (i = 0; i < METAL_NET_MAX_IFACES; i++) {
        if (g_metal_ifaces[i].used && strcmp(g_metal_ifaces[i].name, "lo") != 0) {
            return &g_metal_ifaces[i];
        }
    }
    return NULL;
}

int pm_metal_ip_parse_ipv4(const char *s, void *ip4_addr_out)
{
    return (s != NULL && ip4_addr_out != NULL && ip4addr_aton(s, (ip4_addr_t *)ip4_addr_out)) ? 0
                                                                                             : -1;
}

int pm_metal_ip_parse_host(const char *host, ip_addr_t *out)
{
    ip4_addr_t a4;
    if (host == NULL || out == NULL) {
        return -1;
    }
    if (strcmp(host, "localhost") == 0) {
        IP_ADDR4(out, 127, 0, 0, 1);
        return 0;
    }
    if (pm_metal_ip_parse_ipv4(host, &a4) == 0) {
        ip_addr_copy_from_ip4(*out, a4);
        return 0;
    }
    return -1;
}

static void store_ip4(char *dst, size_t n, const ip4_addr_t *a)
{
    if (dst == NULL || n == 0 || a == NULL) {
        return;
    }
    ip4addr_ntoa_r(a, dst, (int)n);
}

void pm_metal_ip_sync_iface(metal_net_iface_t *mif)
{
    const ip4_addr_t *dns;
    if (mif == NULL || !mif->used) {
        return;
    }
    store_ip4(mif->ip, sizeof(mif->ip), netif_ip4_addr(&mif->netif));
    store_ip4(mif->mask, sizeof(mif->mask), netif_ip4_netmask(&mif->netif));
    store_ip4(mif->gw, sizeof(mif->gw), netif_ip4_gw(&mif->netif));
    dns = (const ip4_addr_t *)dns_getserver(0);
    if (dns != NULL) {
        store_ip4(mif->dns, sizeof(mif->dns), dns);
    }
}

static void status_cb(struct netif *nif)
{
    metal_net_iface_t *mif = (metal_net_iface_t *)nif->state;
    pm_metal_ip_sync_iface(mif);
    pm_metal_ip_bump_if_gen();
}

static void link_cb(struct netif *nif)
{
    (void)nif;
    pm_metal_ip_bump_if_gen();
}

static err_t linkoutput(struct netif *netif, struct pbuf *p)
{
    metal_net_iface_t *mif = (metal_net_iface_t *)netif->state;
    uint8_t buf[METAL_NET_TX_MAX];
    uint16_t n;
    if (mif == NULL || mif->l2_tx == NULL || p == NULL) {
        return ERR_IF;
    }
    n = pbuf_copy_partial(p, buf, sizeof(buf), 0);
    if (n == 0 || mif->l2_tx(buf, n) != 0) {
        return ERR_IF;
    }
    return ERR_OK;
}

static err_t eth_netif_init(struct netif *netif)
{
    metal_net_iface_t *mif = (metal_net_iface_t *)netif->state;
    const uint8_t *mac;
    netif->name[0] = 'e';
    netif->name[1] = 'n';
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_IGMP;
    netif->hwaddr_len = ETH_HWADDR_LEN;
    netif->output = etharp_output;
    netif->linkoutput = linkoutput;
    if (mif != NULL && mif->l2_mac != NULL) {
        mac = mif->l2_mac();
        if (mac != NULL) {
            memcpy(netif->hwaddr, mac, ETH_HWADDR_LEN);
        }
    }
    return ERR_OK;
}

static err_t loop_out4(struct netif *netif, struct pbuf *p, const ip4_addr_t *addr)
{
    (void)addr;
    return netif_loop_output(netif, p);
}

static err_t loop_netif_init(struct netif *netif)
{
    netif->name[0] = 'l';
    netif->name[1] = 'o';
    netif->mtu = 65535;
    netif->flags = NETIF_FLAG_LINK_UP;
    memset(netif->hwaddr, 0, sizeof(netif->hwaddr));
    netif->hwaddr_len = ETH_HWADDR_LEN;
    netif->output = loop_out4;
    netif->linkoutput = NULL;
    return ERR_OK;
}

static void on_frame(void *ctx, const uint8_t *frame, uint32_t len)
{
    metal_net_iface_t *mif = (metal_net_iface_t *)ctx;
    struct pbuf *p;
    if (mif == NULL || frame == NULL || len == 0) {
        return;
    }
    p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
    if (p == NULL) {
        return;
    }
    if (pbuf_take(p, frame, (u16_t)len) != ERR_OK) {
        pbuf_free(p);
        return;
    }
    if (mif->netif.input(p, &mif->netif) != ERR_OK) {
        pbuf_free(p);
    }
}

void pm_metal_dhcp_parse_option(struct netif *netif, struct dhcp *dhcp, unsigned char state,
                                struct dhcp_msg *msg, unsigned char msg_type, unsigned char option,
                                unsigned char len, struct pbuf *pbuf, unsigned short offset)
{
    metal_net_iface_t *mif = (metal_net_iface_t *)(netif ? netif->state : NULL);
    uint8_t tmp[PM_METAL_NET_TFTP_HOST_MAX];
    (void)dhcp;
    (void)state;
    (void)msg;
    (void)msg_type;
    if (mif == NULL || pbuf == NULL || len == 0) {
        return;
    }
    if (option == 66 && len < sizeof(mif->tftp)) {
        if (pbuf_copy_partial(pbuf, tmp, len, offset) == len) {
            memcpy(mif->tftp, tmp, len);
            mif->tftp[len] = '\0';
        }
    } else if (option == 67 && len < sizeof(mif->boot_file)) {
        if (pbuf_copy_partial(pbuf, tmp, len, offset) == len) {
            memcpy(mif->boot_file, tmp, len);
            mif->boot_file[len] = '\0';
        }
    } else if (option == 42 && len >= 4 && sizeof(mif->ntp) > 0) {
        uint32_t a;
        if (pbuf_copy_partial(pbuf, &a, 4, offset) == 4) {
            ip4_addr_t ntp;
            ntp.addr = a;
            store_ip4(mif->ntp, sizeof(mif->ntp), &ntp);
        }
    }
}

static int lwip_ensure(void)
{
    if (!g_metal_lwip_inited) {
        lwip_init();
        g_metal_lwip_inited = 1;
    }
    return 0;
}

static metal_net_iface_t *iface_init_one(uint32_t idx, const char *backend,
                                         int (*open_fn)(uint8_t mac[6]),
                                         const uint8_t *(*mac_fn)(void),
                                         int (*tx_fn)(const void *frame, uint32_t len),
                                         pm_metal_net_ip_l2_poll_fn poll_fn)
{
    metal_net_iface_t *mif;
    ip4_addr_t ip, nm, gw;
    uint8_t hwmac[6];

    if (idx >= METAL_NET_MAX_IFACES || backend == NULL || lwip_ensure() != 0) {
        return NULL;
    }
    mif = &g_metal_ifaces[idx];
    memset(mif, 0, sizeof(*mif));
    snprintf(mif->name, sizeof(mif->name), "eth%u", (unsigned)g_metal_eth_count);
    snprintf(mif->backend, sizeof(mif->backend), "%s", backend);
    mif->l2_open = open_fn;
    mif->l2_mac = mac_fn;
    mif->l2_tx = tx_fn;
    mif->l2_poll = poll_fn;
    mif->use_dhcp = 1;
    if (mif->l2_open == NULL || mif->l2_open(hwmac) != 0) {
        return NULL;
    }
    IP4_ADDR(&ip, 0, 0, 0, 0);
    IP4_ADDR(&nm, 0, 0, 0, 0);
    IP4_ADDR(&gw, 0, 0, 0, 0);
    if (netif_add(&mif->netif, &ip, &nm, &gw, mif, eth_netif_init, ethernet_input) == NULL) {
        return NULL;
    }
    memcpy(mif->netif.hwaddr, hwmac, ETH_HWADDR_LEN);
    netif_set_status_callback(&mif->netif, status_cb);
    netif_set_link_callback(&mif->netif, link_cb);
    netif_set_hostname(&mif->netif, "metal");
    netif_set_up(&mif->netif);
    netif_set_link_up(&mif->netif);
    if (g_metal_default_idx < 0 ||
        (g_metal_ifaces[g_metal_default_idx].used &&
         strcmp(g_metal_ifaces[g_metal_default_idx].backend, "loopback") == 0)) {
        netif_set_default(&mif->netif);
        g_metal_default_idx = (int32_t)idx;
    }
    if (dhcp_start(&mif->netif) != ERR_OK) {
        return NULL;
    }
    mif->used = 1;
    g_metal_eth_count++;
    g_metal_iface_count++;
    pm_metal_ip_bump_if_gen();
    return mif;
}

int pm_metal_net_ip_lwip_start_with_l2(const char *backend, int (*open_fn)(uint8_t mac_out[6]),
                                       const uint8_t *(*mac_fn)(void),
                                       int (*tx_fn)(const void *frame, uint32_t len),
                                       pm_metal_net_ip_l2_poll_fn poll_fn)
{
    uint32_t i;
    if (backend == NULL) {
        return -1;
    }
    for (i = 0; i < METAL_NET_MAX_IFACES; i++) {
        if (g_metal_ifaces[i].used && strcmp(g_metal_ifaces[i].backend, backend) == 0) {
            return 0;
        }
    }
    for (i = 0; i < METAL_NET_MAX_IFACES; i++) {
        if (!g_metal_ifaces[i].used) {
            break;
        }
    }
    if (i >= METAL_NET_MAX_IFACES) {
        return -1;
    }
    return iface_init_one(i, backend, open_fn, mac_fn, tx_fn, poll_fn) ? 0 : -1;
}

int pm_metal_net_ip_loopback_start(void)
{
    uint32_t i;
    metal_net_iface_t *mif;
    ip4_addr_t ip, nm, gw;
    for (i = 0; i < METAL_NET_MAX_IFACES; i++) {
        if (g_metal_ifaces[i].used && strcmp(g_metal_ifaces[i].name, "lo") == 0) {
            return 0;
        }
    }
    for (i = 0; i < METAL_NET_MAX_IFACES; i++) {
        if (!g_metal_ifaces[i].used) {
            break;
        }
    }
    if (i >= METAL_NET_MAX_IFACES || lwip_ensure() != 0) {
        return -1;
    }
    mif = &g_metal_ifaces[i];
    memset(mif, 0, sizeof(*mif));
    snprintf(mif->name, sizeof(mif->name), "lo");
    snprintf(mif->backend, sizeof(mif->backend), "loopback");
    snprintf(mif->ip, sizeof(mif->ip), "127.0.0.1");
    snprintf(mif->mask, sizeof(mif->mask), "255.0.0.0");
    IP4_ADDR(&ip, 127, 0, 0, 1);
    IP4_ADDR(&nm, 255, 0, 0, 0);
    IP4_ADDR(&gw, 127, 0, 0, 1);
    if (netif_add(&mif->netif, &ip, &nm, &gw, mif, loop_netif_init, ip_input) == NULL) {
        return -1;
    }
    netif_set_status_callback(&mif->netif, status_cb);
    netif_set_link_up(&mif->netif);
    netif_set_up(&mif->netif);
    if (g_metal_default_idx < 0) {
        netif_set_default(&mif->netif);
        g_metal_default_idx = (int32_t)i;
    }
    mif->used = 1;
    g_metal_iface_count++;
    pm_metal_ip_sync_iface(mif);
    return 0;
}

static void virtio_poll_wrap(pm_metal_net_ip_l2_rx_fn fn, void *ctx)
{
    pm_metal_dev_net_virtio_poll((pm_metal_dev_net_virtio_rx_fn)fn, ctx);
}

static void bge_poll_wrap(pm_metal_net_ip_l2_rx_fn fn, void *ctx)
{
    pm_metal_bge_netif_poll((pm_metal_bge_netif_rx_fn)fn, ctx);
}

int pm_metal_net_ip_virtio_start(void)
{
    return pm_metal_net_ip_lwip_start_with_l2("lwip+virtio-net", pm_metal_dev_net_virtio_open,
                                              pm_metal_dev_net_virtio_mac, pm_metal_dev_net_virtio_tx,
                                              virtio_poll_wrap);
}

int pm_metal_net_ip_bge_start(void)
{
    return pm_metal_net_ip_lwip_start_with_l2("lwip+bge", pm_metal_bge_netif_open,
                                              pm_metal_bge_netif_mac, pm_metal_bge_netif_tx,
                                              bge_poll_wrap);
}

int pm_metal_net_ip_if_dhcp_ready(const char *name, char *ip_out, uint32_t ip_cap)
{
    metal_net_iface_t *mif = pm_metal_ip_iface_by_name(name);
    if (mif == NULL) {
        return -1;
    }
    pm_metal_ip_sync_iface(mif);
    if (!dhcp_supplied_address(&mif->netif)) {
        return 0;
    }
    if (ip_out != NULL && ip_cap > 0) {
        snprintf(ip_out, ip_cap, "%s", mif->ip);
    }
    return 1;
}

/* ---- boot / mini-IP compat -------------------------------------------- */

int32_t pm_metal_net_ip_init(uint32_t addr_be, uint32_t mask_be, uint32_t gw_be)
{
    (void)addr_be;
    (void)mask_be;
    (void)gw_be;
    if (pm_metal_net_ip_loopback_start() != 0) {
        return -1;
    }
    return g_metal_lwip_inited ? 0 : -1;
}

int32_t pm_metal_net_ip_ready(void)
{
    return (g_metal_lwip_inited && pm_metal_ip_iface_default() != NULL) ? 1 : 0;
}

int32_t pm_metal_net_ip_set_addrs(uint32_t addr, uint32_t mask, uint32_t gw)
{
    metal_net_iface_t *mif = pm_metal_ip_iface_default();
    ip4_addr_t a, m, g;
    if (mif == NULL) {
        return -1;
    }
    a.addr = lwip_htonl(addr);
    m.addr = lwip_htonl(mask);
    g.addr = lwip_htonl(gw);
    dhcp_stop(&mif->netif);
    mif->use_dhcp = 0;
    netif_set_addr(&mif->netif, &a, &m, &g);
    pm_metal_ip_sync_iface(mif);
    pm_metal_ip_bump_if_gen();
    return 0;
}

int32_t pm_metal_net_ip_set_dns(uint32_t dns)
{
    ip_addr_t d;
    IP_ADDR4(&d, (dns >> 24) & 0xff, (dns >> 16) & 0xff, (dns >> 8) & 0xff, dns & 0xff);
    /* host-order input like mini-IP */
    {
        ip4_addr_t a4;
        a4.addr = lwip_htonl(dns);
        ip_addr_copy_from_ip4(d, a4);
    }
    dns_setserver(0, &d);
    return 0;
}

uint32_t pm_metal_net_ip_addr(void)
{
    metal_net_iface_t *mif = pm_metal_ip_iface_default();
    if (mif == NULL) {
        return 0;
    }
    return lwip_ntohl(netif_ip4_addr(&mif->netif)->addr);
}

uint32_t pm_metal_net_ip_gw(void)
{
    metal_net_iface_t *mif = pm_metal_ip_iface_default();
    if (mif == NULL) {
        return 0;
    }
    return lwip_ntohl(netif_ip4_gw(&mif->netif)->addr);
}

uint32_t pm_metal_net_ip_mask(void)
{
    metal_net_iface_t *mif = pm_metal_ip_iface_default();
    if (mif == NULL) {
        return 0;
    }
    return lwip_ntohl(netif_ip4_netmask(&mif->netif)->addr);
}

uint32_t pm_metal_net_ip_dns(void)
{
    const ip_addr_t *d = dns_getserver(0);
    if (d == NULL || !IP_IS_V4(d)) {
        return 0;
    }
    return lwip_ntohl(ip_2_ip4(d)->addr);
}

int32_t pm_metal_net_ip_announce(void)
{
    metal_net_iface_t *mif = pm_metal_ip_iface_default();
    if (mif == NULL) {
        return -1;
    }
    etharp_gratuitous(&mif->netif);
    return 0;
}

void pm_metal_net_ip_poll(void)
{
    uint32_t i;
    if (!g_metal_lwip_inited) {
        return;
    }
    for (i = 0; i < METAL_NET_MAX_IFACES; i++) {
        if (g_metal_ifaces[i].used && g_metal_ifaces[i].l2_poll != NULL) {
            g_metal_ifaces[i].l2_poll(on_frame, &g_metal_ifaces[i]);
        }
    }
#if !LWIP_NETIF_LOOPBACK_MULTITHREADING
    netif_poll_all();
#endif
    sys_check_timeouts();
    for (i = 0; i < METAL_NET_MAX_IFACES; i++) {
        if (g_metal_ifaces[i].used) {
            pm_metal_ip_sync_iface(&g_metal_ifaces[i]);
        }
    }
    /* Prefer eth* as default once it has an address. */
    {
        metal_net_iface_t *eth = pm_metal_ip_iface_by_name("eth0");
        if (eth != NULL && eth->used && !ip4_addr_isany_val(*netif_ip4_addr(&eth->netif))) {
            netif_set_default(&eth->netif);
            g_metal_default_idx = (int32_t)(eth - g_metal_ifaces);
        }
    }
    pm_metal_ip_sock_wake_poll();
}

static u8_t ping_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr)
{
    (void)arg;
    (void)pcb;
    (void)addr;
    if (p != NULL && p->tot_len >= 8) {
        g_ping_replies++;
    }
    return 0; /* let stack free / continue */
}

int32_t pm_metal_net_ip_ping(uint32_t dst_ip, uint16_t id, uint16_t seq)
{
    static struct raw_pcb *pcb;
    struct pbuf *p;
    ip_addr_t dst;
    ip4_addr_t a4;
    uint8_t echo[8];
    (void)id;
    (void)seq;
    a4.addr = lwip_htonl(dst_ip);
    ip_addr_copy_from_ip4(dst, a4);
    if (pcb == NULL) {
        pcb = raw_new(IP_PROTO_ICMP);
        if (pcb == NULL) {
            return -1;
        }
        raw_recv(pcb, ping_recv, NULL);
    }
    g_ping_id = id;
    g_ping_seq = seq;
    echo[0] = ICMP_ECHO;
    echo[1] = 0;
    echo[2] = 0;
    echo[3] = 0;
    echo[4] = (uint8_t)(id >> 8);
    echo[5] = (uint8_t)id;
    echo[6] = (uint8_t)(seq >> 8);
    echo[7] = (uint8_t)seq;
    p = pbuf_alloc(PBUF_IP, sizeof(echo), PBUF_RAM);
    if (p == NULL) {
        return -1;
    }
    memcpy(p->payload, echo, sizeof(echo));
    {
        uint16_t chk = inet_chksum(p->payload, sizeof(echo));
        ((uint8_t *)p->payload)[2] = (uint8_t)(chk >> 8);
        ((uint8_t *)p->payload)[3] = (uint8_t)chk;
    }
    if (raw_sendto(pcb, p, &dst) != ERR_OK) {
        pbuf_free(p);
        return -1;
    }
    pbuf_free(p);
    return 0;
}

uint32_t pm_metal_net_ip_ping_replies(void)
{
    return g_ping_replies;
}

/* Compat for dns/ntp/tftp ARP wait loops. >0 resolved, 0 pending, <0 error. */
int32_t pm_metal_net_ip_arp_resolve(uint32_t ip_host)
{
    metal_net_iface_t *mif = pm_metal_ip_iface_default();
    ip4_addr_t ip;
    struct eth_addr *eth;
    const ip4_addr_t *ipaddr;
    if (mif == NULL) {
        return -1;
    }
    ip.addr = lwip_htonl(ip_host);
    if (etharp_find_addr(&mif->netif, &ip, &eth, &ipaddr) >= 0) {
        return 1;
    }
    (void)etharp_query(&mif->netif, &ip, NULL);
    return 0;
}

/* ---- if-mgmt ---------------------------------------------------------- */

unsigned pm_metal_net_ip_if_count(void)
{
    return (unsigned)g_metal_iface_count;
}

uint32_t pm_metal_net_ip_if_gen(void)
{
    return g_metal_if_gen;
}

uint32_t pm_metal_net_ip_if_wait(uint32_t since_gen)
{
    uint32_t h;
    if (g_metal_if_gen != since_gen) {
        return pm_metal_async_completed_u32(g_metal_if_gen);
    }
    h = pm_metal_async_park();
    if (h == 0u) {
        return 0;
    }
    g_metal_if_wait_h = h;
    g_metal_if_wait_since = since_gen;
    return h;
}

static void fill_ifcfg(metal_net_iface_t *mif, pm_metal_net_ip_ifcfg_t *out)
{
    const uint8_t *mac;
    if (mif == NULL || out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    snprintf(out->name, sizeof(out->name), "%s", mif->name);
    pm_metal_ip_sync_iface(mif);
    snprintf(out->ip, sizeof(out->ip), "%s", mif->ip);
    snprintf(out->mask, sizeof(out->mask), "%s", mif->mask);
    snprintf(out->gw, sizeof(out->gw), "%s", mif->gw);
    snprintf(out->dns, sizeof(out->dns), "%s", mif->dns);
    snprintf(out->ntp, sizeof(out->ntp), "%s", mif->ntp);
    snprintf(out->tftp, sizeof(out->tftp), "%s", mif->tftp);
    snprintf(out->boot_file, sizeof(out->boot_file), "%s", mif->boot_file);
    out->link_up = netif_is_link_up(&mif->netif) ? 1 : 0;
    out->backend = mif->backend;
    mac = (mif->l2_mac != NULL) ? mif->l2_mac() : mif->netif.hwaddr;
    if (mac != NULL) {
        memcpy(out->mac, mac, 6);
    }
}

static int format_status_line(const pm_metal_net_ip_ifcfg_t *cfg, char *buf, uint32_t buf_len)
{
    if (cfg == NULL || buf == NULL || buf_len == 0) {
        return -1;
    }
    if (cfg->backend != NULL && strcmp(cfg->backend, "wireguard") == 0) {
        snprintf(buf, buf_len, "%s %s/%s %s wireguard", cfg->name, cfg->ip, cfg->mask,
                 cfg->link_up ? "up" : "down");
        return 0;
    }
    snprintf(buf, buf_len, "%s %s/%s gw %s dns %s %s %s", cfg->name, cfg->ip, cfg->mask, cfg->gw,
             cfg->dns, cfg->link_up ? "up" : "down", cfg->backend ? cfg->backend : "");
    return 0;
}

int pm_metal_net_ip_if_get_index(unsigned index, pm_metal_net_ip_ifcfg_t *out)
{
    uint32_t i, n = 0;
    for (i = 0; i < METAL_NET_MAX_IFACES; i++) {
        if (!g_metal_ifaces[i].used) {
            continue;
        }
        if (n == index) {
            fill_ifcfg(&g_metal_ifaces[i], out);
            return 0;
        }
        n++;
    }
    return -1;
}

int32_t pm_metal_net_ip_if_status_index(uint32_t index, char *buf, uint32_t buf_len)
{
    pm_metal_net_ip_ifcfg_t cfg;
    if (pm_metal_net_ip_if_get_index(index, &cfg) != 0) {
        return -1;
    }
    return format_status_line(&cfg, buf, buf_len);
}

int pm_metal_net_ip_if_get_named(const char *name, pm_metal_net_ip_ifcfg_t *out)
{
    metal_net_iface_t *mif = pm_metal_ip_iface_by_name(name);
    if (mif == NULL) {
        return -1;
    }
    fill_ifcfg(mif, out);
    return 0;
}

int pm_metal_net_ip_if_get(pm_metal_net_ip_ifcfg_t *out)
{
    return pm_metal_net_ip_if_get_named(NULL, out);
}

int pm_metal_net_ip_if_set_named(const char *name, const char *ip, const char *mask, const char *gw,
                                 const char *dns)
{
    metal_net_iface_t *mif = pm_metal_ip_iface_by_name(name);
    ip4_addr_t a, m, g;
    if (mif == NULL || pm_metal_ip_parse_ipv4(ip, &a) != 0 || pm_metal_ip_parse_ipv4(mask, &m) != 0 ||
        pm_metal_ip_parse_ipv4(gw, &g) != 0) {
        return -1;
    }
    dhcp_stop(&mif->netif);
    mif->use_dhcp = 0;
    netif_set_addr(&mif->netif, &a, &m, &g);
    if (dns != NULL) {
        ip4_addr_t d4;
        ip_addr_t d;
        if (pm_metal_ip_parse_ipv4(dns, &d4) == 0) {
            ip_addr_copy_from_ip4(d, d4);
            dns_setserver(0, &d);
        }
    }
    pm_metal_ip_sync_iface(mif);
    pm_metal_ip_bump_if_gen();
    return 0;
}

int pm_metal_net_ip_if_set(const char *ip, const char *mask, const char *gw, const char *dns)
{
    return pm_metal_net_ip_if_set_named(NULL, ip, mask, gw, dns);
}

int pm_metal_net_ip_if_set_dhcp_named(const char *name)
{
    metal_net_iface_t *mif = pm_metal_ip_iface_by_name(name);
    ip4_addr_t z;
    if (mif == NULL) {
        return -1;
    }
    IP4_ADDR(&z, 0, 0, 0, 0);
    netif_set_addr(&mif->netif, &z, &z, &z);
    mif->use_dhcp = 1;
    return (dhcp_start(&mif->netif) == ERR_OK) ? 0 : -1;
}

int pm_metal_net_ip_if_set_dhcp(void)
{
    return pm_metal_net_ip_if_set_dhcp_named(NULL);
}

int pm_metal_net_ip_if_status_named(const char *name, char *buf, uint32_t buf_len)
{
    pm_metal_net_ip_ifcfg_t cfg;
    if (pm_metal_net_ip_if_get_named(name, &cfg) != 0) {
        return -1;
    }
    return format_status_line(&cfg, buf, buf_len);
}

int pm_metal_net_ip_if_status(char *buf, uint32_t buf_len)
{
    uint32_t i, n = 0, off = 0;
    char line[160];
    if (buf == NULL || buf_len == 0) {
        return -1;
    }
    buf[0] = '\0';
    for (i = 0; i < METAL_NET_MAX_IFACES; i++) {
        pm_metal_net_ip_ifcfg_t cfg;
        if (!g_metal_ifaces[i].used) {
            continue;
        }
        fill_ifcfg(&g_metal_ifaces[i], &cfg);
        format_status_line(&cfg, line, sizeof(line));
        if (n > 0 && off + 1 < buf_len) {
            buf[off++] = '\n';
            buf[off] = '\0';
        }
        snprintf(buf + off, buf_len - off, "%s", line);
        off = (uint32_t)strlen(buf);
        n++;
    }
    return 0;
}

int pm_metal_net_ip_if_boot_get(const char *name, char *tftp_host, uint32_t tftp_cap,
                                char *boot_file, uint32_t boot_cap)
{
    metal_net_iface_t *mif = pm_metal_ip_iface_by_name(name);
    if (mif == NULL) {
        return -1;
    }
    if (tftp_host != NULL && tftp_cap > 0) {
        snprintf(tftp_host, tftp_cap, "%s", mif->tftp);
    }
    if (boot_file != NULL && boot_cap > 0) {
        snprintf(boot_file, boot_cap, "%s", mif->boot_file);
    }
    return 0;
}

int pm_metal_net_ip_resolve_ip4(const char *host, uint32_t *out_host)
{
    ip_addr_t a;
    if (out_host == NULL || pm_metal_ip_parse_host(host, &a) != 0) {
        return -1;
    }
    *out_host = lwip_ntohl(ip_2_ip4(&a)->addr);
    return 0;
}

int pm_metal_net_ip_dns_last_ntoa(char *out, uint32_t out_cap)
{
    if (out == NULL || out_cap == 0 || g_metal_dns_last[0] == '\0') {
        return -1;
    }
    snprintf(out, out_cap, "%s", g_metal_dns_last);
    return 0;
}
