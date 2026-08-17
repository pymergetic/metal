/* pymergetic.metal.drivers.net.gmac — Synopsys dwmac-4.20a.
 * RV1106 firmware: RMII MAC @ 0xffa80000, PHY MDIO addr 2, GRF RMII 100M.
 * Other seats: linked, probe/up fail closed (no MMIO). Same netdev face. */
#include "pymergetic/metal/drivers/net/gmac/__exports__.h"

#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/net/ip.h"

#include <stdint.h>
#include <string.h>

#if defined(PM_METAL_FIRMWARE) && defined(__arm__) && !defined(__aarch64__)
#define PM_METAL_GMAC_HW 1
#include "pm_cpu.h"
#endif

#define GMAC_RING 4u
#define GMAC_FRAME 1536u
#define GMAC_RXBUF 2048u

struct dwmac4_desc {
    volatile uint32_t des0;
    volatile uint32_t des1;
    volatile uint32_t des2;
    volatile uint32_t des3;
};

struct gmac_nic {
    uint32_t used;
    uint32_t tx_i;
    uint32_t rx_i;
    uint8_t mac[6];
    int32_t dt_id;
    int32_t net_h;
    pm_metal_netdev_ops_t ops;
#if defined(PM_METAL_GMAC_HW)
    _Alignas(64) struct dwmac4_desc txd[GMAC_RING];
    _Alignas(64) struct dwmac4_desc rxd[GMAC_RING];
    _Alignas(64) uint8_t txb[GMAC_RING][GMAC_FRAME];
    _Alignas(64) uint8_t rxb[GMAC_RING][GMAC_RXBUF];
#endif
};

static pm_util_mem_arena_t *s_arena;
static struct gmac_nic s_dev;

#if defined(PM_METAL_GMAC_HW)

#define GMAC_BASE ((volatile uint32_t *)(uintptr_t)0xffa80000u)
#define GRF_BASE ((volatile uint32_t *)(uintptr_t)0xff000000u)

#define GMAC_CONFIG 0x0000u
#define GMAC_PACKET_FILTER 0x0008u
#define GMAC_RXQ_CTRL0 0x00a0u
#define GMAC_MDIO_ADDR 0x0200u
#define GMAC_MDIO_DATA 0x0204u
#define GMAC_ADDR_HIGH0 0x0300u
#define GMAC_ADDR_LOW0 0x0304u
#define MTL_TXQ0_OP 0x0d00u
#define MTL_RXQ0_OP 0x0d30u
#define DMA_BUS_MODE 0x1000u
#define DMA_SYS_BUS 0x1004u
#define DMA_CHAN_TX_CONTROL 0x1104u
#define DMA_CHAN_RX_CONTROL 0x1108u
#define DMA_CHAN_TX_BASE 0x1114u
#define DMA_CHAN_RX_BASE 0x111cu
#define DMA_CHAN_TX_END 0x1120u
#define DMA_CHAN_RX_END 0x1128u
#define DMA_CHAN_TX_RING_LEN 0x112cu
#define DMA_CHAN_RX_RING_LEN 0x1130u

#define GMAC_CONFIG_RE (1u << 0)
#define GMAC_CONFIG_TE (1u << 1)
#define GMAC_CONFIG_DCRS (1u << 9)
#define GMAC_CONFIG_DM (1u << 13)
#define GMAC_CONFIG_FES (1u << 14)
#define GMAC_CONFIG_PS (1u << 15)
#define GMAC_CONFIG_JD (1u << 17)
#define GMAC_CONFIG_BE (1u << 18)
#define GMAC_CONFIG_ACS (1u << 20)
#define GMAC_FILTER_PR (1u << 0)
#define GMAC_FILTER_PM (1u << 4)
#define GMAC_RXQ0_DCB (1u << 1)
#define DMA_SWR (1u << 0)
#define DMA_TX_ST (1u << 0)
#define DMA_TX_OSP (1u << 4)
#define DMA_RX_SR (1u << 0)
#define MTL_TXQEN (1u << 3)
#define MTL_TSF (1u << 1)
#define MTL_RSF (1u << 5)
#define TDES2_IOC (1u << 31)
#define TDES3_OWN (1u << 31)
#define TDES3_FD (1u << 29)
#define TDES3_LD (1u << 28)
#define RDES3_OWN (1u << 31)
#define RDES3_IOC (1u << 30)
#define RDES3_FD (1u << 29)
#define RDES3_LD (1u << 28)
#define RDES3_BUF1V (1u << 24)
#define RDES3_ES (1u << 15)
#define GMAC_HI_AE (1u << 31)
#define MDIO_GB (1u << 0)
#define MDIO_GOC_WRITE (1u << 2)
#define MDIO_GOC_READ (3u << 2)
#define PHY_ADDR 2u
#define PHY_BMCR 0u
#define PHY_BMSR 1u

