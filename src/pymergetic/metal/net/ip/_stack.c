/** @file
  lwIP NO_SYS bridge — DHCPv4 over pluggable L2 (exp2 minimal slice).
**/
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "_cfg.h"
#include <pymergetic/metal/net/ip/__init__.h>

#include "lwipopts.h" /* IWYU pragma: keep */
#include <lwip/init.h>
#include <lwip/netif.h>
#include <lwip/ip.h>
#include <lwip/timeouts.h>
#include <lwip/ip4_addr.h>
#include <lwip/pbuf.h>
#include <lwip/etharp.h>
#include <lwip/dhcp.h>
#include <netif/ethernet.h>

typedef struct {
  int32_t      used;
  struct netif netif;
  char         name[PM_METAL_NET_IP_IFNAME_MAX];
  char         backend[24];
  int (*l2_open)(uint8_t mac[6]);
  const uint8_t *(*l2_mac)(void);
  int (*l2_tx)(const void *frame, uint32_t len);
  pm_metal_net_ip_l2_poll_fn l2_poll;
  char             ip[16];
  char             mask[16];
  char             gw[16];
  int32_t          use_dhcp;
} metal_net_iface_t;

static metal_net_iface_t mIfaces[PM_METAL_NET_IP_MAX_IFS];
static uint32_t          mIfaceCount;
static uint32_t          mEthCount;
static int32_t           mDefaultIdx = -1;
static int32_t           mLwipInited;
static uint8_t           mTxScratch[1514];

static const char *pm_metal_host_name_cstr(void)
{
  return "metal";
}

static void StoreIp4Ascii(char *dst, size_t dst_len, const ip4_addr_t *addr)
{
  if (dst == NULL || dst_len == 0 || addr == NULL) {
    return;
  }

  ip4addr_ntoa_r(addr, dst, (int)dst_len);
}

static void SyncIfaceCfg(metal_net_iface_t *mif)
{
  const ip4_addr_t *ip;
  const ip4_addr_t *nm;
  const ip4_addr_t *gw;

  if (mif == NULL || !mif->used) {
    return;
  }

#if LWIP_DHCP
  if (mif->use_dhcp && !dhcp_supplied_address(&mif->netif)) {
    snprintf(mif->ip, sizeof(mif->ip), "%s", "0.0.0.0");
    snprintf(mif->mask, sizeof(mif->mask), "%s", "0.0.0.0");
    snprintf(mif->gw, sizeof(mif->gw), "%s", "0.0.0.0");
    return;
  }
#endif

  ip = netif_ip4_addr(&mif->netif);
  nm = netif_ip4_netmask(&mif->netif);
  gw = netif_ip4_gw(&mif->netif);
  StoreIp4Ascii(mif->ip, sizeof(mif->ip), ip);
  StoreIp4Ascii(mif->mask, sizeof(mif->mask), nm);
  StoreIp4Ascii(mif->gw, sizeof(mif->gw), gw);
}

static err_t MetalLinkOutput(struct netif *netif, struct pbuf *p)
{
  uint32_t     tot;
  uint32_t     off;
  struct pbuf *q;
  metal_net_iface_t *mif;

  (void)netif;
  if (p == NULL) {
    return ERR_ARG;
  }

  tot = p->tot_len;
  if (tot == 0 || tot > sizeof(mTxScratch)) {
    return ERR_BUF;
  }

  off = 0;
  for (q = p; q != NULL; q = q->next) {
    memcpy(mTxScratch + off, q->payload, q->len);
    off += q->len;
  }

  mif = (metal_net_iface_t *)netif->state;
  if (mif == NULL || mif->l2_tx == NULL || mif->l2_tx(mTxScratch, tot) != 0) {
    return ERR_IF;
  }

  return ERR_OK;
}

static err_t MetalNetifInit(struct netif *netif)
{
  metal_net_iface_t *mif;
  const uint8_t     *mac;

  mif = (metal_net_iface_t *)netif->state;
  if (mif == NULL) {
    return ERR_IF;
  }

  if (mif->l2_mac == NULL) {
    return ERR_IF;
  }
  mac = mif->l2_mac();
  if (mac == NULL) {
    return ERR_IF;
  }
  netif->hwaddr_len = ETH_HWADDR_LEN;
  memcpy(netif->hwaddr, mac, ETH_HWADDR_LEN);
  netif->mtu = 1500;
  netif->flags =
    (uint8_t)(NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_LINK_UP);
  netif->output     = etharp_output;
  netif->linkoutput = MetalLinkOutput;
#if LWIP_NETIF_HOSTNAME
  netif_set_hostname(netif, pm_metal_host_name_cstr());
#endif
  return ERR_OK;
}

