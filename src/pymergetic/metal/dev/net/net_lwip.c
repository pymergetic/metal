/** @file
  lwIP NO_SYS bridge — DHCPv4 + stateless DHCPv6 on virtio/bge L2.
  (impl: efi|bios)
**/
#include <pymergetic/metal/dev/net/net_ops.h>
#include <pymergetic/metal/bus/virtio/virtio.h>
#include <pymergetic/metal/dev/net/net_cfg.h>
#include <pymergetic/metal/dev/net/metal_dhcp6_stateful.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/bus/io/io.h>
#include <pymergetic/metal/fs/esp/esp.h>
#include <pymergetic/metal/fs/fs.h>
#include <pymergetic/metal/util/ip.h>
#include <pymergetic/metal/host/host.h>
#include <runtime/coro/coro.h>
#include <runtime/time/time.h>
#include <runtime/mem/mem.h>

#include "virtio_netif.h"
#include "bge/bge_netif.h"

#include "lwipopts.h" /* IWYU pragma: keep */
#include <lwip/init.h>
#include <lwip/netif.h>
#include <lwip/ip.h>
#include <lwip/timeouts.h>
#include <lwip/ip4_addr.h>
#include <lwip/ip6_addr.h>
#include <lwip/dns.h>
#include <lwip/tcp.h>
#include <lwip/udp.h>
#include <lwip/pbuf.h>
#include <lwip/etharp.h>
#include <lwip/dhcp.h>
#include <lwip/dhcp6.h>
#include <lwip/ethip6.h>
#include <netif/ethernet.h>
#include <lwip/prot/dhcp.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>

typedef void (*pm_metal_net_l2_rx_fn)(void *ctx, const uint8_t *frame,
				      uint32_t len);
typedef void (*pm_metal_net_l2_poll_fn)(pm_metal_net_l2_rx_fn fn, void *ctx);

#define METAL_NET_MAX_IFACES  PM_METAL_NET_MAX_IFS
#define METAL_NET_MAX_SOCKS   16u
#define METAL_NET_TX_MAX      1514u
#define METAL_HOSTS_MAX       64u
#define METAL_HOSTS_NAME_MAX  64u
#define METAL_HOSTS_FILE_MAX  4096u
#define METAL_HOSTS_PATH      "/etc/hosts"

typedef struct {
  INT32                      used;
  struct netif               netif;
  CHAR8                      name[PM_METAL_NET_IFNAME_MAX];
  CHAR8                      backend[24];
  int                      (*l2_open)(uint8_t mac[6]);
  const uint8_t *          (*l2_mac)(void);
  int                      (*l2_tx)(const void *frame, uint32_t len);
  pm_metal_net_l2_poll_fn    l2_poll;
  CHAR8                      ip[16];
  CHAR8                      mask[16];
  CHAR8                      gw[16];
  CHAR8                      dns[16];
  CHAR8                      tftp[PM_METAL_NET_TFTP_HOST_MAX];
  CHAR8                      boot_file[PM_METAL_NET_BOOT_FILE_MAX];
  INT32                      use_dhcp;
  INT32                      dhcp6_mode;
  metal_dhcp6_stateful_t     dhcp6_sf;
} metal_net_iface_t;

typedef struct {
  INT32   set_dhcp;
  INT32   set_dhcp6;
  INT32   set_ip;
  CHAR8   ip[16];
  CHAR8   mask[16];
  CHAR8   gw[16];
  CHAR8   dns[16];
} metal_net_conf_ov_t;

typedef struct {
  INT32      used;
  CHAR8      name[METAL_HOSTS_NAME_MAX];
  ip_addr_t  addr;
} metal_hosts_ent_t;

typedef enum {
  WAIT_CONNECT = 0,
  WAIT_RECV,
  WAIT_ACCEPT,
  WAIT_DNS
} wait_kind_t;

typedef struct {
  INT32             used;
  UINT32            domain;
  UINT32            type;
  struct tcp_pcb   *tcp;
  struct udp_pcb   *udp;
  ip_addr_t         remote;
  UINT16            remote_port;
  INT32             have_remote;
  INT32             conn_done;
  INT32             conn_ok;
  INT32             listening;
  struct tcp_pcb   *accept_pcb;
  VOID             *recv_buf;
  UINT32            recv_cap;
  UINT32            recv_got;
  INT32             recv_done;
  INT32             recv_err;
  struct pbuf      *rx_q;
  INT32             bound_if;
} msock_t;

typedef struct {
  pm_metal_coro_t       coro;
  wait_kind_t           kind;
  pm_metal_net_sock_h   h;
  pm_metal_net_sock_h   accept_out;
  UINT64                deadline;
  INT32                 dns_done;
  INT32                 dns_ok;
  ip_addr_t             dns_addr;
} net_wait_t;

STATIC pm_metal_net_sock_h  PromoteAcceptPcb (msock_t *s);
STATIC metal_net_iface_t   *IfaceInitOne (UINT32 idx, CONST CHAR8 *backend,
                                          int (*open_fn)(uint8_t mac[6]),
                                          const uint8_t *(*mac_fn)(void),
                                          int (*tx_fn)(const void *frame,
                                                       uint32_t len),
                                          pm_metal_net_l2_poll_fn poll_fn);
STATIC metal_net_iface_t   *IfaceByName (CONST CHAR8 *name);
STATIC metal_net_iface_t   *IfaceDefault (VOID);
STATIC VOID                 SyncIfaceCfg (metal_net_iface_t *mif);
STATIC INT32                ParseIfIndex (CONST CHAR8 *name);

STATIC metal_net_iface_t    mIfaces[METAL_NET_MAX_IFACES];
STATIC UINT32               mIfaceCount;
STATIC UINT32               mEthCount;
STATIC INT32                mDefaultIdx = -1;
STATIC INT32                mLwipInited;
STATIC INT32                mOpsRegistered;
STATIC INT32                mConfLoaded;
STATIC INT32                mHostsLoaded;
STATIC UINT32               mHostsCount;
STATIC metal_hosts_ent_t    mHosts[METAL_HOSTS_MAX];
STATIC msock_t              mSocks[METAL_NET_MAX_SOCKS + 1];
STATIC UINT8                mTxScratch[METAL_NET_TX_MAX];
STATIC metal_net_conf_ov_t  mGlobalConf;
STATIC metal_net_conf_ov_t  mIfConf[METAL_NET_MAX_IFACES];
STATIC net_wait_t          *mDnsWait;
STATIC ip_addr_t            mLastDnsAddr;
STATIC INT32                mLastDnsValid;

#define METAL_NET_READY()  (mIfaceCount > 0)

STATIC
INT32
ParseIpv4 (
  CONST CHAR8  *s,
  ip4_addr_t   *out
  )
{
  UINT32  host;

  if (s == NULL || out == NULL) {
    return -1;
  }

  if (pm_metal_util_ip4_parse (s, &host) != 0) {
    return -1;
  }

  IP4_ADDR (
    out,
    (host >> 24) & 0xffu,
    (host >> 16) & 0xffu,
    (host >> 8) & 0xffu,
    host & 0xffu
    );
  return 0;
}

STATIC
INT32
ParseIpv6 (
  CONST CHAR8  *s,
  ip6_addr_t   *out
  )
{
  if (s == NULL || out == NULL) {
    return -1;
  }

  return ip6addr_aton (s, out) ? 0 : -1;
}

STATIC
INT32
HostNameEq (
  CONST CHAR8  *a,
  CONST CHAR8  *b
  )
{
  CHAR8  ca;
  CHAR8  cb;

  if (a == NULL || b == NULL) {
    return 0;
  }

  while (*a != '\0' && *b != '\0') {
    ca = *a;
    cb = *b;
    if (ca >= 'A' && ca <= 'Z') {
      ca = (CHAR8)(ca - 'A' + 'a');
    }

    if (cb >= 'A' && cb <= 'Z') {
      cb = (CHAR8)(cb - 'A' + 'a');
    }

    if (ca != cb) {
      return 0;
    }

    a++;
    b++;
  }

  return (*a == '\0' && *b == '\0') ? 1 : 0;
}

STATIC
INT32
HostsAdd (
  CONST CHAR8      *name,
  CONST ip_addr_t  *addr
  )
{
  UINT32  i;

  if (name == NULL || name[0] == '\0' || addr == NULL
      || mHostsCount >= METAL_HOSTS_MAX)
  {
    return -1;
  }

  for (i = 0; name[i] != '\0'; i++) {
    if (i + 1u >= METAL_HOSTS_NAME_MAX) {
      return -1;
    }
  }

  for (i = 0; i < mHostsCount; i++) {
    if (mHosts[i].used && HostNameEq (mHosts[i].name, name)) {
      mHosts[i].addr = *addr;
      return 0;
    }
  }

  ZeroMem (&mHosts[mHostsCount], sizeof (mHosts[0]));
  AsciiStrCpyS (
    mHosts[mHostsCount].name,
    sizeof (mHosts[mHostsCount].name),
    name
    );
  mHosts[mHostsCount].addr = *addr;
  mHosts[mHostsCount].used = 1;
  mHostsCount++;
  return 0;
}

STATIC
VOID
ParseHostsFile (
  CONST UINT8  *data,
  UINT32        len
  )
{
  UINT32  i;
  UINT32  line_start;

  if (data == NULL || len == 0) {
    return;
  }

  line_start = 0;
  for (i = 0; i <= len; i++) {
    INT32  end;

    end = (i == len) || (data[i] == '\n') || (data[i] == '\r');
    if (!end) {
      continue;
    }

    {
      CHAR8       line[160];
      UINT32      li;
      UINT32      p;
      CHAR8       tok[METAL_HOSTS_NAME_MAX];
      UINT32      ti;
      ip_addr_t   addr;
      INT32       have_addr;

      li = 0;
      while (line_start < i && li + 1u < sizeof (line)) {
        line[li++] = (CHAR8)data[line_start++];
      }

      line[li] = '\0';
      while (i < len && (data[i] == '\n' || data[i] == '\r')) {
        i++;
      }

      line_start = i;
      if (line[0] == '\0' || line[0] == '#') {
        continue;
      }

      p         = 0;
      have_addr = 0;
      while (line[p] != '\0') {
        while (line[p] == ' ' || line[p] == '\t') {
          p++;
        }

        if (line[p] == '\0' || line[p] == '#') {
          break;
        }

        ti = 0;
        while (line[p] != '\0' && line[p] != ' ' && line[p] != '\t'
               && line[p] != '#' && ti + 1u < sizeof (tok))
        {
          tok[ti++] = line[p++];
        }

        tok[ti] = '\0';
        if (tok[0] == '\0') {
          break;
        }

        if (!have_addr) {
          ip4_addr_t  a4;
          ip6_addr_t  a6;

          if (pm_metal_util_ip4_is_literal (tok) && ParseIpv4 (tok, &a4) == 0) {
            ip_addr_copy_from_ip4 (addr, a4);
            have_addr = 1;
          } else if (ParseIpv6 (tok, &a6) == 0) {
            ip_addr_copy_from_ip6 (addr, a6);
            have_addr = 1;
          } else {
            break;
          }
        } else {
          (VOID)HostsAdd (tok, &addr);
        }
      }
    }
  }
}