#define GRF_GMAC_CLK_CON (0x60004u / 4u)
#define GRF_MACPHY_CON0 (0x60028u / 4u)
#define GRF_BIT(n) ((1u << (n)) | (1u << ((n) + 16u)))
#define GRF_CLR_BIT(n) (1u << ((n) + 16u))
/* VOCRU @ 0xff3bc000 — ACLK/PCLK_MAC and CLK_MACPHY live here. Gate is set-to-disable. */
#define VOCRU_BASE ((volatile uint32_t *)(uintptr_t)0xff3bc000u)
#define VOCLKGATE_CON(x) ((0x800u / 4u) + (x))
#define VOSOFTRST_CON(x) ((0xa00u / 4u) + (x))

static uint32_t grf_field(uint32_t msb, uint32_t lsb, uint32_t val) {
    uint32_t mask = ((1u << (msb - lsb + 1u)) - 1u) << lsb;
    return (mask << 16) | ((val << lsb) & mask);
}

static void spin_us(uint32_t us) {
    uint32_t i;
    uint32_t n = us * 200u;
    /* Counted delay only — CNTPCT can be 0 after bootm; a timer wait never returns. */
    for (i = 0; i < n; i++) {
        pm_cpu_pause();
    }
}

#define VOCLKSEL_CON(x) ((0x300u / 4u) + (x))
#define VOSRST_MAC_BIU_P 6u
#define VOSRST_MAC_BIU_A 7u
#define VOSRST_MAC 8u
#define VOSRST_MACPHY 13u

static void vocru_sel(uint32_t con, uint32_t lsb, uint32_t width, uint32_t val) {
    uint32_t mask = ((1u << width) - 1u) << lsb;
    VOCRU_BASE[VOCLKSEL_CON(con)] = (mask << 16) | ((val << lsb) & mask);
}

static void vocru_ungate(uint32_t con, uint32_t bit) {
    /* SET_TO_DISABLE: write 0 through the hiword mask to turn the clock on. */
    uint32_t m = 1u << bit;
    VOCRU_BASE[VOCLKGATE_CON(con)] = (m << 16);
}

static void vocru_rst(uint32_t con, uint32_t bit, uint32_t assert) {
    uint32_t m = 1u << bit;
    VOCRU_BASE[VOSOFTRST_CON(con)] = (m << 16) | (assert ? m : 0u);
}

static void gmac_clk_phy_up(void) {
    /* VOCLKSEL mux 3 = xin_osc0 (24M). Only VOGATE CON0–CON3 exist; CON4+ hangs the bus. */
    vocru_sel(0, 0, 2, 3);
    vocru_sel(0, 2, 2, 3);
    vocru_sel(0, 4, 2, 3);
    vocru_sel(1, 12, 2, 3);
    vocru_ungate(0, 0);  /* aclk_vo_root */
    vocru_ungate(0, 1);  /* hclk_vo_root */
    vocru_ungate(0, 2);  /* pclk_vo_root */
    vocru_ungate(0, 3);  /* aclk_vo_biu */
    vocru_ungate(0, 4);  /* hclk_vo_biu */
    vocru_ungate(0, 10); /* pclk_vo_grf */
    vocru_ungate(1, 4);  /* aclk_mac_root */
    vocru_ungate(1, 6);  /* pclk_mac_biu */
    vocru_ungate(1, 7);  /* aclk_mac_biu */
    vocru_ungate(1, 8);  /* aclk_mac */
    vocru_ungate(1, 9);  /* pclk_mac */
    vocru_ungate(2, 13); /* clk_macphy */
    vocru_rst(1, VOSRST_MAC_BIU_P, 1);
    vocru_rst(1, VOSRST_MAC_BIU_A, 1);
    vocru_rst(1, VOSRST_MAC, 1);
    vocru_rst(2, VOSRST_MACPHY, 1);
    spin_us(30u);
    /* Linux rk_gmac_integrated_fephy_powerup: power, internal RMII, clksel 6, PHYAD 2. */
    GRF_BASE[GRF_MACPHY_CON0] = GRF_CLR_BIT(1) | GRF_BIT(6) | grf_field(9, 7, 6) | grf_field(14, 10, 2);
    /* RMII, 25M (100M), PHY clock, RMII not gated. */
    GRF_BASE[GRF_GMAC_CLK_CON] = GRF_BIT(0) | GRF_BIT(2) | GRF_CLR_BIT(4) | GRF_CLR_BIT(1);
    spin_us(12000u);
    vocru_rst(2, VOSRST_MACPHY, 0);
    spin_us(50000u);
    vocru_rst(1, VOSRST_MAC, 0);
    vocru_rst(1, VOSRST_MAC_BIU_A, 0);
    vocru_rst(1, VOSRST_MAC_BIU_P, 0);
    spin_us(1000u);
}

