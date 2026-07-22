/** @file
  Minimal stateful DHCPv6 (SOLICIT/REQUEST/REPLY, IA_NA/IAADDR).
  lwIP's dhcp6_enable_stateful() remains a stub; Metal uses this module.
**/
#include <pymergetic/metal/dev/net/metal_dhcp6_stateful.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>

#include <lwip/def.h>
#include <lwip/ip6_addr.h>
#include <lwip/ip_addr.h>
#include <lwip/prot/dhcp6.h>
#include <lwip/prot/dhcp6.h>
#include <lwip/udp.h>
#include <lwip/pbuf.h>
#include <lwip/mem.h>

#define METAL_DH6_OFF     0u
#define METAL_DH6_IDLE    1u
#define METAL_DH6_SOL     2u
#define METAL_DH6_REQ     3u
#define METAL_DH6_BOUND   4u

#define METAL_DH6_TIMER_TICKS  1u

typedef struct {
	struct netif  *netif;
	metal_dhcp6_stateful_t *st;
} metal_dhcp6_rx_ctx_t;

STATIC struct udp_pcb       *g_dhcp6_sf_pcb;
STATIC u8_t                  g_dhcp6_sf_pcb_refs;
STATIC metal_dhcp6_rx_ctx_t  g_dhcp6_sf_rx[4];

STATIC const ip_addr_t  g_dhcp6_all_servers = IPADDR6_INIT_HOST (0xFF020000, 0, 0, 0x00010002);

STATIC void
metal_dhcp6_stateful_set (
  metal_dhcp6_stateful_t  *st,
  u8_t                     state
  )
{
  if (st == NULL) {
    return;
  }

  if (st->state != state) {
    st->state = state;
    st->tries = 0;
    st->timeout_ticks = 0;
  }
}

STATIC err_t
metal_dhcp6_sf_pcb_get (
  VOID
  )
{
  if (g_dhcp6_sf_pcb_refs == 0) {
    g_dhcp6_sf_pcb = udp_new_ip6 ();
    if (g_dhcp6_sf_pcb == NULL) {
      return ERR_MEM;
    }

    udp_bind (g_dhcp6_sf_pcb, IP6_ADDR_ANY, DHCP6_CLIENT_PORT);
  }

  g_dhcp6_sf_pcb_refs++;
  return ERR_OK;
}

STATIC void
metal_dhcp6_sf_pcb_put (
  VOID
  )
{
  if (g_dhcp6_sf_pcb_refs == 0) {
    return;
  }

  g_dhcp6_sf_pcb_refs--;
  if (g_dhcp6_sf_pcb_refs == 0 && g_dhcp6_sf_pcb != NULL) {
    udp_remove (g_dhcp6_sf_pcb);
    g_dhcp6_sf_pcb = NULL;
  }
}

STATIC u16_t
metal_dhcp6_put_opt (
  u8_t   *out,
  u16_t   off,
  u16_t   max,
  u16_t   type,
  CONST u8_t *data,
  u16_t   len
  )
{
  if (out == NULL || off + 4u + len > max) {
    return off;
  }

  out[off++] = (u8_t)(type >> 8);
  out[off++] = (u8_t)(type & 0xffu);
  out[off++] = (u8_t)(len >> 8);
  out[off++] = (u8_t)(len & 0xffu);
  if (len > 0 && data != NULL) {
    CopyMem (out + off, (VOID *)data, len);
    off = (u16_t)(off + len);
  }

  return off;
}

STATIC u16_t
metal_dhcp6_append_duid_ll (
  u8_t          *out,
  u16_t          off,
  u16_t          max,
  struct netif  *netif
  )
{
  u8_t  duid[10];

  duid[0] = 0;
  duid[1] = DHCP6_DUID_LL;
  duid[2] = 0;
  duid[3] = 1;
  CopyMem (&duid[4], netif->hwaddr, 6);
  return metal_dhcp6_put_opt (out, off, max, DHCP6_OPTION_CLIENTID, duid, sizeof (duid));
}