STATIC
INT32
LookupHosts (
  CONST CHAR8  *host,
  ip_addr_t    *out
  )
{
  UINT32  i;

  if (host == NULL || out == NULL) {
    return -1;
  }

  for (i = 0; i < mHostsCount; i++) {
    if (mHosts[i].used && HostNameEq (mHosts[i].name, host)) {
      *out = mHosts[i].addr;
      return 0;
    }
  }

  return -1;
}

STATIC
VOID
TryLoadHosts (
  VOID
  )
{
  UINT32  sz;
  UINT32  n;
  UINT8   buf[METAL_HOSTS_FILE_MAX];

  if (mHostsLoaded) {
    return;
  }

  mHostsLoaded = 1;
  mHostsCount  = 0;
  ZeroMem (mHosts, sizeof (mHosts));

  sz = pm_metal_fs_size (METAL_HOSTS_PATH);
  if (sz == 0 || sz > METAL_HOSTS_FILE_MAX) {
    return;
  }

  n = pm_metal_fs_read (METAL_HOSTS_PATH, buf, sz);
  if (n > 0) {
    ParseHostsFile (buf, n);
  }
}

STATIC
INT32
ParseHostAddr (
  CONST CHAR8  *host,
  ip_addr_t    *out
  )
{
  ip4_addr_t  a4;
  ip6_addr_t  a6;
  CHAR8       local[PM_METAL_HOST_NAME_MAX];

  if (host == NULL || out == NULL) {
    return -1;
  }

  /* Literals — sync util/ip (and lwIP IPv6); skip DNS. */
  if (pm_metal_util_ip4_is_literal (host) && ParseIpv4 (host, &a4) == 0) {
    ip_addr_copy_from_ip4 (*out, a4);
    return 0;
  }

  if (ParseIpv6 (host, &a6) == 0) {
    ip_addr_copy_from_ip6 (*out, a6);
    return 0;
  }

  /* Local identity + well-known loopback names → lo, not remote DNS. */
  if (HostNameEq (host, "localhost")
      || (pm_metal_host_name_get (local, sizeof (local)) == 0
          && HostNameEq (host, local)))
  {
    IP4_ADDR (&a4, 127, 0, 0, 1);
    ip_addr_copy_from_ip4 (*out, a4);
    return 0;
  }

  if (HostNameEq (host, "ip6-localhost")) {
    IP_ADDR6_HOST (out, 0, 0, 0, 0x00000001UL);
    return 0;
  }

  /* Classical /etc/hosts (VFS) before remote DNS. */
  if (LookupHosts (host, out) == 0) {
    return 0;
  }

  return -1;
}

STATIC
VOID
SockClear (
  msock_t  *s
  )
{
  if (s->rx_q != NULL) {
    pbuf_free (s->rx_q);
    s->rx_q = NULL;
  }

  ZeroMem (s, sizeof (*s));
  s->bound_if = -1;
}

STATIC
err_t
MetalLinkOutput (
  struct netif  *netif,
  struct pbuf   *p
  )
{
  UINT32  tot;
  UINT32  off;
  struct pbuf  *q;

  (VOID)netif;
  if (p == NULL) {
    return ERR_ARG;
  }

  tot = p->tot_len;
  if (tot == 0 || tot > METAL_NET_TX_MAX) {
    return ERR_BUF;
  }

  off = 0;
  for (q = p; q != NULL; q = q->next) {
    CopyMem (mTxScratch + off, q->payload, q->len);
    off += q->len;
  }

  {
    metal_net_iface_t  *mif;

    mif = (metal_net_iface_t *)netif->state;
    if (mif == NULL || mif->l2_tx == NULL || mif->l2_tx (mTxScratch, tot) != 0) {
      return ERR_IF;
    }
  }

  return ERR_OK;
}

STATIC
err_t
MetalNetifInit (
  struct netif  *netif
  )
{
  metal_net_iface_t  *mif;
  CONST UINT8        *mac;

  mif = (metal_net_iface_t *)netif->state;
  if (mif == NULL) {
    return ERR_IF;
  }

  mac = (mif->l2_mac != NULL) ? mif->l2_mac () : pm_metal_virtio_netif_mac ();
  netif->hwaddr_len = ETH_HWADDR_LEN;
  CopyMem (netif->hwaddr, mac, ETH_HWADDR_LEN);
  netif->mtu   = 1500;
  netif->flags = (UINT8)(NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP
                         | NETIF_FLAG_ETHERNET | NETIF_FLAG_LINK_UP
#if LWIP_IPV6
                         | NETIF_FLAG_MLD6
#endif
                         );
  netif->output     = etharp_output;
#if LWIP_IPV6
  netif->output_ip6 = ethip6_output;
#endif
  netif->linkoutput = MetalLinkOutput;
#if LWIP_NETIF_HOSTNAME
  netif_set_hostname (netif, pm_metal_host_name_cstr ());
#endif
  return ERR_OK;
}

STATIC
VOID
ApplyNetifHostname (
  metal_net_iface_t  *mif
  )
{
#if LWIP_NETIF_HOSTNAME
  if (mif == NULL) {
    return;
  }

  netif_set_hostname (&mif->netif, pm_metal_host_name_cstr ());
#else
  (VOID)mif;
#endif
}

STATIC
err_t
MetalLoopOutputIpv4 (
  struct netif      *netif,
  struct pbuf       *p,
  CONST ip4_addr_t  *addr
  )
{
  (VOID)addr;
  return netif_loop_output (netif, p);
}

#if LWIP_IPV6
STATIC
err_t
MetalLoopOutputIpv6 (
  struct netif      *netif,
  struct pbuf       *p,
  CONST ip6_addr_t  *addr
  )
{
  (VOID)addr;
  return netif_loop_output (netif, p);
}
#endif

STATIC
err_t
MetalLoopNetifInit (
  struct netif  *netif
  )
{
  if (netif == NULL) {
    return ERR_IF;
  }

  netif->name[0] = 'l';
  netif->name[1] = 'o';
  netif->mtu     = 65535;
  netif->flags   = (UINT8)(NETIF_FLAG_LINK_UP);
  ZeroMem (netif->hwaddr, sizeof (netif->hwaddr));
  netif->hwaddr_len = ETH_HWADDR_LEN;
  netif->output     = MetalLoopOutputIpv4;
#if LWIP_IPV6
  netif->output_ip6 = MetalLoopOutputIpv6;
#endif
  netif->linkoutput = NULL;
  return ERR_OK;
}

STATIC
VOID
MetalOnFrame (
  VOID          *ctx,
  CONST UINT8   *frame,
  UINT32         len
  )
{
  metal_net_iface_t  *mif;
  struct pbuf        *p;

  mif = (metal_net_iface_t *)ctx;
  if (mif == NULL || frame == NULL || len == 0) {
    return;
  }

  p = pbuf_alloc (PBUF_RAW, (UINT16)len, PBUF_POOL);
  if (p == NULL) {
    return;
  }

  if (pbuf_take (p, frame, (UINT16)len) != ERR_OK) {
    pbuf_free (p);
    return;
  }

  if (mif->netif.input (p, &mif->netif) != ERR_OK) {
    pbuf_free (p);
  }
}

STATIC
VOID
ApplyDnsServer (
  metal_net_iface_t  *mif
  )
{
  ip4_addr_t  dns4;
  ip_addr_t   dns;

  if (mif == NULL || ParseIpv4 (mif->dns, &dns4) != 0) {
    return;
  }

  ip_addr_copy_from_ip4 (dns, dns4);
  dns_setserver (0, &dns);
}

STATIC
VOID
StoreIp4Ascii (
  CHAR8             *dst,
  UINTN              dst_len,
  CONST ip4_addr_t  *addr
  )
{
  if (dst == NULL || dst_len == 0 || addr == NULL) {
    return;
  }

  ip4addr_ntoa_r (addr, dst, (int)dst_len);
}

STATIC
VOID
SyncIfaceBoot (
  metal_net_iface_t  *mif
  )
{
#if LWIP_DHCP && LWIP_DHCP_BOOTP_FILE
  struct dhcp  *dhcp;

  if (mif == NULL) {
    return;
  }

  dhcp = netif_dhcp_data (&mif->netif);
  if (dhcp == NULL || !dhcp_supplied_address (&mif->netif)) {
    return;
  }

  /* Prefer option 66 string; else siaddr as dotted IPv4. */
  if (mif->tftp[0] == '\0' && !ip4_addr_isany_val (dhcp->offered_si_addr)) {
    StoreIp4Ascii (
      mif->tftp,
      sizeof (mif->tftp),
      &dhcp->offered_si_addr
      );
  }

  if (mif->boot_file[0] == '\0' && dhcp->boot_file_name[0] != '\0') {
    AsciiStrCpyS (
      mif->boot_file,
      sizeof (mif->boot_file),
      dhcp->boot_file_name
      );
  }
#else
  (VOID)mif;
#endif
}