static void wr(uint32_t off, uint32_t v) {
    GMAC_BASE[off / 4u] = v;
}

static uint32_t rd(uint32_t off) {
    return GMAC_BASE[off / 4u];
}

static int32_t wait_clr(uint32_t off, uint32_t bit, uint32_t spins) {
    uint32_t i;
    for (i = 0; i < spins; i++) {
        if ((rd(off) & bit) == 0u) {
            return 0;
        }
        pm_cpu_pause();
    }
    return -1;
}

static int32_t mdio_wait(void) {
    return wait_clr(GMAC_MDIO_ADDR, MDIO_GB, 100000u);
}

static int32_t mdio_write(uint32_t reg, uint16_t val) {
    uint32_t addr;
    wr(GMAC_MDIO_DATA, val);
    addr = (PHY_ADDR << 21) | (reg << 16) | (4u << 8) | MDIO_GOC_WRITE | MDIO_GB;
    wr(GMAC_MDIO_ADDR, addr);
    return mdio_wait();
}

static int32_t mdio_read(uint32_t reg, uint16_t *out) {
    uint32_t addr;
    if (out == NULL) {
        return -1;
    }
    addr = (PHY_ADDR << 21) | (reg << 16) | (4u << 8) | MDIO_GOC_READ | MDIO_GB;
    wr(GMAC_MDIO_ADDR, addr);
    if (mdio_wait() != 0) {
        return -1;
    }
    *out = (uint16_t)rd(GMAC_MDIO_DATA);
    return 0;
}

static void gmac_phy_rmii100(void) {
    uint16_t bmsr;
    GRF_BASE[GRF_GMAC_CLK_CON] = GRF_BIT(0) | GRF_BIT(2);
    (void)mdio_write(PHY_BMCR, 0x8000u);
    (void)wait_clr(GMAC_MDIO_ADDR, MDIO_GB, 10000u);
    /* 100M full duplex; AN off — Luckfox RMII is 100. */
    (void)mdio_write(PHY_BMCR, 0x2100u);
    (void)mdio_read(PHY_BMSR, &bmsr);
    (void)bmsr;
}

static void gmac_set_mac(struct gmac_nic *d) {
    /* Luckfox Pico Max Linux MAC (this board). Always program AE. */
    d->mac[0] = 0x72;
    d->mac[1] = 0xb3;
    d->mac[2] = 0x59;
    d->mac[3] = 0xc7;
    d->mac[4] = 0x48;
    d->mac[5] = 0x9f;
    wr(GMAC_ADDR_HIGH0, GMAC_HI_AE | ((uint32_t)d->mac[5] << 8) | d->mac[4]);
    wr(GMAC_ADDR_LOW0, ((uint32_t)d->mac[3] << 24) | ((uint32_t)d->mac[2] << 16)
            | ((uint32_t)d->mac[1] << 8) | d->mac[0]);
}

static void gmac_arm_rx(struct gmac_nic *d, uint32_t i) {
    d->rxd[i].des0 = (uint32_t)(uintptr_t)d->rxb[i];
    d->rxd[i].des1 = 0;
    d->rxd[i].des2 = 0;
    pm_cpu_store_fence();
    d->rxd[i].des3 = RDES3_OWN | RDES3_IOC | RDES3_BUF1V;
}