STATIC u16_t
metal_dhcp6_append_ia_na (
  u8_t   *out,
  u16_t   off,
  u16_t   max,
  u32_t   iaid
  )
{
  u8_t  ia[12];

  ia[0]  = (u8_t)(iaid >> 24);
  ia[1]  = (u8_t)(iaid >> 16);
  ia[2]  = (u8_t)(iaid >> 8);
  ia[3]  = (u8_t)(iaid);
  ia[4]  = ia[5] = ia[6] = ia[7] = 0;
  ia[8]  = ia[9] = ia[10] = ia[11] = 0;
  return metal_dhcp6_put_opt (out, off, max, DHCP6_OPTION_IA_NA, ia, sizeof (ia));
}

STATIC err_t
metal_dhcp6_send (
  struct netif             *netif,
  metal_dhcp6_stateful_t   *st,
  u8_t                      msgtype,
  u8_t                      with_server_id
  )
{
  struct pbuf     *p;
  struct dhcp6_msg *msg;
  u8_t             opts[192];
  u16_t            olen;
  err_t            e;

  if (netif == NULL || st == NULL) {
    return ERR_ARG;
  }

  if (st->tries == 0) {
    st->xid = LWIP_RAND () & 0xFFFFFFu;
  }

  p = pbuf_alloc (PBUF_TRANSPORT, (u16_t)(sizeof (*msg) + sizeof (opts)), PBUF_RAM);
  if (p == NULL) {
    return ERR_MEM;
  }

  msg = (struct dhcp6_msg *)p->payload;
  ZeroMem (msg, sizeof (*msg));
  msg->msgtype = msgtype;
  msg->transaction_id[0] = (u8_t)(st->xid >> 16);
  msg->transaction_id[1] = (u8_t)(st->xid >> 8);
  msg->transaction_id[2] = (u8_t)(st->xid);

  olen = 0;
  olen = metal_dhcp6_append_duid_ll (opts, olen, sizeof (opts), netif);
  if (with_server_id && st->server_id_len > 0) {
    olen = metal_dhcp6_put_opt (
             opts,
             olen,
             sizeof (opts),
             DHCP6_OPTION_SERVERID,
             st->server_id,
             st->server_id_len
             );
  }

  olen = metal_dhcp6_append_ia_na (opts, olen, sizeof (opts), st->iaid);
  CopyMem ((u8_t *)(msg + 1), opts, olen);
  pbuf_realloc (p, (u16_t)(sizeof (*msg) + olen));

  e = udp_sendto_if (g_dhcp6_sf_pcb, p, &g_dhcp6_all_servers, DHCP6_SERVER_PORT, netif);
  pbuf_free (p);
  if (e != ERR_OK) {
    return e;
  }

  if (st->tries < 255) {
    st->tries++;
  }

  st->timeout_ticks = (u16_t)(METAL_DH6_TIMER_TICKS * (1u << (st->tries > 5 ? 5 : st->tries)));
  return ERR_OK;
}

STATIC s8_t
metal_dhcp6_addr_slot (
  struct netif  *netif
  )
{
  s8_t  i;

  for (i = 1; i < LWIP_IPV6_NUM_ADDRESSES; i++) {
    if (netif_ip6_addr_state (netif, i) == IP6_ADDR_INVALID) {
      return i;
    }
  }

  return -1;
}

STATIC void
metal_dhcp6_apply_addr (
  struct netif             *netif,
  metal_dhcp6_stateful_t   *st,
  CONST ip6_addr_t         *addr6
  )
{
  s8_t  slot;

  slot = metal_dhcp6_addr_slot (netif);
  if (slot < 0 || addr6 == NULL) {
    return;
  }

  netif_ip6_addr_set (netif, slot, addr6);
  netif_ip6_addr_set_state (netif, slot, IP6_ADDR_PREFERRED);
  st->bound = 1;
  metal_dhcp6_stateful_set (st, METAL_DH6_BOUND);
}