static err_t MetalLoopOutputIpv4(struct netif *netif, struct pbuf *p, const ip4_addr_t *addr)
{
  (void)addr;
  return netif_loop_output(netif, p);
}

static err_t MetalLoopNetifInit(struct netif *netif)
{
  if (netif == NULL) {
    return ERR_IF;
  }

  netif->name[0]    = 'l';
  netif->name[1]    = 'o';
  netif->mtu        = 65535;
  netif->flags      = (uint8_t)(NETIF_FLAG_LINK_UP);
  memset(netif->hwaddr, 0, sizeof(netif->hwaddr));
  netif->hwaddr_len = ETH_HWADDR_LEN;
  netif->output     = MetalLoopOutputIpv4;
  netif->linkoutput = NULL;
  return ERR_OK;
}

static void MetalOnFrame(void *ctx, const uint8_t *frame, uint32_t len)
{
  metal_net_iface_t *mif;
  struct pbuf       *p;

  mif = (metal_net_iface_t *)ctx;
  if (mif == NULL || frame == NULL || len == 0) {
    return;
  }

  p = pbuf_alloc(PBUF_RAW, (uint16_t)len, PBUF_POOL);
  if (p == NULL) {
    return;
  }

  if (pbuf_take(p, frame, (uint16_t)len) != ERR_OK) {
    pbuf_free(p);
    return;
  }

  if (mif->netif.input(p, &mif->netif) != ERR_OK) {
    pbuf_free(p);
  }
}

static int32_t LwipEnsure(void)
{
  if (!mLwipInited) {
    lwip_init();
    mLwipInited = 1;
  }

  return 0;
}

static metal_net_iface_t *IfaceByName(const char *name)
{
  uint32_t i;

  if (name == NULL) {
    name = "eth0";
  }

  for (i = 0; i < PM_METAL_NET_IP_MAX_IFS; i++) {
    if (mIfaces[i].used && strcmp(mIfaces[i].name, name) == 0) {
      return &mIfaces[i];
    }
  }

  return NULL;
}

int pm_metal_net_ip_l2_start(const char *backend, const pm_metal_net_ip_l2_ops_t *ops)
{
  metal_net_iface_t *mif;
  ip4_addr_t         ip;
  ip4_addr_t         nm;
  ip4_addr_t         gw;
  uint8_t            hwmac[6];
  uint32_t           idx;

  if (backend == NULL || ops == NULL || ops->open == NULL || ops->mac == NULL ||
      ops->tx == NULL || ops->poll == NULL) {
    return -1;
  }

  if (LwipEnsure() != 0) {
    return -1;
  }

  for (idx = 0; idx < PM_METAL_NET_IP_MAX_IFS; idx++) {
    if (!mIfaces[idx].used) {
      break;
    }
    if (strcmp(mIfaces[idx].backend, backend) == 0) {
      return 0;
    }
  }

  if (idx >= PM_METAL_NET_IP_MAX_IFS) {
    return -1;
  }

  mif = &mIfaces[idx];
  memset(mif, 0, sizeof(*mif));
  snprintf(mif->name, sizeof(mif->name), "eth%u", mEthCount);
  snprintf(mif->backend, sizeof(mif->backend), "%s", backend);
  mif->l2_open  = ops->open;
  mif->l2_mac   = ops->mac;
  mif->l2_tx    = ops->tx;
  mif->l2_poll  = ops->poll;
  mif->use_dhcp = 1;

  if (mif->l2_open(hwmac) != 0) {
    return -1;
  }

  IP4_ADDR(&ip, 0, 0, 0, 0);
  IP4_ADDR(&nm, 0, 0, 0, 0);
  IP4_ADDR(&gw, 0, 0, 0, 0);
  snprintf(mif->ip, sizeof(mif->ip), "%s", "0.0.0.0");

  if (netif_add(&mif->netif, &ip, &nm, &gw, mif, MetalNetifInit, ethernet_input) == NULL) {
    return -1;
  }

  memcpy(mif->netif.hwaddr, hwmac, ETH_HWADDR_LEN);
  netif_set_up(&mif->netif);
#if LWIP_NETIF_HOSTNAME
  netif_set_hostname(&mif->netif, pm_metal_host_name_cstr());
#endif

  if (mDefaultIdx < 0 ||
      (mIfaces[mDefaultIdx].used && strcmp(mIfaces[mDefaultIdx].backend, "loopback") == 0)) {
    netif_set_default(&mif->netif);
    mDefaultIdx = (int32_t)idx;
  }

#if LWIP_DHCP
  if (dhcp_start(&mif->netif) != ERR_OK) {
    return -1;
  }
#endif

  mif->used = 1;
  mEthCount++;
  mIfaceCount++;
  return 0;
}