STATIC
VOID
SyncIfaceCfg (
  metal_net_iface_t  *mif
  )
{
  CONST ip4_addr_t  *ip;
  CONST ip4_addr_t  *nm;
  CONST ip4_addr_t  *gw;
  CONST ip_addr_t   *dns;

  if (mif == NULL || !mif->used) {
    return;
  }

#if LWIP_DHCP
  if (mif->use_dhcp && !dhcp_supplied_address (&mif->netif)) {
    AsciiStrCpyS (mif->ip, sizeof (mif->ip), "0.0.0.0");
    AsciiStrCpyS (mif->mask, sizeof (mif->mask), "0.0.0.0");
    AsciiStrCpyS (mif->gw, sizeof (mif->gw), "0.0.0.0");
    return;
  }
#endif

  ip = netif_ip4_addr (&mif->netif);
  nm = netif_ip4_netmask (&mif->netif);
  gw = netif_ip4_gw (&mif->netif);
  StoreIp4Ascii (mif->ip, sizeof (mif->ip), ip);
  StoreIp4Ascii (mif->mask, sizeof (mif->mask), nm);
  StoreIp4Ascii (mif->gw, sizeof (mif->gw), gw);

  dns = dns_getserver (0);
  if (dns != NULL && !ip_addr_isany (dns)) {
    if (IP_IS_V4 (dns)) {
      StoreIp4Ascii (mif->dns, sizeof (mif->dns), ip_2_ip4 (dns));
    }
  }

  SyncIfaceBoot (mif);
}

void
pm_metal_dhcp_parse_option (
  struct netif     *netif,
  struct dhcp      *dhcp,
  unsigned char     state,
  struct dhcp_msg  *msg,
  unsigned char     msg_type,
  unsigned char     option,
  unsigned char     len,
  struct pbuf      *pbuf,
  unsigned short    offset
  )
{
  metal_net_iface_t  *mif;
  CHAR8               tmp[PM_METAL_NET_BOOT_FILE_MAX];
  UINT16              n;

  (VOID)dhcp;
  (VOID)state;
  (VOID)msg;
  (VOID)msg_type;
  if (netif == NULL || pbuf == NULL || len == 0) {
    return;
  }

  mif = (metal_net_iface_t *)netif->state;
  if (mif == NULL) {
    return;
  }

  if (option == DHCP_OPTION_TFTP_SERVERNAME) {
    n = (UINT16)len;
    if (n >= sizeof (tmp)) {
      n = (UINT16)(sizeof (tmp) - 1u);
    }

    if (pbuf_copy_partial (pbuf, tmp, n, offset) != n) {
      return;
    }

    tmp[n] = '\0';
    AsciiStrCpyS (mif->tftp, sizeof (mif->tftp), tmp);
  } else if (option == DHCP_OPTION_BOOTFILE) {
    n = (UINT16)len;
    if (n >= sizeof (tmp)) {
      n = (UINT16)(sizeof (tmp) - 1u);
    }

    if (pbuf_copy_partial (pbuf, tmp, n, offset) != n) {
      return;
    }

    tmp[n] = '\0';
    AsciiStrCpyS (mif->boot_file, sizeof (mif->boot_file), tmp);
  }
}

#if LWIP_IPV6_DHCP6
STATIC
INT32
StartIfaceDhcp6 (
  metal_net_iface_t  *mif
  )
{
  /* Called from IfaceInitOne before mif->used is set. */
  if (mif == NULL) {
    return -1;
  }

  switch (mif->dhcp6_mode) {
  case PM_METAL_NET_DHCP6_STATELESS:
    return (dhcp6_enable_stateless (&mif->netif) == ERR_OK) ? 0 : -1;
  case PM_METAL_NET_DHCP6_STATEFUL:
    metal_dhcp6_stateful_reset (&mif->dhcp6_sf);
    return (metal_dhcp6_stateful_start (&mif->netif, &mif->dhcp6_sf) == ERR_OK)
             ? 0
             : -1;
  default:
    return 0;
  }
}
#endif

STATIC
VOID
StopIfaceAutoconf (
  metal_net_iface_t  *mif
  )
{
  if (mif == NULL || !mif->used) {
    return;
  }

#if LWIP_DHCP
  dhcp_stop (&mif->netif);
#endif
#if LWIP_IPV6_DHCP6
  dhcp6_disable (&mif->netif);
  metal_dhcp6_stateful_stop (&mif->netif, &mif->dhcp6_sf);
#endif
}

STATIC
INT32
ApplyIfaceAddrs (
  metal_net_iface_t  *mif
  )
{
  ip4_addr_t  ip;
  ip4_addr_t  nm;
  ip4_addr_t  gw;

  if (mif == NULL || !mif->used) {
    return -1;
  }

  if (ParseIpv4 (mif->ip, &ip) != 0 || ParseIpv4 (mif->mask, &nm) != 0
      || ParseIpv4 (mif->gw, &gw) != 0)
  {
    return -1;
  }

  StopIfaceAutoconf (mif);
  mif->use_dhcp   = 0;
  mif->dhcp6_mode = PM_METAL_NET_DHCP6_OFF;
  netif_set_addr (&mif->netif, &ip, &nm, &gw);
  ApplyDnsServer (mif);
  return 0;
}

STATIC
INT32
ApplyIfaceDhcp (
  metal_net_iface_t  *mif
  )
{
  ip4_addr_t  z;

  if (mif == NULL || !mif->used) {
    return -1;
  }

  StopIfaceAutoconf (mif);
  mif->use_dhcp   = 1;
  mif->dhcp6_mode = PM_METAL_NET_DHCP6_STATELESS;
  IP4_ADDR (&z, 0, 0, 0, 0);
  netif_set_addr (&mif->netif, &z, &z, &z);
  AsciiStrCpyS (mif->ip, sizeof (mif->ip), "0.0.0.0");
  AsciiStrCpyS (mif->mask, sizeof (mif->mask), "0.0.0.0");
  AsciiStrCpyS (mif->gw, sizeof (mif->gw), "0.0.0.0");
  mif->tftp[0]      = '\0';
  mif->boot_file[0] = '\0';
  ApplyNetifHostname (mif);
#if LWIP_IPV6
  netif_create_ip6_linklocal_address (&mif->netif, 1);
#endif
#if LWIP_IPV6_DHCP6
  if (StartIfaceDhcp6 (mif) != 0) {
    return -1;
  }
#endif
#if LWIP_DHCP
  if (dhcp_start (&mif->netif) != ERR_OK) {
    return -1;
  }
#endif
  return 0;
}

STATIC
INT32
ParseIfIndex (
  CONST CHAR8  *name
  )
{
  UINT32  i;
  UINT32  n;

  if (name == NULL || AsciiStrnCmp (name, "eth", 3) != 0) {
    return -1;
  }

  n = 0;
  for (i = 3; name[i] >= '0' && name[i] <= '9'; i++) {
    n = n * 10u + (UINT32)(name[i] - '0');
    if (n >= METAL_NET_MAX_IFACES) {
      return -1;
    }
  }

  if (name[i] != '\0') {
    return -1;
  }

  return (INT32)n;
}

STATIC
metal_net_conf_ov_t *
ConfForIfIndex (
  INT32  idx
  )
{
  if (idx < 0 || idx >= (INT32)METAL_NET_MAX_IFACES) {
    return NULL;
  }

  return &mIfConf[idx];
}

STATIC
INT32
ParseConfDhcp6 (
  CONST CHAR8  *val
  )
{
  if (val == NULL) {
    return -1;
  }

  if (AsciiStrCmp (val, "off") == 0 || AsciiStrCmp (val, "0") == 0) {
    return PM_METAL_NET_DHCP6_OFF;
  }

  if (AsciiStrCmp (val, "stateful") == 0 || AsciiStrCmp (val, "2") == 0) {
    return PM_METAL_NET_DHCP6_STATEFUL;
  }

  if (AsciiStrCmp (val, "stateless") == 0 || AsciiStrCmp (val, "1") == 0
      || AsciiStrCmp (val, "on") == 0)
  {
    return PM_METAL_NET_DHCP6_STATELESS;
  }

  return -1;
}

STATIC
VOID
ApplyConfKey (
  metal_net_conf_ov_t  *ov,
  CONST CHAR8          *key,
  CONST CHAR8          *val
  )
{
  if (ov == NULL || key == NULL || val == NULL) {
    return;
  }

  if (AsciiStrCmp (key, "ip") == 0) {
    AsciiStrCpyS (ov->ip, sizeof (ov->ip), val);
    ov->set_ip    = 1;
    ov->set_dhcp  = 0;
    ov->set_dhcp6 = 0;
  } else if (AsciiStrCmp (key, "mask") == 0) {
    AsciiStrCpyS (ov->mask, sizeof (ov->mask), val);
  } else if (AsciiStrCmp (key, "gw") == 0) {
    AsciiStrCpyS (ov->gw, sizeof (ov->gw), val);
  } else if (AsciiStrCmp (key, "dns") == 0) {
    AsciiStrCpyS (ov->dns, sizeof (ov->dns), val);
  } else if (AsciiStrCmp (key, "dhcp") == 0) {
    ov->set_dhcp = (val[0] != '0') ? 1 : 0;
  } else if (AsciiStrCmp (key, "dhcp6") == 0) {
    INT32  mode;

    mode = ParseConfDhcp6 (val);
    if (mode >= 0) {
      ov->set_dhcp6 = mode;
    }
  } else if (AsciiStrCmp (key, "hostname") == 0) {
    (VOID)pm_metal_host_name_set (val);
  }
}