STATIC void
metal_dhcp6_parse_ia_addr (
  struct netif             *netif,
  metal_dhcp6_stateful_t   *st,
  CONST u8_t               *data,
  u16_t                     len
  )
{
  ip6_addr_t  a6;

  if (len < sizeof (struct ip6_addr_packed)) {
    return;
  }

  ip6_addr_set_zero (&a6);
  CopyMem (&a6, (VOID *)data, sizeof (struct ip6_addr_packed));
  ip6_addr_assign_zone (&a6, IP6_UNICAST, netif);
  metal_dhcp6_apply_addr (netif, st, &a6);
}

STATIC void
metal_dhcp6_parse_options (
  struct netif             *netif,
  metal_dhcp6_stateful_t   *st,
  CONST u8_t               *opts,
  u16_t                     opts_len,
  u8_t                      msgtype
  )
{
  u16_t  off;

  off = 0;
  while (off + 4u <= opts_len) {
    u16_t  opt;
    u16_t  olen;

    opt   = (u16_t)((opts[off] << 8) | opts[off + 1]);
    olen  = (u16_t)((opts[off + 2] << 8) | opts[off + 3]);
    off  += 4;
    if (off + olen > opts_len) {
      break;
    }

    if (opt == DHCP6_OPTION_SERVERID && st->server_id_len == 0 && olen <= sizeof (st->server_id)) {
      CopyMem (st->server_id, (VOID *)(opts + off), olen);
      st->server_id_len = olen;
    } else if (opt == DHCP6_OPTION_IA_NA && olen >= 12u) {
      u16_t  sub = (u16_t)(off + 12u);

      while (sub + 4u <= off + olen) {
        u16_t  sub_opt;
        u16_t  sub_len;

        sub_opt  = (u16_t)((opts[sub] << 8) | opts[sub + 1]);
        sub_len  = (u16_t)((opts[sub + 2] << 8) | opts[sub + 3]);
        sub     += 4;
        if (sub + sub_len > off + olen) {
          break;
        }

        if (sub_opt == DHCP6_OPTION_IAADDR) {
          metal_dhcp6_parse_ia_addr (netif, st, opts + sub, sub_len);
        }

        sub = (u16_t)(sub + sub_len);
      }
    }

    off = (u16_t)(off + olen);
  }

  if (st->bound) {
    return;
  }

  if (msgtype == DHCP6_ADVERTISE && st->state == METAL_DH6_SOL) {
    if (st->server_id_len > 0) {
      metal_dhcp6_stateful_set (st, METAL_DH6_REQ);
      (VOID)metal_dhcp6_send (netif, st, DHCP6_REQUEST, 1);
    }
  } else if (msgtype == DHCP6_REPLY && st->state == METAL_DH6_REQ && !st->bound) {
    metal_dhcp6_stateful_set (st, METAL_DH6_IDLE);
    (VOID)metal_dhcp6_send (netif, st, DHCP6_SOLICIT, 0);
  }
}

STATIC void
metal_dhcp6_sf_recv (
  void           *arg,
  struct udp_pcb *pcb,
  struct pbuf    *p,
  const ip_addr_t *addr,
  u16_t           port
  )
{
  metal_dhcp6_rx_ctx_t     *ctx;
  struct dhcp6_msg         *msg;
  u32_t                     xid;
  u16_t                     opts_off;
  u16_t                     opts_len;

  (VOID)pcb;
  (VOID)addr;
  (VOID)port;
  ctx = (metal_dhcp6_rx_ctx_t *)arg;
  if (ctx == NULL || ctx->netif == NULL || ctx->st == NULL || p == NULL) {
    goto done;
  }

  if (p->len < sizeof (*msg)) {
    goto done;
  }

  msg = (struct dhcp6_msg *)p->payload;
  xid = ((u32_t)msg->transaction_id[0] << 16)
        | ((u32_t)msg->transaction_id[1] << 8)
        | (u32_t)msg->transaction_id[2];
  if (xid != ctx->st->xid) {
    goto done;
  }

  opts_off = sizeof (*msg);
  if (p->tot_len <= opts_off) {
    goto done;
  }

  opts_len = (u16_t)(p->tot_len - opts_off);
  metal_dhcp6_parse_options (ctx->netif, ctx->st, (CONST u8_t *)msg + opts_off, opts_len, msg->msgtype);

done:
  if (p != NULL) {
    pbuf_free (p);
  }
}

