/** @file
  lwIP NO_SYS bridge — sockets + static IPv4 config on virtio-net L2.
  (impl: efi)
**/
#include <pymergetic/metal/net/net_ops.h>
#include <pymergetic/metal/net/net_cfg.h>
#include <pymergetic/metal/async/async.h>
#include <pymergetic/metal/io/io.h>
#include <pymergetic/metal/esp/esp.h>
#include <coro/coro.h>
#include <time/time.h>
#include <mem/mem.h>

#include "virtio_netif.h"

#include <lwip/init.h>
#include <lwip/netif.h>
#include <lwip/timeouts.h>
#include <lwip/ip4_addr.h>
#include <lwip/dns.h>
#include <lwip/tcp.h>
#include <lwip/udp.h>
#include <lwip/pbuf.h>
#include <lwip/etharp.h>
#include <netif/ethernet.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>

#define METAL_NET_MAX_SOCKS  16u
#define METAL_NET_TX_MAX     1514u

typedef enum {
  WAIT_CONNECT = 0,
  WAIT_RECV,
  WAIT_ACCEPT,
  WAIT_DNS
} wait_kind_t;

typedef struct {
  INT32             used;
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
} msock_t;

typedef struct {
  pm_metal_coro_t       coro;
  wait_kind_t           kind;
  pm_metal_net_sock_h   h;
  UINT64                deadline;
  INT32                 dns_done;
  INT32                 dns_ok;
  ip_addr_t             dns_addr;
} net_wait_t;

STATIC struct netif  mNetif;
STATIC INT32         mReady;
STATIC INT32         mLwipInited;
STATIC msock_t       mSocks[METAL_NET_MAX_SOCKS + 1];
STATIC UINT8         mTxScratch[METAL_NET_TX_MAX];
STATIC CHAR8         mIp[16]   = "10.0.2.15";
STATIC CHAR8         mMask[16] = "255.255.255.0";
STATIC CHAR8         mGw[16]   = "10.0.2.2";
STATIC CHAR8         mDns[16]  = "10.0.2.3";
STATIC net_wait_t   *mDnsWait;

STATIC
INT32
ParseIpv4 (
  CONST CHAR8  *s,
  ip4_addr_t   *out
  )
{
  UINT32  a;
  UINT32  b;
  UINT32  c;
  UINT32  d;
  UINT32  v;
  CONST CHAR8  *p;
  UINT32       *n;

  if (s == NULL || out == NULL) {
    return -1;
  }

  a = b = c = d = 0;
  p = s;
  n = &a;
  v = 0;
  for (;;) {
    if (*p >= '0' && *p <= '9') {
      v = v * 10u + (UINT32)(*p - '0');
      if (v > 255) {
        return -1;
      }

      p++;
      continue;
    }

    *n = v;
    if (*p == '.') {
      if (n == &a) {
        n = &b;
      } else if (n == &b) {
        n = &c;
      } else if (n == &c) {
        n = &d;
      } else {
        return -1;
      }

      v = 0;
      p++;
      continue;
    }

    if (*p == '\0') {
      if (n != &d) {
        return -1;
      }

      *n = v;
      IP4_ADDR (out, a, b, c, d);
      return 0;
    }

    return -1;
  }
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

  if (pm_metal_virtio_netif_tx (mTxScratch, tot) != 0) {
    return ERR_IF;
  }

  return ERR_OK;
}