STATIC
VOID
ApplyIfaceDefaults (
  metal_net_iface_t  *mif,
  UINT32              idx
  )
{
  metal_net_conf_ov_t  *glob;
  metal_net_conf_ov_t  *loc;

  glob = &mGlobalConf;
  loc  = ConfForIfIndex ((INT32)idx);
  AsciiStrCpyS (mif->ip, sizeof (mif->ip), "10.0.2.15");
  AsciiStrCpyS (mif->mask, sizeof (mif->mask), "255.255.255.0");
  AsciiStrCpyS (mif->gw, sizeof (mif->gw), "10.0.2.2");
  AsciiStrCpyS (mif->dns, sizeof (mif->dns), "10.0.2.3");
  mif->use_dhcp   = (glob->set_dhcp >= 0) ? glob->set_dhcp : 1;
  mif->dhcp6_mode = (glob->set_dhcp6 >= 0) ? glob->set_dhcp6
                                           : PM_METAL_NET_DHCP6_STATELESS;

  if (glob->set_ip) {
    AsciiStrCpyS (mif->ip, sizeof (mif->ip), glob->ip);
    mif->use_dhcp   = 0;
    mif->dhcp6_mode = PM_METAL_NET_DHCP6_OFF;
  }

  if (glob->mask[0] != '\0') {
    AsciiStrCpyS (mif->mask, sizeof (mif->mask), glob->mask);
  }

  if (glob->gw[0] != '\0') {
    AsciiStrCpyS (mif->gw, sizeof (mif->gw), glob->gw);
  }

  if (glob->dns[0] != '\0') {
    AsciiStrCpyS (mif->dns, sizeof (mif->dns), glob->dns);
  }

  if (loc == NULL) {
    return;
  }

  if (loc->set_ip) {
    AsciiStrCpyS (mif->ip, sizeof (mif->ip), loc->ip);
    mif->use_dhcp   = 0;
    mif->dhcp6_mode = PM_METAL_NET_DHCP6_OFF;
  }

  if (loc->mask[0] != '\0') {
    AsciiStrCpyS (mif->mask, sizeof (mif->mask), loc->mask);
  }

  if (loc->gw[0] != '\0') {
    AsciiStrCpyS (mif->gw, sizeof (mif->gw), loc->gw);
  }

  if (loc->dns[0] != '\0') {
    AsciiStrCpyS (mif->dns, sizeof (mif->dns), loc->dns);
  }

  if (loc->set_dhcp >= 0) {
    mif->use_dhcp = loc->set_dhcp;
  }

  if (loc->set_dhcp6 >= 0) {
    mif->dhcp6_mode = loc->set_dhcp6;
  }
}

STATIC
VOID
ParseNetConf (
  CONST UINT8  *data,
  UINT32        len
  )
{
  UINT32  i;
  UINT32  line_start;
  CHAR8   line[80];
  UINT32  li;

  if (data == NULL || len == 0) {
    return;
  }

  line_start = 0;
  for (i = 0; i <= len; i++) {
    INT32  end;

    end = (i == len) || (data[i] == '\n') || (data[i] == '\r');
    if (!end) {
      continue;
    }

    li = 0;
    while (line_start < i && li + 1 < sizeof (line)) {
      line[li++] = (CHAR8)data[line_start++];
    }

    line[li] = '\0';
    while (i < len && (data[i] == '\n' || data[i] == '\r')) {
      i++;
    }

    line_start = i;
    if (line[0] == '\0' || line[0] == '#') {
      continue;
    }

    {
      CONST CHAR8           *eq;
      CHAR8                  key[32];
      metal_net_conf_ov_t   *ov;
      CHAR8                 *dot;
      UINTN                  k;

      eq = AsciiStrStr (line, "=");
      if (eq == NULL) {
        continue;
      }

      k = (UINTN)(eq - line);
      if (k == 0 || k >= sizeof (key)) {
        continue;
      }

      CopyMem (key, line, k);
      key[k] = '\0';
      ov     = &mGlobalConf;
      dot    = AsciiStrStr (key, ".");
      if (dot != NULL) {
        *dot = '\0';
        ov   = ConfForIfIndex (ParseIfIndex (key));
        if (ov == NULL) {
          ov = &mGlobalConf;
        }

        ApplyConfKey (ov, dot + 1, eq + 1);
      } else {
        ApplyConfKey (ov, key, eq + 1);
      }
    }
  }
}

STATIC
VOID
TryLoadNetConf (
  VOID
  )
{
  UINT8   *data;
  UINT32   len;
  UINT32   n;

  if (mConfLoaded) {
    return;
  }

  mGlobalConf.set_dhcp  = -1;
  mGlobalConf.set_dhcp6 = -1;
  mGlobalConf.set_ip    = 0;
  mGlobalConf.ip[0]     = '\0';
  mGlobalConf.mask[0]   = '\0';
  mGlobalConf.gw[0]     = '\0';
  mGlobalConf.dns[0]    = '\0';
  for (n = 0; n < METAL_NET_MAX_IFACES; n++) {
    mIfConf[n].set_dhcp  = -1;
    mIfConf[n].set_dhcp6 = -1;
    mIfConf[n].set_ip    = 0;
    mIfConf[n].ip[0]     = '\0';
    mIfConf[n].mask[0]   = '\0';
    mIfConf[n].gw[0]     = '\0';
    mIfConf[n].dns[0]    = '\0';
  }

  if (pm_metal_esp_read_file ("metal/net.conf", &data, &len) == 0) {
    ParseNetConf (data, len);
    pm_metal_mem_free (data);
  }

  mConfLoaded = 1;
}

STATIC
err_t
TcpRecvCb (
  VOID             *arg,
  struct tcp_pcb   *tpcb,
  struct pbuf      *p,
  err_t             err
  )
{
  msock_t  *s;

  s = (msock_t *)arg;
  if (s == NULL) {
    if (p != NULL) {
      pbuf_free (p);
    }

    return ERR_ARG;
  }

  if (err != ERR_OK || p == NULL) {
    if (p != NULL) {
      pbuf_free (p);
    }

    if (!s->recv_done) {
      s->recv_err  = 1;
      s->recv_done = 1;
    }

    return ERR_OK;
  }

  if (s->recv_buf != NULL && !s->recv_done) {
    UINT16  n;

    n = (UINT16)(s->recv_cap < p->tot_len ? s->recv_cap : p->tot_len);
    pbuf_copy_partial (p, s->recv_buf, n, 0);
    s->recv_got  = n;
    s->recv_done = 1;
    tcp_recved (tpcb, p->tot_len);
    pbuf_free (p);
    return ERR_OK;
  }

  if (s->rx_q == NULL) {
    s->rx_q = p;
  } else {
    pbuf_cat (s->rx_q, p);
  }

  return ERR_OK;
}

STATIC
err_t
TcpSentCb (
  VOID            *arg,
  struct tcp_pcb  *tpcb,
  UINT16           len
  )
{
  (VOID)arg;
  (VOID)tpcb;
  (VOID)len;
  return ERR_OK;
}

STATIC
VOID
TcpErrCb (
  VOID   *arg,
  err_t   err
  )
{
  msock_t  *s;

  (VOID)err;
  s = (msock_t *)arg;
  if (s == NULL) {
    return;
  }

  s->tcp       = NULL;
  s->conn_done = 1;
  s->conn_ok   = 0;
  s->recv_err  = 1;
  s->recv_done = 1;
}

STATIC
err_t
TcpConnectedCb (
  VOID            *arg,
  struct tcp_pcb  *tpcb,
  err_t            err
  )
{
  msock_t  *s;

  (VOID)tpcb;
  s = (msock_t *)arg;
  if (s == NULL) {
    return ERR_ARG;
  }

  s->conn_done = 1;
  s->conn_ok   = (err == ERR_OK) ? 1 : 0;
  if (err == ERR_OK) {
    tcp_recv (tpcb, TcpRecvCb);
    tcp_sent (tpcb, TcpSentCb);
  }

  return ERR_OK;
}

STATIC
err_t
TcpAcceptCb (
  VOID            *arg,
  struct tcp_pcb  *newpcb,
  err_t            err
  )
{
  msock_t  *s;

  s = (msock_t *)arg;
  if (s == NULL || err != ERR_OK || newpcb == NULL) {
    if (newpcb != NULL) {
      tcp_abort (newpcb);
    }

    return ERR_VAL;
  }

  if (s->accept_pcb != NULL) {
    tcp_abort (newpcb);
    return ERR_MEM;
  }

  s->accept_pcb = newpcb;
  return ERR_OK;
}

STATIC
VOID
UdpRecvCb (
  VOID            *arg,
  struct udp_pcb  *pcb,
  struct pbuf     *p,
  CONST ip_addr_t *addr,
  UINT16           port
  )
{
  msock_t  *s;

  (VOID)pcb;
  (VOID)addr;
  (VOID)port;
  s = (msock_t *)arg;
  if (s == NULL || p == NULL) {
    if (p != NULL) {
      pbuf_free (p);
    }

    return;
  }

  if (s->recv_buf != NULL && !s->recv_done) {
    UINT16  n;

    n = (UINT16)(s->recv_cap < p->tot_len ? s->recv_cap : p->tot_len);
    pbuf_copy_partial (p, s->recv_buf, n, 0);
    s->recv_got  = n;
    s->recv_done = 1;
    pbuf_free (p);
    return;
  }

  if (s->rx_q == NULL) {
    s->rx_q = p;
  } else {
    pbuf_cat (s->rx_q, p);
  }
}

STATIC
pm_metal_status_t
NetWaitFn (
  pm_metal_coro_t  *self
  )
{
  net_wait_t  *w;
  msock_t     *s;

  w = (net_wait_t *)self;
  if (w->kind == WAIT_DNS) {
    if (w->dns_done) {
      w->coro.result = (VOID *)(UINTN)(w->dns_ok ? 1u : 0u);
      return w->dns_ok ? PM_METAL_DONE : PM_METAL_ERROR;
    }

    if (pm_metal_time_mono_us () > w->deadline) {
      if (mDnsWait == w) {
        mDnsWait = NULL;
      }

      return PM_METAL_ERROR;
    }

    return pm_metal_await (self, pm_metal_sleep_us (2000));
  }

  if (w->h == 0 || w->h > METAL_NET_MAX_SOCKS) {
    return PM_METAL_ERROR;
  }

  s = &mSocks[w->h];
  if (!s->used) {
    return PM_METAL_ERROR;
  }

  if (w->kind == WAIT_CONNECT) {
    if (s->conn_done) {
      w->coro.result = (VOID *)(UINTN)(s->conn_ok ? 1u : 0u);
      return s->conn_ok ? PM_METAL_DONE : PM_METAL_ERROR;
    }
  } else if (w->kind == WAIT_RECV) {
    if (s->recv_done) {
      if (s->recv_err) {
        return PM_METAL_ERROR;
      }

      w->coro.result = (VOID *)(UINTN)s->recv_got;
      return PM_METAL_DONE;
    }

    if (s->rx_q != NULL && s->recv_buf != NULL) {
      UINT16  n;

      n = (UINT16)(s->recv_cap < s->rx_q->tot_len ? s->recv_cap : s->rx_q->tot_len);
      pbuf_copy_partial (s->rx_q, s->recv_buf, n, 0);
      s->recv_got = n;
      {
        struct pbuf  *rest;

        rest = pbuf_free_header (s->rx_q, n);
        s->rx_q = rest;
      }
      if (s->tcp != NULL) {
        tcp_recved (s->tcp, n);
      }

      s->recv_done = 1;
      w->coro.result = (VOID *)(UINTN)s->recv_got;
      return PM_METAL_DONE;
    }
  } else if (w->kind == WAIT_ACCEPT) {
    if (w->accept_out != 0) {
      w->coro.result = (VOID *)(UINTN)w->accept_out;
      return PM_METAL_DONE;
    }

    w->accept_out = PromoteAcceptPcb (s);
    if (w->accept_out != 0) {
      w->coro.result = (VOID *)(UINTN)w->accept_out;
      return PM_METAL_DONE;
    }
  }

  if (pm_metal_time_mono_us () > w->deadline) {
    return PM_METAL_ERROR;
  }

  return pm_metal_await (self, pm_metal_sleep_us (2000));
}

