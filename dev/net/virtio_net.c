#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/bus/virtio.h"
#include "pymergetic/metal/dev/net.h"
#include "pymergetic/metal/mem.h"

#define VNET_RX      0
#define VNET_TX      1
#define VNET_QSZ     16
#define VNET_MTU     1514
#define VNET_RX_BUFS 16
#define VNET_TX_BUFS 4

#pragma pack(1)
/* With VIRTIO_NET_F_MRG_RXBUF negotiated — 12-byte hdr. */
typedef struct {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    uint16_t num_buffers;
} vnet_hdr_t;
#pragma pack()

#define VNET_FRAME (VNET_MTU + (uint32_t)sizeof(vnet_hdr_t))

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} vnet_desc_t;

static pm_metal_virtio_dev_t m_dev;
static int32_t m_ready;
static uint8_t m_mac[6];
static uint8_t *m_rx_bufs[VNET_RX_BUFS];
static uint8_t m_tx_scratch[VNET_TX_BUFS][VNET_FRAME];
static uint8_t m_tx_busy[VNET_TX_BUFS];
static uint32_t m_tx_free_count;

static void vnet_tx_reap(void)
{
    uint16_t head;
    uint32_t ulen;

    while (pm_metal_virtq_get_used(&m_dev.vqs[VNET_TX], &head, &ulen)) {
        vnet_desc_t *desc;
        uint8_t *buf;
        uint32_t i;

        (void)ulen;
        desc = (vnet_desc_t *)m_dev.vqs[VNET_TX].desc;
        buf = (uint8_t *)(uintptr_t)desc[head].addr;
        if (buf >= m_tx_scratch[0] && buf < m_tx_scratch[0] + (VNET_TX_BUFS * VNET_FRAME)) {
            i = (uint32_t)((buf - m_tx_scratch[0]) / VNET_FRAME);
            if (i < VNET_TX_BUFS && m_tx_busy[i]) {
                m_tx_busy[i] = 0;
                m_tx_free_count++;
            }
        }
        pm_metal_virtq_free_chain(&m_dev.vqs[VNET_TX], head);
    }
}

static int32_t vnet_tx_alloc(uint32_t *idx_out)
{
    uint32_t i;

    if (idx_out == NULL) {
        return -1;
    }
    vnet_tx_reap();
    if (m_tx_free_count == 0) {
        return -1;
    }
    for (i = 0; i < VNET_TX_BUFS; i++) {
        if (!m_tx_busy[i]) {
            m_tx_busy[i] = 1;
            m_tx_free_count--;
            *idx_out = i;
            return 0;
        }
    }
    return -1;
}

static int vnet_negotiate_features(uint64_t *feats_out)
{
    uint64_t feats;

    feats = pm_metal_virtio_get_features(&m_dev);
    feats &= PM_METAL_VIRTIO_F_VERSION_1 | PM_METAL_VIRTIO_F_MAC |
             PM_METAL_VIRTIO_NET_F_MRG_RXBUF;
    if (pm_metal_virtio_set_features(&m_dev, feats) != 0) {
        pm_metal_virtio_set_status(&m_dev, 0);
        pm_metal_virtio_set_status(&m_dev,
                                   (uint8_t)(PM_METAL_VIRTIO_S_ACK | PM_METAL_VIRTIO_S_DRIVER));
        if (pm_metal_virtio_set_features(&m_dev, PM_METAL_VIRTIO_F_MAC) != 0) {
            return -1;
        }
        feats = PM_METAL_VIRTIO_F_MAC;
    }
    if (feats_out != NULL) {
        *feats_out = feats;
    }
    return 0;
}

static void vnet_read_mac(uint64_t feats)
{
    int mac_ok = 0;
    uintptr_t i;

    if ((feats & PM_METAL_VIRTIO_F_MAC) != 0 &&
        pm_metal_virtio_cfg_read(&m_dev, 0, m_mac, 6) == 0 &&
        (m_mac[0] & 0x01u) == 0u) {
        for (i = 0; i < 6; i++) {
            if (m_mac[i] != 0u) {
                mac_ok = 1;
                break;
            }
        }
    }

    if (!mac_ok) {
        memset(m_mac, 0x02, 6);
        m_mac[5] = 0x15;
    }
}