static int32_t gmac_hw_open(struct gmac_nic *d) {
    uint32_t i;
    uint32_t cfg;
    /* Unclocked GMAC MMIO wedges the A7. PHY is on-die RK630 — must power it.
     * Do not read GMAC_HW_FEATURE0: that load hangs the bus if clocks are still wrong. */
    gmac_clk_phy_up();
    wr(DMA_BUS_MODE, DMA_SWR);
    if (wait_clr(DMA_BUS_MODE, DMA_SWR, 1000000u) != 0) {
        return -1;
    }
    gmac_phy_rmii100();
    wr(DMA_SYS_BUS, (1u << 0) | (1u << 14));
    wr(MTL_TXQ0_OP, MTL_TXQEN | MTL_TSF | (7u << 16));
    wr(MTL_RXQ0_OP, MTL_RSF | (7u << 20));
    wr(GMAC_RXQ_CTRL0, GMAC_RXQ0_DCB);
    wr(GMAC_PACKET_FILTER, GMAC_FILTER_PR | GMAC_FILTER_PM);
    gmac_set_mac(d);
    memset(d->txd, 0, sizeof(d->txd));
    memset(d->rxd, 0, sizeof(d->rxd));
    d->tx_i = 0;
    d->rx_i = 0;
    for (i = 0; i < GMAC_RING; i++) {
        gmac_arm_rx(d, i);
    }
    pm_cpu_store_fence();
    wr(DMA_CHAN_TX_BASE, (uint32_t)(uintptr_t)d->txd);
    wr(DMA_CHAN_RX_BASE, (uint32_t)(uintptr_t)d->rxd);
    wr(DMA_CHAN_TX_RING_LEN, GMAC_RING - 1u);
    wr(DMA_CHAN_RX_RING_LEN, GMAC_RING - 1u);
    wr(DMA_CHAN_RX_CONTROL, (8u << 16) | (GMAC_RXBUF << 1) | DMA_RX_SR);
    wr(DMA_CHAN_TX_CONTROL, (8u << 16) | DMA_TX_OSP | DMA_TX_ST);
    wr(DMA_CHAN_RX_END, (uint32_t)(uintptr_t)(d->rxd + GMAC_RING));
    cfg = rd(GMAC_CONFIG);
    cfg |= GMAC_CONFIG_TE | GMAC_CONFIG_RE | GMAC_CONFIG_DM | GMAC_CONFIG_FES | GMAC_CONFIG_PS
        | GMAC_CONFIG_ACS | GMAC_CONFIG_DCRS | GMAC_CONFIG_JD | GMAC_CONFIG_BE;
    wr(GMAC_CONFIG, cfg);
    return 0;
}

static int32_t gmac_open(void *ctx) {
    struct gmac_nic *d = ctx;
    if (d == NULL) {
        return -1;
    }
    return gmac_hw_open(d);
}

static void gmac_close(void *ctx) {
    struct gmac_nic *d = ctx;
    if (d == NULL) {
        return;
    }
    wr(GMAC_CONFIG, rd(GMAC_CONFIG) & ~(GMAC_CONFIG_TE | GMAC_CONFIG_RE));
    wr(DMA_CHAN_TX_CONTROL, rd(DMA_CHAN_TX_CONTROL) & ~DMA_TX_ST);
    wr(DMA_CHAN_RX_CONTROL, rd(DMA_CHAN_RX_CONTROL) & ~DMA_RX_SR);
    d->used = 0;
    d->dt_id = -1;
    d->net_h = -1;
}

static int32_t gmac_tx(void *ctx, const uint8_t *frame, uint16_t len) {
    struct gmac_nic *d = ctx;
    uint32_t i;
    uint32_t spins;
    uint16_t n;
    if (d == NULL || frame == NULL || len < 14u) {
        return -1;
    }
    n = len < 60u ? 60u : len;
    if (n > GMAC_FRAME) {
        return -1;
    }
    i = d->tx_i % GMAC_RING;
    for (spins = 0; spins < 1000000u; spins++) {
        if ((d->txd[i].des3 & TDES3_OWN) == 0u) {
            break;
        }
        pm_cpu_pause();
    }
    if ((d->txd[i].des3 & TDES3_OWN) != 0u) {
        return -1;
    }
    memset(d->txb[i], 0, GMAC_FRAME);
    memcpy(d->txb[i], frame, len);
    d->txd[i].des0 = (uint32_t)(uintptr_t)d->txb[i];
    d->txd[i].des1 = 0;
    d->txd[i].des2 = TDES2_IOC | n;
    pm_cpu_store_fence();
    d->txd[i].des3 = TDES3_OWN | TDES3_FD | TDES3_LD | n;
    pm_cpu_store_fence();
    d->tx_i = (i + 1u) % GMAC_RING;
    wr(DMA_CHAN_TX_END, (uint32_t)(uintptr_t)&d->txd[d->tx_i]);
    return 0;
}