STATIC
pm_metal_status_t
NetOkFn (
  pm_metal_coro_t  *self
  )
{
  self->result = (VOID *)(UINTN)1u;
  return PM_METAL_DONE;
}

STATIC
pm_metal_net_sock_h
PromoteAcceptPcb (
  msock_t  *s
  )
{
  UINT32  i;

  if (s == NULL || s->accept_pcb == NULL) {
    return 0;
  }

  for (i = 1; i <= METAL_NET_MAX_SOCKS; i++) {
    if (mSocks[i].used) {
      continue;
    }

    SockClear (&mSocks[i]);
    mSocks[i].used     = 1;
    mSocks[i].type     = PM_METAL_NET_SOCK_STREAM;
    mSocks[i].tcp      = s->accept_pcb;
    mSocks[i].bound_if = s->bound_if;
    s->accept_pcb      = NULL;
    tcp_arg (mSocks[i].tcp, &mSocks[i]);
    tcp_recv (mSocks[i].tcp, TcpRecvCb);
    tcp_err (mSocks[i].tcp, TcpErrCb);
    tcp_sent (mSocks[i].tcp, TcpSentCb);
    return (pm_metal_net_sock_h)i;
  }

  return 0;
}

STATIC
VOID
DnsRemember (
  CONST ip_addr_t  *ipaddr
  )
{
  if (ipaddr == NULL) {
    mLastDnsValid = 0;
    return;
  }

  mLastDnsAddr  = *ipaddr;
  mLastDnsValid = 1;
}

STATIC
VOID
DnsFoundCb (
  CONST CHAR8     *name,
  CONST ip_addr_t *ipaddr,
  VOID            *arg
  )
{
  net_wait_t  *w;

  (VOID)name;
  w = (net_wait_t *)arg;
  if (w == NULL) {
    return;
  }

  w->dns_done = 1;
  if (ipaddr != NULL) {
    w->dns_addr = *ipaddr;
    w->dns_ok   = 1;
    DnsRemember (ipaddr);
  } else {
    w->dns_ok     = 0;
    mLastDnsValid = 0;
  }

  if (mDnsWait == w) {
    mDnsWait = NULL;
  }
}

STATIC
INT32
StartTcpConnect (
  msock_t          *s,
  CONST ip_addr_t  *ip,
  UINT16            port
  )
{
  err_t  e;

  if (s->tcp == NULL) {
    return -1;
  }

  s->conn_done = 0;
  s->conn_ok   = 0;
  tcp_arg (s->tcp, s);
  tcp_err (s->tcp, TcpErrCb);
  e = tcp_connect (s->tcp, ip, port, TcpConnectedCb);
  return (e == ERR_OK) ? 0 : -1;
}

STATIC
VOID
DnsConnectFound (
  CONST CHAR8     *name,
  CONST ip_addr_t *ipaddr,
  VOID            *arg
  )
{
  msock_t  *s;

  (VOID)name;
  s = (msock_t *)arg;
  if (s == NULL || !s->used) {
    return;
  }

  if (ipaddr == NULL) {
    s->conn_done = 1;
    s->conn_ok   = 0;
    return;
  }

  s->remote      = *ipaddr;
  s->have_remote = 1;
  if (StartTcpConnect (s, ipaddr, s->remote_port) != 0) {
    s->conn_done = 1;
    s->conn_ok   = 0;
  }
}

STATIC
metal_net_iface_t *
IfaceByName (
  CONST CHAR8  *name
  )
{
  UINT32  i;

  if (name == NULL || name[0] == '\0') {
    return IfaceDefault ();
  }

  for (i = 0; i < METAL_NET_MAX_IFACES; i++) {
    if (mIfaces[i].used && AsciiStrCmp (mIfaces[i].name, name) == 0) {
      return &mIfaces[i];
    }
  }

  return NULL;
}

STATIC
metal_net_iface_t *
IfaceDefault (
  VOID
  )
{
  UINT32  i;

  if (mDefaultIdx >= 0 && mDefaultIdx < (INT32)METAL_NET_MAX_IFACES
      && mIfaces[mDefaultIdx].used)
  {
    return &mIfaces[mDefaultIdx];
  }

  for (i = 0; i < METAL_NET_MAX_IFACES; i++) {
    if (mIfaces[i].used) {
      return &mIfaces[i];
    }
  }

  return NULL;
}

STATIC
INT32
LwipEnsure (
  VOID
  )
{
  TryLoadNetConf ();
  TryLoadHosts ();
  if (!mLwipInited) {
    lwip_init ();
    mLwipInited = 1;
  }

  return 0;
}

STATIC
metal_net_iface_t *
IfaceInitOne (
  UINT32                   idx,
  CONST CHAR8             *backend,
  int                    (*open_fn)(uint8_t mac[6]),
  const uint8_t *        (*mac_fn)(void),
  int                    (*tx_fn)(const void *frame, uint32_t len),
  pm_metal_net_l2_poll_fn   poll_fn
  )
{
  metal_net_iface_t  *mif;
  ip4_addr_t          ip;
  ip4_addr_t          nm;
  ip4_addr_t          gw;
  UINT8               hwmac[6];

  if (idx >= METAL_NET_MAX_IFACES || backend == NULL) {
    return NULL;
  }

  if (LwipEnsure () != 0) {
    return NULL;
  }

  mif = &mIfaces[idx];
  ZeroMem (mif, sizeof (*mif));
  AsciiSPrint (mif->name, sizeof (mif->name), "eth%u", mEthCount);
  AsciiStrCpyS (mif->backend, sizeof (mif->backend), backend);
  mif->l2_open = open_fn;
  mif->l2_mac  = mac_fn;
  mif->l2_tx   = tx_fn;
  mif->l2_poll = poll_fn;
  ApplyIfaceDefaults (mif, mEthCount);

  if (mif->l2_open == NULL || mif->l2_open (hwmac) != 0) {
    return NULL;
  }

  if (mif->use_dhcp) {
    IP4_ADDR (&ip, 0, 0, 0, 0);
    IP4_ADDR (&nm, 0, 0, 0, 0);
    IP4_ADDR (&gw, 0, 0, 0, 0);
  } else if (ParseIpv4 (mif->ip, &ip) != 0 || ParseIpv4 (mif->mask, &nm) != 0
             || ParseIpv4 (mif->gw, &gw) != 0)
  {
    return NULL;
  }

  if (netif_add (
        &mif->netif,
        &ip,
        &nm,
        &gw,
        mif,
        MetalNetifInit,
        ethernet_input
        ) == NULL)
  {
    return NULL;
  }

  /* Prefer MAC from L2 open — MetalNetifInit also reads l2_mac(). */
  CopyMem (mif->netif.hwaddr, hwmac, ETH_HWADDR_LEN);
  netif_set_up (&mif->netif);
  ApplyNetifHostname (mif);
  /* First ethN wins default; always displace loopback. */
  if (mDefaultIdx < 0
      || (mIfaces[mDefaultIdx].used
          && AsciiStrCmp (mIfaces[mDefaultIdx].backend, "loopback") == 0))
  {
    netif_set_default (&mif->netif);
    mDefaultIdx = (INT32)idx;
  }

#if LWIP_IPV6
  netif_create_ip6_linklocal_address (&mif->netif, 1);
#endif
#if LWIP_IPV6_DHCP6
  if (mif->dhcp6_mode != PM_METAL_NET_DHCP6_OFF) {
    if (StartIfaceDhcp6 (mif) != 0) {
      return NULL;
    }
  }
#endif
#if LWIP_DHCP
  if (mif->use_dhcp) {
    if (dhcp_start (&mif->netif) != ERR_OK) {
      return NULL;
    }
  } else {
    ApplyDnsServer (mif);
  }
#else
  ApplyDnsServer (mif);
#endif

  mif->used = 1;
  mEthCount++;
  mIfaceCount++;
  return mif;
}

STATIC
int
LwipInit (
  VOID
  )
{
  return METAL_NET_READY () ? 0 : -1;
}

STATIC
VOID
LwipPoll (
  VOID
  )
{
  UINT32  i;

  if (!METAL_NET_READY ()) {
    return;
  }

  for (i = 0; i < METAL_NET_MAX_IFACES; i++) {
    if (!mIfaces[i].used || mIfaces[i].l2_poll == NULL) {
      continue;
    }

    mIfaces[i].l2_poll (MetalOnFrame, &mIfaces[i]);
  }

#if !LWIP_NETIF_LOOPBACK_MULTITHREADING
  netif_poll_all ();
#endif
  sys_check_timeouts ();
  for (i = 0; i < METAL_NET_MAX_IFACES; i++) {
    if (mIfaces[i].used) {
#if LWIP_IPV6_DHCP6
      if (mIfaces[i].dhcp6_mode == PM_METAL_NET_DHCP6_STATEFUL) {
        metal_dhcp6_stateful_poll (&mIfaces[i].netif, &mIfaces[i].dhcp6_sf);
      }
#endif
      SyncIfaceCfg (&mIfaces[i]);
    }
  }
}