int pm_metal_dev_net_virtio_probe(uint8_t mac_out[6])
{
    pm_metal_virtio_dev_t dev;
    uint64_t feats;
    uint8_t mac[6];
    int mac_ok;
    uintptr_t i;

    if (pm_metal_virtio_open(PM_METAL_VIRTIO_DEV_NET, &dev) != 0 &&
        pm_metal_virtio_open(PM_METAL_VIRTIO_DEV_NET_LEGACY, &dev) != 0) {
        return -1;
    }

    feats = pm_metal_virtio_get_features(&dev);
    feats &= PM_METAL_VIRTIO_F_VERSION_1 | PM_METAL_VIRTIO_F_MAC;
    if (pm_metal_virtio_set_features(&dev, feats) != 0) {
        pm_metal_virtio_set_status(&dev, 0);
        pm_metal_virtio_set_status(&dev,
                                   (uint8_t)(PM_METAL_VIRTIO_S_ACK | PM_METAL_VIRTIO_S_DRIVER));
        if (pm_metal_virtio_set_features(&dev, PM_METAL_VIRTIO_F_MAC) != 0) {
            pm_metal_virtio_close(&dev);
            return -1;
        }
        feats = PM_METAL_VIRTIO_F_MAC;
    }

    mac_ok = 0;
    if ((feats & PM_METAL_VIRTIO_F_MAC) != 0 &&
        pm_metal_virtio_cfg_read(&dev, 0, mac, 6) == 0 &&
        (mac[0] & 0x01u) == 0u) {
        for (i = 0; i < 6; i++) {
            if (mac[i] != 0u) {
                mac_ok = 1;
                break;
            }
        }
    }

    if (mac_out != NULL) {
        if (mac_ok) {
            memcpy(mac_out, mac, 6);
        } else {
            memset(mac_out, 0, 6);
        }
    }

    pm_metal_virtio_close(&dev);
    return 0;
}

int pm_metal_dev_net_virtio_open(uint8_t mac_out[6])
{
    uint64_t feats;
    uint32_t i;

    if (m_ready) {
        if (mac_out != NULL) {
            memcpy(mac_out, m_mac, 6);
        }
        return 0;
    }

    if (pm_metal_virtio_open(PM_METAL_VIRTIO_DEV_NET, &m_dev) != 0 &&
        pm_metal_virtio_open(PM_METAL_VIRTIO_DEV_NET_LEGACY, &m_dev) != 0) {
        return -1;
    }

    if (vnet_negotiate_features(&feats) != 0) {
        pm_metal_virtio_close(&m_dev);
        return -1;
    }
    vnet_read_mac(feats);

    if (pm_metal_virtio_setup_queue(&m_dev, VNET_RX, VNET_QSZ) != 0 ||
        pm_metal_virtio_setup_queue(&m_dev, VNET_TX, VNET_QSZ) != 0) {
        pm_metal_virtio_close(&m_dev);
        return -1;
    }

    for (i = 0; i < VNET_RX_BUFS; i++) {
        m_rx_bufs[i] = pm_metal_virtio_pages_alloc(
            PM_METAL_VIRTIO_SIZE_TO_PAGES(sizeof(vnet_hdr_t) + VNET_MTU));
        if (m_rx_bufs[i] == NULL) {
            uint32_t j;
            for (j = 0; j < i; j++) {
                pm_metal_virtio_pages_free(
                    m_rx_bufs[j], PM_METAL_VIRTIO_SIZE_TO_PAGES(sizeof(vnet_hdr_t) + VNET_MTU));
                m_rx_bufs[j] = NULL;
            }
            pm_metal_virtio_close(&m_dev);
            return -1;
        }
        memset(m_rx_bufs[i], 0, sizeof(vnet_hdr_t) + VNET_MTU);
        if (pm_metal_virtq_add(
                &m_dev.vqs[VNET_RX], m_rx_bufs[i], sizeof(vnet_hdr_t) + VNET_MTU, 1, NULL) != 0) {
            uint32_t j;
            pm_metal_virtio_pages_free(
                m_rx_bufs[i], PM_METAL_VIRTIO_SIZE_TO_PAGES(sizeof(vnet_hdr_t) + VNET_MTU));
            m_rx_bufs[i] = NULL;
            for (j = 0; j < i; j++) {
                pm_metal_virtio_pages_free(
                    m_rx_bufs[j], PM_METAL_VIRTIO_SIZE_TO_PAGES(sizeof(vnet_hdr_t) + VNET_MTU));
                m_rx_bufs[j] = NULL;
            }
            pm_metal_virtio_close(&m_dev);
            return -1;
        }
    }

    memset(m_tx_busy, 0, sizeof(m_tx_busy));
    m_tx_free_count = VNET_TX_BUFS;

    pm_metal_virtq_kick(&m_dev, &m_dev.vqs[VNET_RX]);
    (void)pm_metal_virtio_driver_ok(&m_dev);
    m_ready = 1;

    if (mac_out != NULL) {
        memcpy(mac_out, m_mac, 6);
    }
    return 0;
}