static int32_t gmac_poll(void *ctx) {
    struct gmac_nic *d = ctx;
    uint32_t steps;
    if (d == NULL) {
        return -1;
    }
    for (steps = 0; steps < GMAC_RING; steps++) {
        uint32_t i = d->rx_i % GMAC_RING;
        uint32_t st = d->rxd[i].des3;
        uint16_t n;
        if ((st & RDES3_OWN) != 0u) {
            break;
        }
        n = (uint16_t)(st & 0x7fffu);
        if ((st & RDES3_ES) == 0u && (st & RDES3_LD) != 0u && n >= 14u && n <= GMAC_RXBUF) {
            (void)pm_metal_net_ip_rx_from(d->net_h, d->rxb[i], n);
        }
        gmac_arm_rx(d, i);
        pm_cpu_store_fence();
        d->rx_i = (i + 1u) % GMAC_RING;
        wr(DMA_CHAN_RX_END, (uint32_t)(uintptr_t)&d->rxd[d->rx_i]);
    }
    return 0;
}

#else /* !PM_METAL_GMAC_HW */

static int32_t gmac_open(void *ctx) {
    (void)ctx;
    return -1;
}

static void gmac_close(void *ctx) {
    struct gmac_nic *d = ctx;
    if (d == NULL) {
        return;
    }
    d->used = 0;
    d->dt_id = -1;
    d->net_h = -1;
}

static int32_t gmac_tx(void *ctx, const uint8_t *frame, uint16_t len) {
    (void)ctx;
    (void)frame;
    (void)len;
    return -1;
}

static int32_t gmac_poll(void *ctx) {
    (void)ctx;
    return -1;
}

#endif /* PM_METAL_GMAC_HW */

static void gmac_mac(void *ctx, uint8_t out[6]) {
    struct gmac_nic *d = ctx;
    if (d == NULL || out == NULL) {
        return;
    }
    memcpy(out, d->mac, 6);
}

static int32_t gmac_attach(void) {
    if (s_arena == NULL || s_dev.used) {
        return s_dev.used ? s_dev.net_h : -1;
    }
    memset(&s_dev, 0, sizeof(s_dev));
    s_dev.ops.open = gmac_open;
    s_dev.ops.close = gmac_close;
    s_dev.ops.mac = gmac_mac;
    s_dev.ops.tx = gmac_tx;
    s_dev.ops.poll = gmac_poll;
    s_dev.ops.ctx = &s_dev;
#if !defined(PM_METAL_GMAC_HW)
    return -1;
#else
    s_dev.dt_id = pm_metal_dt_add(PM_METAL_DT_CLASS_NET, "gmac", PM_METAL_DT_BUS_MMIO, 0xffa80000u,
        0, 0, 0);
    if (s_dev.dt_id < 0) {
        return -1;
    }
    s_dev.used = 1;
    s_dev.net_h = pm_metal_drivers_net_bind(s_dev.dt_id, &s_dev.ops);
    if (s_dev.net_h < 0) {
        (void)pm_metal_dt_unbind(s_dev.dt_id);
        s_dev.used = 0;
        return -1;
    }
    return s_dev.net_h;
#endif
}

int32_t pm_metal_drivers_net_gmac_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    memset(&s_dev, 0, sizeof(s_dev));
    return 0;
}

void pm_metal_drivers_net_gmac_deinit(void) {
    memset(&s_dev, 0, sizeof(s_dev));
    s_arena = NULL;
}

int32_t pm_metal_drivers_net_gmac_probe(void) {
    return gmac_attach();
}

int32_t pm_metal_drivers_net_gmac_up(void) {
    if (s_arena == NULL) {
        return -1;
    }
    if (s_dev.used) {
        return 0;
    }
    return gmac_attach() >= 0 ? 0 : -1;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.gmac, pm_metal_drivers_net_gmac_init, pm_metal_drivers_net_gmac_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.gmac, pm_metal_drivers_net_gmac_deinit, pm_metal_drivers_net_gmac_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.gmac, pm_metal_drivers_net_gmac_probe, pm_metal_drivers_net_gmac_probe, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.net.gmac, pm_metal_drivers_net_gmac_up, pm_metal_drivers_net_gmac_up, int32_t(void));

PM_MOD_BOOT_C(pymergetic.metal.drivers.net.gmac, pm_metal_drivers_net_gmac_init, pm_metal_drivers_net_gmac_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.net.gmac, pymergetic.metal.drivers.net);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.net.gmac, pymergetic.metal.net.ip);

static int32_t gmac_drv_attach(int32_t bus, uint32_t loc0, uint32_t loc1, uint32_t loc2,
    uint32_t loc3) {
    (void)bus;
    (void)loc0;
    (void)loc1;
    (void)loc2;
    (void)loc3;
#if !defined(PM_METAL_GMAC_HW)
    return 0;
#else
    return gmac_attach() >= 0 ? 0 : -1;
#endif
}

#include "pymergetic/metal/drivers/__types__.h"

PM_METAL_DRV_PLATFORM_C(pymergetic.metal.drivers.net.gmac, gmac_drv_attach);