STATIC
pm_metal_net_sock_h
LwipSocket (
  UINT32  domain,
  UINT32  type
  )
{
  UINT32  i;

  if (!METAL_NET_READY ()) {
    return PM_METAL_NET_SOCK_INVALID;
  }

  if (domain != PM_METAL_NET_AF_INET && domain != PM_METAL_NET_AF_INET6) {
    return PM_METAL_NET_SOCK_INVALID;
  }

  for (i = 1; i <= METAL_NET_MAX_SOCKS; i++) {
    if (mSocks[i].used) {
      continue;
    }

    SockClear (&mSocks[i]);
    mSocks[i].used   = 1;
    mSocks[i].domain = domain;
    mSocks[i].type   = type;
    if (type == PM_METAL_NET_SOCK_STREAM) {
      mSocks[i].tcp = tcp_new_ip_type (
                        domain == PM_METAL_NET_AF_INET6 ? IPADDR_TYPE_V6
                                                        : IPADDR_TYPE_V4
                        );
      if (mSocks[i].tcp == NULL) {
        SockClear (&mSocks[i]);
        return PM_METAL_NET_SOCK_INVALID;
      }

      tcp_arg (mSocks[i].tcp, &mSocks[i]);
      tcp_err (mSocks[i].tcp, TcpErrCb);
    } else if (type == PM_METAL_NET_SOCK_DGRAM) {
      mSocks[i].udp = udp_new ();
      if (mSocks[i].udp == NULL) {
        SockClear (&mSocks[i]);
        return PM_METAL_NET_SOCK_INVALID;
      }

      udp_recv (mSocks[i].udp, UdpRecvCb, &mSocks[i]);
    } else {
      SockClear (&mSocks[i]);
      return PM_METAL_NET_SOCK_INVALID;
    }

    return (pm_metal_net_sock_h)i;
  }

  return PM_METAL_NET_SOCK_INVALID;
}

STATIC
VOID
LwipClose (
  pm_metal_net_sock_h  h
  )
{
  msock_t  *s;

  if (h == 0 || h > METAL_NET_MAX_SOCKS) {
    return;
  }

  s = &mSocks[h];
  if (!s->used) {
    return;
  }

  if (s->tcp != NULL) {
    tcp_arg (s->tcp, NULL);
    tcp_recv (s->tcp, NULL);
    tcp_err (s->tcp, NULL);
    (VOID)tcp_close (s->tcp);
    s->tcp = NULL;
  }

  if (s->udp != NULL) {
    udp_remove (s->udp);
    s->udp = NULL;
  }

  if (s->accept_pcb != NULL) {
    tcp_abort (s->accept_pcb);
    s->accept_pcb = NULL;
  }

  SockClear (s);
}