int pm_metal_dev_net_virtio_ready(void)
{
    return m_ready ? 1 : 0;
}

const uint8_t *pm_metal_dev_net_virtio_mac(void)
{
    return m_mac;
}

int pm_metal_dev_net_virtio_tx(const void *frame, uint32_t len)
{
    vnet_hdr_t *hdr;
    uint8_t *scratch;
    uint8_t *pkt;
    uint16_t head;
    uint32_t idx;

    if (!m_ready || frame == NULL || len == 0 || len > VNET_MTU) {
        return -1;
    }

    if (vnet_tx_alloc(&idx) != 0) {
        vnet_tx_reap();
        if (vnet_tx_alloc(&idx) != 0) {
            return -1;
        }
    }

    scratch = m_tx_scratch[idx];
    hdr = (vnet_hdr_t *)scratch;
    memset(hdr, 0, sizeof(*hdr));
    hdr->num_buffers = 1;
    pkt = scratch + sizeof(*hdr);
    memcpy(pkt, frame, len);

    if (pm_metal_virtq_add(&m_dev.vqs[VNET_TX], scratch, sizeof(*hdr) + len, 0, &head) != 0) {
        m_tx_busy[idx] = 0;
        m_tx_free_count++;
        return -1;
    }

    pm_metal_virtq_kick(&m_dev, &m_dev.vqs[VNET_TX]);
    return 0;
}

void pm_metal_dev_net_virtio_poll(pm_metal_dev_net_virtio_rx_fn on_frame, void *ctx)
{
    uint16_t head;
    uint32_t len;
    uint8_t *buf;

    if (!m_ready) {
        return;
    }

    while (pm_metal_virtq_get_used(&m_dev.vqs[VNET_RX], &head, &len)) {
        vnet_desc_t *desc;

        desc = (vnet_desc_t *)m_dev.vqs[VNET_RX].desc;
        buf = (uint8_t *)(uintptr_t)desc[head].addr;
        if (on_frame != NULL && len > sizeof(vnet_hdr_t)) {
            on_frame(ctx, buf + sizeof(vnet_hdr_t), len - (uint32_t)sizeof(vnet_hdr_t));
        }

        pm_metal_virtq_free_chain(&m_dev.vqs[VNET_RX], head);
        (void)pm_metal_virtq_add(
            &m_dev.vqs[VNET_RX], buf, sizeof(vnet_hdr_t) + VNET_MTU, 1, NULL);
    }

    pm_metal_virtq_kick(&m_dev, &m_dev.vqs[VNET_RX]);
    vnet_tx_reap();
}

int pm_metal_dev_net_virtio_reap_tx(void)
{
    uint32_t before;

    if (!m_ready) {
        return 0;
    }
    before = m_tx_free_count;
    vnet_tx_reap();
    return (m_tx_free_count > before) ? 1 : 0;
}