int pm_metal_net_ip_loopback_start(void)
{
  metal_net_iface_t *mif;
  ip4_addr_t         ip;
  ip4_addr_t         nm;
  ip4_addr_t         gw;
  uint32_t           idx;

  for (idx = 0; idx < PM_METAL_NET_IP_MAX_IFS; idx++) {
    if (mIfaces[idx].used && strcmp(mIfaces[idx].name, "lo") == 0) {
      return 0;
    }
  }

  for (idx = 0; idx < PM_METAL_NET_IP_MAX_IFS; idx++) {
    if (!mIfaces[idx].used) {
      break;
    }
  }

  if (idx >= PM_METAL_NET_IP_MAX_IFS) {
    return -1;
  }

  if (LwipEnsure() != 0) {
    return -1;
  }

  mif = &mIfaces[idx];
  memset(mif, 0, sizeof(*mif));
  snprintf(mif->name, sizeof(mif->name), "%s", "lo");
  snprintf(mif->backend, sizeof(mif->backend), "%s", "loopback");
  mif->use_dhcp = 0;

  IP4_ADDR(&ip, 127, 0, 0, 1);
  IP4_ADDR(&nm, 255, 0, 0, 0);
  IP4_ADDR(&gw, 127, 0, 0, 1);

  if (netif_add(&mif->netif, &ip, &nm, &gw, mif, MetalLoopNetifInit, ip_input) == NULL) {
    return -1;
  }

  netif_set_link_up(&mif->netif);
  netif_set_up(&mif->netif);

  if (mDefaultIdx < 0) {
    netif_set_default(&mif->netif);
    mDefaultIdx = (int32_t)idx;
  }

  StoreIp4Ascii(mif->ip, sizeof(mif->ip), &ip);
  StoreIp4Ascii(mif->mask, sizeof(mif->mask), &nm);
  StoreIp4Ascii(mif->gw, sizeof(mif->gw), &gw);
  mif->used = 1;
  mIfaceCount++;
  return 0;
}

void pm_metal_net_ip_poll(void)
{
  uint32_t i;

  if (!mLwipInited) {
    return;
  }

  for (i = 0; i < PM_METAL_NET_IP_MAX_IFS; i++) {
    if (!mIfaces[i].used || mIfaces[i].l2_poll == NULL) {
      continue;
    }

    mIfaces[i].l2_poll(MetalOnFrame, &mIfaces[i]);
  }

#if !LWIP_NETIF_LOOPBACK_MULTITHREADING
  netif_poll_all();
#endif
  sys_check_timeouts();

  for (i = 0; i < PM_METAL_NET_IP_MAX_IFS; i++) {
    if (mIfaces[i].used) {
      SyncIfaceCfg(&mIfaces[i]);
    }
  }
}

uint32_t pm_metal_net_ip_if_count(void)
{
  return mIfaceCount;
}

int32_t pm_metal_net_ip_if_status_index(uint32_t index, char *dest, uint32_t dest_cap)
{
  metal_net_iface_t *mif;
  uint32_t           n;
  uint32_t           i;

  if (dest == NULL || dest_cap == 0 || index >= mIfaceCount) {
    return -1;
  }

  n   = 0;
  mif = NULL;
  for (i = 0; i < PM_METAL_NET_IP_MAX_IFS; i++) {
    if (!mIfaces[i].used) {
      continue;
    }
    if (n == index) {
      mif = &mIfaces[i];
      break;
    }
    n++;
  }

  if (mif == NULL) {
    return -1;
  }

  SyncIfaceCfg(mif);
  return snprintf(dest,
                    dest_cap,
                    "%s %s/%s gw %s %s mac %02x:%02x:%02x:%02x:%02x:%02x",
                    mif->name,
                    mif->ip,
                    mif->mask,
                    mif->gw,
                    (netif_is_link_up(&mif->netif) && netif_is_up(&mif->netif)) ? "up" : "down",
                    mif->netif.hwaddr[0],
                    mif->netif.hwaddr[1],
                    mif->netif.hwaddr[2],
                    mif->netif.hwaddr[3],
                    mif->netif.hwaddr[4],
                    mif->netif.hwaddr[5]);
}

int pm_metal_net_ip_if_dhcp_ready(const char *ifname, char *ip_out, uint32_t ip_cap)
{
  metal_net_iface_t *mif;

  mif = IfaceByName(ifname);
  if (mif == NULL || !mif->used || !mif->use_dhcp) {
    return -1;
  }

  SyncIfaceCfg(mif);
#if LWIP_DHCP
  if (!dhcp_supplied_address(&mif->netif)) {
    return 0;
  }
  if (ip_out != NULL && ip_cap > 0) {
    snprintf(ip_out, ip_cap, "%s", mif->ip);
  }
  return 1;
#else
  (void)ip_out;
  (void)ip_cap;
  return -1;
#endif
}