STATIC
pm_metal_async_handle_t
LwipConnect (
  pm_metal_net_sock_h  h,
  CONST CHAR8         *host,
  UINT32               port
  )
{
  msock_t      *s;
  net_wait_t   *w;
  ip_addr_t     addr;
  err_t         e;

  if (h == 0 || h > METAL_NET_MAX_SOCKS || host == NULL || !METAL_NET_READY ()) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  s = &mSocks[h];
  if (!s->used) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  s->remote_port = (UINT16)port;

  if (s->type == PM_METAL_NET_SOCK_DGRAM) {
    if (ParseHostAddr (host, &addr) != 0) {
      return PM_METAL_ASYNC_HANDLE_INVALID;
    }

    s->remote      = addr;
    s->have_remote = 1;
    {
      pm_metal_coro_t  *c;

      c = pm_metal_coro (NetOkFn, sizeof (*c));
      if (c == NULL) {
        return PM_METAL_ASYNC_HANDLE_INVALID;
      }

      return pm_metal_async_adopt_host_coro (c);
    }
  }

  if (s->tcp == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  {
    if (ParseHostAddr (host, &addr) == 0) {
      s->remote      = addr;
      s->have_remote = 1;
      if (StartTcpConnect (s, &addr, (UINT16)port) != 0) {
        return PM_METAL_ASYNC_HANDLE_INVALID;
      }
    } else {
      s->conn_done = 0;
      s->conn_ok   = 0;
      e = dns_gethostbyname (host, &addr, DnsConnectFound, s);
      if (e == ERR_OK) {
        s->remote      = addr;
        s->have_remote = 1;
        if (StartTcpConnect (s, &addr, (UINT16)port) != 0) {
          return PM_METAL_ASYNC_HANDLE_INVALID;
        }
      } else if (e != ERR_INPROGRESS) {
        return PM_METAL_ASYNC_HANDLE_INVALID;
      }
    }
  }

  w = (net_wait_t *)pm_metal_coro (NetWaitFn, sizeof (*w));
  if (w == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  w->kind     = WAIT_CONNECT;
  w->h        = h;
  w->deadline = pm_metal_time_mono_us () + 10000000ull;
  return pm_metal_async_adopt_host_coro (&w->coro);
}

STATIC
pm_metal_async_handle_t
LwipListen (
  pm_metal_net_sock_h  h,
  UINT32               port
  )
{
  msock_t          *s;
  struct tcp_pcb   *l;
  pm_metal_coro_t  *c;

  if (h == 0 || h > METAL_NET_MAX_SOCKS || !METAL_NET_READY ()) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  s = &mSocks[h];
  if (!s->used || s->tcp == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  if (tcp_bind (s->tcp, IP_ANY_TYPE, (UINT16)port) != ERR_OK) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  l = tcp_listen (s->tcp);
  if (l == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  s->tcp       = l;
  s->listening = 1;
  tcp_arg (l, s);
  tcp_accept (l, TcpAcceptCb);

  c = pm_metal_coro (NetOkFn, sizeof (*c));
  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_async_adopt_host_coro (c);
}

STATIC
pm_metal_async_handle_t
LwipAccept (
  pm_metal_net_sock_h  h
  )
{
  msock_t          *s;
  net_wait_t       *w;
  pm_metal_net_sock_h  nh;
  pm_metal_coro_t  *c;

  if (h == 0 || h > METAL_NET_MAX_SOCKS) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  s = &mSocks[h];
  if (!s->used || !s->listening) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  nh = PromoteAcceptPcb (s);
  if (nh != 0) {
    c = pm_metal_coro (NetOkFn, sizeof (*c));
    if (c == NULL) {
      return PM_METAL_ASYNC_HANDLE_INVALID;
    }

    c->result = (VOID *)(UINTN)nh;
    return pm_metal_async_adopt_host_coro (c);
  }

  w = (net_wait_t *)pm_metal_coro (NetWaitFn, sizeof (*w));
  if (w == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  w->kind       = WAIT_ACCEPT;
  w->h          = h;
  w->accept_out = 0;
  w->deadline   = pm_metal_time_mono_us () + 30000000ull;
  return pm_metal_async_adopt_host_coro (&w->coro);
}

STATIC
UINT32
LwipSend (
  pm_metal_net_sock_h  h,
  CONST VOID          *ptr,
  UINT32               len
  )
{
  msock_t  *s;
  err_t     e;
  UINT16    n;

  if (h == 0 || h > METAL_NET_MAX_SOCKS || ptr == NULL || len == 0) {
    return 0;
  }

  s = &mSocks[h];
  if (!s->used) {
    return 0;
  }

  if (s->type == PM_METAL_NET_SOCK_STREAM) {
    if (s->tcp == NULL) {
      return 0;
    }

    n = (UINT16)(len > 0xffffu ? 0xffffu : len);
    if (tcp_sndbuf (s->tcp) < n) {
      n = tcp_sndbuf (s->tcp);
    }

    if (n == 0) {
      return 0;
    }

    e = tcp_write (s->tcp, ptr, n, TCP_WRITE_FLAG_COPY);
    if (e != ERR_OK) {
      return 0;
    }

    (VOID)tcp_output (s->tcp);
    return n;
  }

  if (s->udp == NULL || !s->have_remote) {
    return 0;
  }

  {
    struct pbuf  *p;

    n = (UINT16)(len > 0xffffu ? 0xffffu : len);
    p = pbuf_alloc (PBUF_TRANSPORT, n, PBUF_RAM);
    if (p == NULL) {
      return 0;
    }

    CopyMem (p->payload, ptr, n);
    e = udp_sendto (s->udp, p, &s->remote, s->remote_port);
    pbuf_free (p);
    return (e == ERR_OK) ? n : 0;
  }
}

STATIC
pm_metal_async_handle_t
LwipRecv (
  pm_metal_net_sock_h  h,
  VOID                *ptr,
  UINT32               len
  )
{
  msock_t     *s;
  net_wait_t  *w;

  if (h == 0 || h > METAL_NET_MAX_SOCKS || ptr == NULL || len == 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  s = &mSocks[h];
  if (!s->used) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  s->recv_buf  = ptr;
  s->recv_cap  = len;
  s->recv_got  = 0;
  s->recv_done = 0;
  s->recv_err  = 0;

  if (s->rx_q != NULL) {
    UINT16  n;

    n = (UINT16)(len < s->rx_q->tot_len ? len : s->rx_q->tot_len);
    pbuf_copy_partial (s->rx_q, ptr, n, 0);
    s->recv_got  = n;
    s->recv_done = 1;
    s->rx_q      = pbuf_free_header (s->rx_q, n);
    if (s->tcp != NULL) {
      tcp_recved (s->tcp, n);
    }
  }

  w = (net_wait_t *)pm_metal_coro (NetWaitFn, sizeof (*w));
  if (w == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  w->kind     = WAIT_RECV;
  w->h        = h;
  w->deadline = pm_metal_time_mono_us () + 30000000ull;
  return pm_metal_async_adopt_host_coro (&w->coro);
}

STATIC
pm_metal_async_handle_t
LwipDns (
  CONST CHAR8  *host
  )
{
  net_wait_t  *w;
  ip_addr_t    addr;
  err_t        e;

  if (host == NULL || !METAL_NET_READY ()) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  {
    ip_addr_t  addr;

    if (ParseHostAddr (host, &addr) == 0) {
      pm_metal_coro_t  *c;

      c = pm_metal_coro (NetOkFn, sizeof (*c));
      if (c == NULL) {
        return PM_METAL_ASYNC_HANDLE_INVALID;
      }

      DnsRemember (&addr);
      return pm_metal_async_adopt_host_coro (c);
    }
  }

  w = (net_wait_t *)pm_metal_coro (NetWaitFn, sizeof (*w));
  if (w == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  w->kind     = WAIT_DNS;
  w->h        = 0;
  w->deadline = pm_metal_time_mono_us () + 5000000ull;
  w->dns_done = 0;
  w->dns_ok   = 0;
  mDnsWait    = w;
  mLastDnsValid = 0;

  e = dns_gethostbyname (host, &addr, DnsFoundCb, w);
  if (e == ERR_OK) {
    w->dns_done = 1;
    w->dns_ok   = 1;
    w->dns_addr = addr;
    mDnsWait    = NULL;
    DnsRemember (&addr);
  } else if (e != ERR_INPROGRESS) {
    mDnsWait = NULL;
    /* coro will be adopted then fail — close by returning invalid */
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_async_adopt_host_coro (&w->coro);
}

STATIC
INT32
LwipBindIf (
  pm_metal_net_sock_h  h,
  CONST CHAR8         *name
  )
{
  msock_t             *s;
  metal_net_iface_t   *mif;
  INT32                idx;

  if (h == 0 || h > METAL_NET_MAX_SOCKS) {
    return -1;
  }

  s = &mSocks[h];
  if (!s->used) {
    return -1;
  }

  mif = IfaceByName (name);
  if (mif == NULL) {
    return -1;
  }

  idx = (INT32)(mif - mIfaces);
  if (idx < 0 || idx >= METAL_NET_MAX_IFACES) {
    return -1;
  }

  s->bound_if = idx;
  if (s->tcp != NULL) {
    tcp_bind_netif (s->tcp, &mif->netif);
  }

  if (s->udp != NULL) {
    udp_bind_netif (s->udp, &mif->netif);
  }

  return 0;
}

STATIC CONST pm_metal_net_ops_t  mLwipOps = {
  "lwip",
  LwipInit,
  LwipPoll,
  LwipSocket,
  LwipClose,
  LwipConnect,
  LwipListen,
  LwipAccept,
  LwipSend,
  LwipRecv,
  LwipDns,
  LwipBindIf
};

int
pm_metal_net_virtio_detect (
  VOID
  )
{
  if (pm_metal_virtio_find (PM_METAL_VIRTIO_DEV_NET) != 0
      && pm_metal_virtio_find (PM_METAL_VIRTIO_DEV_NET_LEGACY) != 0)
  {
    return -1;
  }

  {
    STATIC pm_metal_io_node_t  Node = {
      .class = PM_METAL_IO_NET,
      .compat = "lwip+virtio-net",
      .caps = 1,
      .bus = PM_METAL_IO_BUS_PCI
    };

    (VOID)pm_metal_io_dt_add (&Node);
  }
  return 0;
}

int
pm_metal_net_lwip_start_with_l2 (
  CONST CHAR8                *backend,
  int (*open_fn)(uint8_t mac_out[6]),
  const uint8_t *(*mac_fn)(void),
  int (*tx_fn)(const void *frame, uint32_t len),
  pm_metal_net_l2_poll_fn     poll_fn
  )
{
  UINT32              i;
  metal_net_iface_t  *mif;

  if (backend == NULL) {
    return -1;
  }

  for (i = 0; i < METAL_NET_MAX_IFACES; i++) {
    if (mIfaces[i].used && AsciiStrCmp (mIfaces[i].backend, backend) == 0) {
      return 0;
    }
  }

  for (i = 0; i < METAL_NET_MAX_IFACES; i++) {
    if (!mIfaces[i].used) {
      break;
    }
  }

  if (i >= METAL_NET_MAX_IFACES) {
    return -1;
  }

  mif = IfaceInitOne (
          i,
          backend,
          open_fn,
          mac_fn,
          tx_fn,
          poll_fn
          );
  if (mif == NULL) {
    return -1;
  }

  if (!mOpsRegistered) {
    pm_metal_net_set_ops (&mLwipOps);
    mOpsRegistered = 1;
  }

  return 0;
}

int
pm_metal_net_virtio_start (
  VOID
  )
{
  return pm_metal_net_lwip_start_with_l2 (
           "lwip+virtio-net",
           pm_metal_virtio_netif_open,
           pm_metal_virtio_netif_mac,
           pm_metal_virtio_netif_tx,
           (pm_metal_net_l2_poll_fn)pm_metal_virtio_netif_poll
           );
}

int
pm_metal_net_loopback_start (
  VOID
  )
{
  UINT32              i;
  metal_net_iface_t  *mif;
  ip4_addr_t          ip;
  ip4_addr_t          nm;
  ip4_addr_t          gw;

  for (i = 0; i < METAL_NET_MAX_IFACES; i++) {
    if (mIfaces[i].used && AsciiStrCmp (mIfaces[i].name, "lo") == 0) {
      return 0;
    }
  }

  for (i = 0; i < METAL_NET_MAX_IFACES; i++) {
    if (!mIfaces[i].used) {
      break;
    }
  }

  if (i >= METAL_NET_MAX_IFACES) {
    return -1;
  }

  if (LwipEnsure () != 0) {
    return -1;
  }

  mif = &mIfaces[i];
  ZeroMem (mif, sizeof (*mif));
  AsciiStrCpyS (mif->name, sizeof (mif->name), "lo");
  AsciiStrCpyS (mif->backend, sizeof (mif->backend), "loopback");
  AsciiStrCpyS (mif->ip, sizeof (mif->ip), "127.0.0.1");
  AsciiStrCpyS (mif->mask, sizeof (mif->mask), "255.0.0.0");
  AsciiStrCpyS (mif->gw, sizeof (mif->gw), "127.0.0.1");
  mif->use_dhcp   = 0;
  mif->dhcp6_mode = PM_METAL_NET_DHCP6_OFF;

  IP4_ADDR (&ip, 127, 0, 0, 1);
  IP4_ADDR (&nm, 255, 0, 0, 0);
  IP4_ADDR (&gw, 127, 0, 0, 1);

  if (netif_add (
        &mif->netif,
        &ip,
        &nm,
        &gw,
        mif,
        MetalLoopNetifInit,
        ip_input
        ) == NULL)
  {
    return -1;
  }

  netif_set_link_up (&mif->netif);
  netif_set_up (&mif->netif);
#if LWIP_IPV6
  IP_ADDR6_HOST (mif->netif.ip6_addr, 0, 0, 0, 0x00000001UL);
  mif->netif.ip6_addr_state[0] = IP6_ADDR_VALID;
#endif

  if (mDefaultIdx < 0) {
    netif_set_default (&mif->netif);
    mDefaultIdx = (INT32)i;
  }

  mif->used = 1;
  mIfaceCount++;
  SyncIfaceCfg (mif);

  if (!mOpsRegistered) {
    pm_metal_net_set_ops (&mLwipOps);
    mOpsRegistered = 1;
  }

  return 0;
}

int
pm_metal_net_bge_detect (
  VOID
  )
{
  if (pm_metal_bge_netif_detect () != 0) {
    return -1;
  }

  {
    STATIC pm_metal_io_node_t  Node = {
      .class = PM_METAL_IO_NET,
      .compat = "lwip+bge",
      .caps = 1,
      .bus = PM_METAL_IO_BUS_PCI
    };

    (VOID)pm_metal_io_dt_add (&Node);
  }
  return 0;
}

int
pm_metal_net_bge_start (
  VOID
  )
{
  return pm_metal_net_lwip_start_with_l2 (
           "lwip+bge",
           pm_metal_bge_netif_open,
           pm_metal_bge_netif_mac,
           pm_metal_bge_netif_tx,
           (pm_metal_net_l2_poll_fn)pm_metal_bge_netif_poll
           );
}

int
pm_metal_net_virtio_probe (
  VOID
  )
{
  if (pm_metal_net_virtio_detect () != 0) {
    return -1;
  }

  return pm_metal_net_virtio_start ();
}

STATIC
VOID
FillIfcfg (
  metal_net_iface_t     *mif,
  pm_metal_net_ifcfg_t  *out
  )
{
  CONST UINT8  *mac;

  if (mif == NULL || out == NULL || !mif->used) {
    return;
  }

  SyncIfaceCfg (mif);
  ZeroMem (out, sizeof (*out));
  AsciiStrCpyS (out->name, sizeof (out->name), mif->name);
  AsciiStrCpyS (out->ip, sizeof (out->ip), mif->ip);
  AsciiStrCpyS (out->mask, sizeof (out->mask), mif->mask);
  AsciiStrCpyS (out->gw, sizeof (out->gw), mif->gw);
  AsciiStrCpyS (out->dns, sizeof (out->dns), mif->dns);
  AsciiStrCpyS (out->tftp, sizeof (out->tftp), mif->tftp);
  AsciiStrCpyS (out->boot_file, sizeof (out->boot_file), mif->boot_file);
  mac = (mif->l2_mac != NULL) ? mif->l2_mac () : pm_metal_virtio_netif_mac ();
  CopyMem (out->mac, mac, 6);
  out->link_up = netif_is_link_up (&mif->netif) && netif_is_up (&mif->netif);
#if LWIP_DHCP
  if (mif->use_dhcp && !dhcp_supplied_address (&mif->netif)) {
    out->link_up = 0;
  }
#endif
  out->backend = mif->backend;
}

STATIC
CONST CHAR8 *
Dhcp6ModeName (
  INT32  mode
  )
{
  switch (mode) {
  case PM_METAL_NET_DHCP6_STATELESS:
    return "stateless";
  case PM_METAL_NET_DHCP6_STATEFUL:
    return "stateful";
  default:
    return "off";
  }
}

STATIC
INT32
FormatIfStatusLine (
  CONST pm_metal_net_ifcfg_t  *cfg,
  metal_net_iface_t           *mif,
  CHAR8                       *buf,
  UINT32                       buf_len
  )
{
  if (cfg == NULL || buf == NULL || buf_len == 0) {
    return -1;
  }

#if LWIP_DHCP
  if (mif != NULL && mif->use_dhcp && AsciiStrCmp (cfg->ip, "0.0.0.0") == 0) {
    return AsciiSPrint (
             buf,
             buf_len,
             "%a %a  dhcp4 pending  dhcp6 %a  mac %02x:%02x:%02x:%02x:%02x:%02x",
             cfg->name,
             cfg->backend,
             Dhcp6ModeName (mif->dhcp6_mode),
             cfg->mac[0],
             cfg->mac[1],
             cfg->mac[2],
             cfg->mac[3],
             cfg->mac[4],
             cfg->mac[5]
             );
  }
#else
  (VOID)mif;
#endif

  return AsciiSPrint (
           buf,
           buf_len,
           "%a %a  %a/%a gw %a dns %a  mac %02x:%02x:%02x:%02x:%02x:%02x  %a",
           cfg->name,
           cfg->backend,
           cfg->ip,
           cfg->mask,
           cfg->gw,
           cfg->dns,
           cfg->mac[0],
           cfg->mac[1],
           cfg->mac[2],
           cfg->mac[3],
           cfg->mac[4],
           cfg->mac[5],
           cfg->link_up ? "up" : "down"
           );
}

unsigned
pm_metal_net_if_count (
  VOID
  )
{
  return mIfaceCount;
}

int
pm_metal_net_if_get_index (
  unsigned              index,
  pm_metal_net_ifcfg_t  *out
  )
{
  UINT32              n;
  UINT32              i;
  metal_net_iface_t  *mif;

  if (out == NULL || index >= mIfaceCount) {
    return -1;
  }

  n = 0;
  for (i = 0; i < METAL_NET_MAX_IFACES; i++) {
    if (!mIfaces[i].used) {
      continue;
    }

    if (n == index) {
      mif = &mIfaces[i];
      FillIfcfg (mif, out);
      return 0;
    }

    n++;
  }

  return -1;
}

int
pm_metal_net_if_get_named (
  CONST CHAR8           *name,
  pm_metal_net_ifcfg_t  *out
  )
{
  metal_net_iface_t  *mif;

  if (out == NULL) {
    return -1;
  }

  mif = IfaceByName (name);
  if (mif == NULL) {
    return -1;
  }

  FillIfcfg (mif, out);
  return 0;
}

int
pm_metal_net_if_get (
  pm_metal_net_ifcfg_t  *out
  )
{
  return pm_metal_net_if_get_named (NULL, out);
}

int
pm_metal_net_if_set_named (
  CONST CHAR8  *name,
  CONST CHAR8  *ip,
  CONST CHAR8  *mask,
  CONST CHAR8  *gw,
  CONST CHAR8  *dns
  )
{
  metal_net_iface_t  *mif;
  ip4_addr_t          tip;
  ip4_addr_t          tnm;
  ip4_addr_t          tgw;

  if (!METAL_NET_READY () || ip == NULL || mask == NULL || gw == NULL) {
    return -1;
  }

  mif = IfaceByName (name);
  if (mif == NULL) {
    return -1;
  }

  if (ParseIpv4 (ip, &tip) != 0 || ParseIpv4 (mask, &tnm) != 0
      || ParseIpv4 (gw, &tgw) != 0)
  {
    return -1;
  }

  if (dns != NULL && dns[0] != '\0') {
    ip4_addr_t  tdns;

    if (ParseIpv4 (dns, &tdns) != 0) {
      return -1;
    }

    AsciiStrCpyS (mif->dns, sizeof (mif->dns), dns);
  }

  AsciiStrCpyS (mif->ip, sizeof (mif->ip), ip);
  AsciiStrCpyS (mif->mask, sizeof (mif->mask), mask);
  AsciiStrCpyS (mif->gw, sizeof (mif->gw), gw);
  return ApplyIfaceAddrs (mif);
}

int
pm_metal_net_if_set (
  CONST CHAR8  *ip,
  CONST CHAR8  *mask,
  CONST CHAR8  *gw,
  CONST CHAR8  *dns
  )
{
  return pm_metal_net_if_set_named (NULL, ip, mask, gw, dns);
}

int
pm_metal_net_if_set_dhcp_named (
  CONST CHAR8  *name
  )
{
  metal_net_iface_t  *mif;

  if (!METAL_NET_READY ()) {
    return -1;
  }

  mif = IfaceByName (name);
  if (mif == NULL) {
    return -1;
  }

  return ApplyIfaceDhcp (mif);
}

int
pm_metal_net_if_set_dhcp6_named (
  CONST CHAR8  *name,
  int           mode
  )
{
  metal_net_iface_t  *mif;

  if (!METAL_NET_READY () || mode < PM_METAL_NET_DHCP6_OFF
      || mode > PM_METAL_NET_DHCP6_STATEFUL)
  {
    return -1;
  }

  mif = IfaceByName (name);
  if (mif == NULL) {
    return -1;
  }

  StopIfaceAutoconf (mif);
  mif->dhcp6_mode = mode;
#if LWIP_IPV6
  netif_create_ip6_linklocal_address (&mif->netif, 1);
#endif
#if LWIP_IPV6_DHCP6
  if (mode != PM_METAL_NET_DHCP6_OFF && StartIfaceDhcp6 (mif) != 0) {
    return -1;
  }
#endif
  return 0;
}

int
pm_metal_net_if_set_dhcp (
  VOID
  )
{
  return pm_metal_net_if_set_dhcp_named (NULL);
}

int
pm_metal_net_if_status_named (
  CONST CHAR8  *name,
  CHAR8        *buf,
  UINT32        buf_len
  )
{
  pm_metal_net_ifcfg_t  cfg;
  metal_net_iface_t    *mif;

  if (buf == NULL || buf_len == 0) {
    return -1;
  }

  mif = IfaceByName (name);
  if (mif == NULL) {
    AsciiSPrint (buf, buf_len, "net: no interface %a", (name != NULL) ? name : "eth0");
    return 0;
  }

  FillIfcfg (mif, &cfg);
  FormatIfStatusLine (&cfg, mif, buf, buf_len);
  return 0;
}

int
pm_metal_net_if_status (
  CHAR8    *buf,
  UINT32    buf_len
  )
{
  CHAR8   line[180];
  UINT32  off;
  UINT32  i;
  UINT32  n;

  if (buf == NULL || buf_len == 0) {
    return -1;
  }

  if (!METAL_NET_READY ()) {
    AsciiSPrint (buf, buf_len, "net: down");
    return 0;
  }

  buf[0] = '\0';
  off    = 0;
  n      = 0;
  for (i = 0; i < METAL_NET_MAX_IFACES; i++) {
    pm_metal_net_ifcfg_t  cfg;

    if (!mIfaces[i].used) {
      continue;
    }

    FillIfcfg (&mIfaces[i], &cfg);
    FormatIfStatusLine (&cfg, &mIfaces[i], line, (UINT32)sizeof (line));
    if (n > 0 && off + 1 < buf_len) {
      buf[off++] = '\n';
    }

    {
      UINTN  ll;
      UINTN  room;

      ll   = AsciiStrLen (line);
      room = (UINTN)buf_len - off;
      if (room <= 1) {
        break;
      }

      if (ll >= room) {
        ll = room - 1;
      }

      CopyMem (buf + off, line, ll);
      off        += (UINT32)ll;
      buf[off]    = '\0';
    }

    n++;
  }

  return 0;
}

int
pm_metal_net_if_boot_get (
  CONST CHAR8  *name,
  CHAR8        *tftp_host,
  UINT32        tftp_cap,
  CHAR8        *boot_file,
  UINT32        boot_cap
  )
{
  metal_net_iface_t  *mif;

  mif = IfaceByName (name);
  if (mif == NULL) {
    return -1;
  }

  SyncIfaceCfg (mif);
  if (tftp_host != NULL && tftp_cap > 0) {
    if (AsciiStrCpyS (tftp_host, tftp_cap, mif->tftp) != RETURN_SUCCESS) {
      return -1;
    }
  }

  if (boot_file != NULL && boot_cap > 0) {
    if (AsciiStrCpyS (boot_file, boot_cap, mif->boot_file) != RETURN_SUCCESS) {
      return -1;
    }
  }

  return 0;
}

void
pm_metal_net_on_hostname_changed (
  VOID
  )
{
  UINT32  i;

  for (i = 0; i < METAL_NET_MAX_IFACES; i++) {
    if (!mIfaces[i].used) {
      continue;
    }

    if (AsciiStrCmp (mIfaces[i].backend, "loopback") == 0) {
      continue;
    }

    ApplyNetifHostname (&mIfaces[i]);
#if LWIP_DHCP
    if (mIfaces[i].use_dhcp && dhcp_supplied_address (&mIfaces[i].netif)) {
      (VOID)dhcp_renew (&mIfaces[i].netif);
    }
#endif
  }
}

int
pm_metal_net_resolve_ip4 (
  CONST CHAR8  *host,
  UINT32       *out_host
  )
{
  ip_addr_t  addr;

  if (host == NULL || out_host == NULL) {
    return -1;
  }

  TryLoadHosts ();
  if (ParseHostAddr (host, &addr) == 0 && IP_IS_V4_VAL (addr)) {
    *out_host = lwip_ntohl (ip4_addr_get_u32 (ip_2_ip4 (&addr)));
    return 0;
  }

  if (dns_gethostbyname (host, &addr, NULL, NULL) == ERR_OK
      && IP_IS_V4_VAL (addr))
  {
    *out_host = lwip_ntohl (ip4_addr_get_u32 (ip_2_ip4 (&addr)));
    return 0;
  }

  return -1;
}

int
pm_metal_net_dns_last_ntoa (
  CHAR8   *out,
  UINT32   out_cap
  )
{
  if (out == NULL || out_cap == 0 || !mLastDnsValid) {
    return -1;
  }

  if (IP_IS_V6_VAL (mLastDnsAddr)) {
    if (ip6addr_ntoa_r (ip_2_ip6 (&mLastDnsAddr), out, (int)out_cap) == NULL) {
      return -1;
    }

    return 0;
  }

  if (ip4addr_ntoa_r (ip_2_ip4 (&mLastDnsAddr), out, (int)out_cap) == NULL) {
    return -1;
  }

  return 0;
}