STATIC metal_dhcp6_rx_ctx_t *
metal_dhcp6_rx_ctx_for (
  struct netif  *netif
  )
{
  UINT32  i;

  for (i = 0; i < LWIP_ARRAYSIZE (g_dhcp6_sf_rx); i++) {
    if (g_dhcp6_sf_rx[i].netif == netif) {
      return &g_dhcp6_sf_rx[i];
    }
  }

  for (i = 0; i < LWIP_ARRAYSIZE (g_dhcp6_sf_rx); i++) {
    if (g_dhcp6_sf_rx[i].netif == NULL) {
      return &g_dhcp6_sf_rx[i];
    }
  }

  return NULL;
}

void
metal_dhcp6_stateful_reset (
  metal_dhcp6_stateful_t  *st
  )
{
  if (st == NULL) {
    return;
  }

  ZeroMem (st, sizeof (*st));
}

err_t
metal_dhcp6_stateful_start (
  struct netif             *netif,
  metal_dhcp6_stateful_t   *st
  )
{
  metal_dhcp6_rx_ctx_t  *ctx;

  if (netif == NULL || st == NULL) {
    return ERR_ARG;
  }

  if (st->state != METAL_DH6_OFF && st->state != METAL_DH6_IDLE) {
    return ERR_OK;
  }

  if (metal_dhcp6_sf_pcb_get () != ERR_OK) {
    return ERR_MEM;
  }

  ctx = metal_dhcp6_rx_ctx_for (netif);
  if (ctx == NULL) {
    metal_dhcp6_sf_pcb_put ();
    return ERR_MEM;
  }

  ctx->netif = netif;
  ctx->st    = st;
  if (st->iaid == 0) {
    st->iaid = LWIP_RAND ();
  }

  udp_recv (g_dhcp6_sf_pcb, metal_dhcp6_sf_recv, ctx);
  metal_dhcp6_stateful_set (st, METAL_DH6_IDLE);
  return ERR_OK;
}

void
metal_dhcp6_stateful_stop (
  struct netif             *netif,
  metal_dhcp6_stateful_t   *st
  )
{
  metal_dhcp6_rx_ctx_t  *ctx;

  if (st != NULL) {
    metal_dhcp6_stateful_reset (st);
  }

  ctx = metal_dhcp6_rx_ctx_for (netif);
  if (ctx != NULL && ctx->netif == netif) {
    ctx->netif = NULL;
    ctx->st    = NULL;
  }

  metal_dhcp6_sf_pcb_put ();
}

void
metal_dhcp6_stateful_poll (
  struct netif             *netif,
  metal_dhcp6_stateful_t   *st
  )
{
  if (netif == NULL || st == NULL || st->state == METAL_DH6_OFF || st->bound) {
    return;
  }

  if (!netif_is_up (netif) || !netif_is_link_up (netif)) {
    return;
  }

  if (st->timeout_ticks > 0) {
    st->timeout_ticks--;
    return;
  }

  switch (st->state) {
  case METAL_DH6_IDLE:
    metal_dhcp6_stateful_set (st, METAL_DH6_SOL);
    (VOID)metal_dhcp6_send (netif, st, DHCP6_SOLICIT, 0);
    break;
  case METAL_DH6_SOL:
    (VOID)metal_dhcp6_send (netif, st, DHCP6_SOLICIT, 0);
    break;
  case METAL_DH6_REQ:
    (VOID)metal_dhcp6_send (netif, st, DHCP6_REQUEST, 1);
    break;
  default:
    break;
  }
}