STATIC
err_t
MetalNetifInit (
  struct netif  *netif
  )
{
  CONST UINT8  *mac;

  mac = pm_metal_virtio_netif_mac ();
  netif->hwaddr_len = ETH_HWADDR_LEN;
  CopyMem (netif->hwaddr, mac, ETH_HWADDR_LEN);
  netif->mtu   = 1500;
  netif->flags = (UINT8)(NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP
                         | NETIF_FLAG_ETHERNET | NETIF_FLAG_LINK_UP);
  netif->output     = etharp_output;
  netif->linkoutput = MetalLinkOutput;
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
  struct pbuf  *p;

  (VOID)ctx;
  if (frame == NULL || len == 0) {
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

  if (mNetif.input (p, &mNetif) != ERR_OK) {
    pbuf_free (p);
  }
}

STATIC
VOID
ApplyDnsServer (
  VOID
  )
{
  ip4_addr_t  dns4;
  ip_addr_t   dns;

  if (ParseIpv4 (mDns, &dns4) != 0) {
    return;
  }

  ip_addr_copy_from_ip4 (dns, dns4);
  dns_setserver (0, &dns);
}

STATIC
INT32
ApplyAddrs (
  VOID
  )
{
  ip4_addr_t  ip;
  ip4_addr_t  nm;
  ip4_addr_t  gw;

  if (ParseIpv4 (mIp, &ip) != 0 || ParseIpv4 (mMask, &nm) != 0
      || ParseIpv4 (mGw, &gw) != 0)
  {
    return -1;
  }

  netif_set_addr (&mNetif, &ip, &nm, &gw);
  ApplyDnsServer ();
  return 0;
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

    if (AsciiStrnCmp (line, "ip=", 3) == 0) {
      AsciiStrCpyS (mIp, sizeof (mIp), line + 3);
    } else if (AsciiStrnCmp (line, "mask=", 5) == 0) {
      AsciiStrCpyS (mMask, sizeof (mMask), line + 5);
    } else if (AsciiStrnCmp (line, "gw=", 3) == 0) {
      AsciiStrCpyS (mGw, sizeof (mGw), line + 3);
    } else if (AsciiStrnCmp (line, "dns=", 4) == 0) {
      AsciiStrCpyS (mDns, sizeof (mDns), line + 4);
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

  if (pm_metal_esp_read_file ("metal/net.conf", &data, &len) != 0) {
    return;
  }

  ParseNetConf (data, len);
  pm_metal_mem_free (data);
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

    s->recv_err  = 1;
    s->recv_done = 1;
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
      return s->conn_ok ? PM_METAL_DONE : PM_METAL_ERROR;
    }
  } else if (w->kind == WAIT_RECV) {
    if (s->recv_done) {
      return s->recv_err ? PM_METAL_ERROR : PM_METAL_DONE;
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
      return PM_METAL_DONE;
    }
  } else if (w->kind == WAIT_ACCEPT) {
    if (s->accept_pcb != NULL) {
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
NetDoneFn (
  pm_metal_coro_t  *self
  )
{
  (VOID)self;
  return PM_METAL_DONE;
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
  } else {
    w->dns_ok = 0;
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
int
LwipInit (
  VOID
  )
{
  ip4_addr_t  ip;
  ip4_addr_t  nm;
  ip4_addr_t  gw;

  if (mReady) {
    return 0;
  }

  TryLoadNetConf ();

  if (!mLwipInited) {
    lwip_init ();
    mLwipInited = 1;
  }

  if (ParseIpv4 (mIp, &ip) != 0 || ParseIpv4 (mMask, &nm) != 0
      || ParseIpv4 (mGw, &gw) != 0)
  {
    return -1;
  }

  if (netif_add (
        &mNetif,
        &ip,
        &nm,
        &gw,
        NULL,
        MetalNetifInit,
        ethernet_input
        ) == NULL)
  {
    return -1;
  }

  netif_set_default (&mNetif);
  netif_set_up (&mNetif);
  ApplyDnsServer ();
  mReady = 1;
  return 0;
}

STATIC
VOID
LwipPoll (
  VOID
  )
{
  if (!mReady) {
    return;
  }

  pm_metal_virtio_netif_poll (MetalOnFrame, NULL);
  sys_check_timeouts ();
}

STATIC
pm_metal_net_sock_h
LwipSocket (
  UINT32  domain,
  UINT32  type
  )
{
  UINT32  i;

  (VOID)domain;
  if (!mReady) {
    return PM_METAL_NET_SOCK_INVALID;
  }

  for (i = 1; i <= METAL_NET_MAX_SOCKS; i++) {
    if (mSocks[i].used) {
      continue;
    }

    SockClear (&mSocks[i]);
    mSocks[i].used = 1;
    mSocks[i].type = type;
    if (type == PM_METAL_NET_SOCK_STREAM) {
      mSocks[i].tcp = tcp_new ();
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

  if (h == 0 || h > METAL_NET_MAX_SOCKS || host == NULL || !mReady) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  s = &mSocks[h];
  if (!s->used) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  s->remote_port = (UINT16)port;

  if (s->type == PM_METAL_NET_SOCK_DGRAM) {
    ip4_addr_t  a4;

    if (ParseIpv4 (host, &a4) != 0) {
      return PM_METAL_ASYNC_HANDLE_INVALID;
    }

    ip_addr_copy_from_ip4 (addr, a4);
    s->remote      = addr;
    s->have_remote = 1;
    {
      pm_metal_coro_t  *c;

      c = pm_metal_coro (NetDoneFn, sizeof (*c));
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
    ip4_addr_t  a4;

    if (ParseIpv4 (host, &a4) == 0) {
      ip_addr_copy_from_ip4 (addr, a4);
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

  if (h == 0 || h > METAL_NET_MAX_SOCKS || !mReady) {
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

  c = pm_metal_coro (NetDoneFn, sizeof (*c));
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
  msock_t     *s;
  net_wait_t  *w;
  UINT32       i;

  if (h == 0 || h > METAL_NET_MAX_SOCKS) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  s = &mSocks[h];
  if (!s->used || !s->listening) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  /* If already have a PCB, promote into a free sock slot immediately. */
  if (s->accept_pcb != NULL) {
    for (i = 1; i <= METAL_NET_MAX_SOCKS; i++) {
      if (mSocks[i].used) {
        continue;
      }

      SockClear (&mSocks[i]);
      mSocks[i].used = 1;
      mSocks[i].type = PM_METAL_NET_SOCK_STREAM;
      mSocks[i].tcp  = s->accept_pcb;
      s->accept_pcb   = NULL;
      tcp_arg (mSocks[i].tcp, &mSocks[i]);
      tcp_recv (mSocks[i].tcp, TcpRecvCb);
      tcp_err (mSocks[i].tcp, TcpErrCb);
      tcp_sent (mSocks[i].tcp, TcpSentCb);
      break;
    }
  }

  w = (net_wait_t *)pm_metal_coro (NetWaitFn, sizeof (*w));
  if (w == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  w->kind     = WAIT_ACCEPT;
  w->h        = h;
  w->deadline = pm_metal_time_mono_us () + 30000000ull;
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

  if (host == NULL || !mReady) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  {
    ip4_addr_t  a4;

    if (ParseIpv4 (host, &a4) == 0) {
      pm_metal_coro_t  *c;

      c = pm_metal_coro (NetDoneFn, sizeof (*c));
      if (c == NULL) {
        return PM_METAL_ASYNC_HANDLE_INVALID;
      }

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

  e = dns_gethostbyname (host, &addr, DnsFoundCb, w);
  if (e == ERR_OK) {
    w->dns_done = 1;
    w->dns_ok   = 1;
    w->dns_addr = addr;
    mDnsWait    = NULL;
  } else if (e != ERR_INPROGRESS) {
    mDnsWait = NULL;
    /* coro will be adopted then fail — close by returning invalid */
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_async_adopt_host_coro (&w->coro);
}

STATIC CONST pm_metal_net_ops_t  mLwipOps = {
  "lwip+virtio-net",
  LwipInit,
  LwipPoll,
  LwipSocket,
  LwipClose,
  LwipConnect,
  LwipListen,
  LwipAccept,
  LwipSend,
  LwipRecv,
  LwipDns
};

int
pm_metal_net_virtio_probe (
  VOID
  )
{
  UINT8  mac[6];

  if (pm_metal_virtio_netif_open (mac) != 0) {
    return -1;
  }

  (VOID)mac;
  if (LwipInit () != 0) {
    return -1;
  }

  pm_metal_net_set_ops (&mLwipOps);
  {
    STATIC pm_metal_io_node_t  Node = {
      PM_METAL_IO_NET, "lwip+virtio-net", 1
    };

    (VOID)pm_metal_io_dt_register (&Node);
  }
  return 0;
}

int
pm_metal_net_if_get (
  pm_metal_net_ifcfg_t  *out
  )
{
  CONST UINT8  *mac;

  if (out == NULL || !mReady) {
    return -1;
  }

  ZeroMem (out, sizeof (*out));
  AsciiStrCpyS (out->ip, sizeof (out->ip), mIp);
  AsciiStrCpyS (out->mask, sizeof (out->mask), mMask);
  AsciiStrCpyS (out->gw, sizeof (out->gw), mGw);
  AsciiStrCpyS (out->dns, sizeof (out->dns), mDns);
  mac = pm_metal_virtio_netif_mac ();
  CopyMem (out->mac, mac, 6);
  out->link_up = netif_is_link_up (&mNetif) && netif_is_up (&mNetif);
  out->backend = "lwip+virtio-net";
  return 0;
}

int
pm_metal_net_if_set (
  CONST CHAR8  *ip,
  CONST CHAR8  *mask,
  CONST CHAR8  *gw,
  CONST CHAR8  *dns
  )
{
  ip4_addr_t  tip;
  ip4_addr_t  tnm;
  ip4_addr_t  tgw;

  if (!mReady || ip == NULL || mask == NULL || gw == NULL) {
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

    AsciiStrCpyS (mDns, sizeof (mDns), dns);
  }

  AsciiStrCpyS (mIp, sizeof (mIp), ip);
  AsciiStrCpyS (mMask, sizeof (mMask), mask);
  AsciiStrCpyS (mGw, sizeof (mGw), gw);
  return ApplyAddrs ();
}

int
pm_metal_net_if_status (
  CHAR8    *buf,
  UINT32    buf_len
  )
{
  pm_metal_net_ifcfg_t  cfg;

  if (buf == NULL || buf_len == 0) {
    return -1;
  }

  if (pm_metal_net_if_get (&cfg) != 0) {
    AsciiSPrint (buf, buf_len, "net: down");
    return 0;
  }

  AsciiSPrint (
    buf,
    buf_len,
    "net %a  %a/%a gw %a dns %a  mac %02x:%02x:%02x:%02x:%02x:%02x  %a",
    cfg.backend,
    cfg.ip,
    cfg.mask,
    cfg.gw,
    cfg.dns,
    cfg.mac[0],
    cfg.mac[1],
    cfg.mac[2],
    cfg.mac[3],
    cfg.mac[4],
    cfg.mac[5],
    cfg.link_up ? "up" : "down"
    );
  return 0;
}
