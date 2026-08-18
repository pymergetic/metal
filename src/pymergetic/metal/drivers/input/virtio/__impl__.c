/* pymergetic.metal.drivers.input.virtio — PCI 1af4:1052; virtio-input events. */
#include "pymergetic/metal/drivers/input/virtio/__exports__.h"

#include "pymergetic/metal/bus/pci.h"
#include "pymergetic/metal/bus/virtio.h"
#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/__types__.h"
#include "pymergetic/metal/drivers/input.h"
#include "pymergetic/metal/input.h"
#include "pymergetic/util/mem.h"

#if defined(PM_METAL_FIRMWARE)
#include "pm_cpu.h"
#endif

#include <string.h>

#define IN_FILL_MAX 4u
#define EV_SYN 0
#define EV_KEY 1
#define EV_ABS 3
#define KEY_LEFTSHIFT 42
#define KEY_RIGHTSHIFT 54
#define KEY_F1 59
#define KEY_F6 64
#define BTN_LEFT 0x110
#define BTN_TOUCH 0x14a
#define ABS_X 0
#define ABS_Y 1

struct in_fill {
    uint32_t used;
    int32_t dt_id;
    int32_t in_h;
    pm_metal_input_ops_t ops;
    uint32_t shift;
    int32_t abs_x;
    int32_t abs_y;
    uint32_t btn;
#if defined(PM_METAL_FIRMWARE)
    volatile uint8_t *common;
    volatile uint8_t *notify;
    uint32_t notify_mult;
    uint16_t nqoff;
    uint16_t last_used;
    uint8_t *vqmem;
    uint8_t *evbuf;
#endif
};

static pm_util_mem_arena_t *s_arena;
static struct in_fill s_dev[IN_FILL_MAX];

static int32_t map_key(uint32_t code, uint32_t shift) {
    static const uint8_t letter[] = {
        [16] = 'q', [17] = 'w', [18] = 'e', [19] = 'r', [20] = 't', [21] = 'y', [22] = 'u',
        [23] = 'i', [24] = 'o', [25] = 'p', [30] = 'a', [31] = 's', [32] = 'd', [33] = 'f',
        [34] = 'g', [35] = 'h', [36] = 'j', [37] = 'k', [38] = 'l', [44] = 'z', [45] = 'x',
        [46] = 'c', [47] = 'v', [48] = 'b', [49] = 'n', [50] = 'm',
    };
    static const uint8_t digit[] = {
        [2] = '1', [3] = '2', [4] = '3', [5] = '4', [6] = '5', [7] = '6', [8] = '7', [9] = '8',
        [10] = '9', [11] = '0',
    };
    static const uint8_t digit_s[] = {
        [2] = '!', [3] = '@', [4] = '#', [5] = '$', [6] = '%', [7] = '^', [8] = '&', [9] = '*',
        [10] = '(', [11] = ')',
    };
    if (code >= KEY_F1 && code <= KEY_F6) {
        return PM_METAL_INPUT_KEY_F1 + (int32_t)(code - KEY_F1);
    }
    if (code == 28u) {
        return (int32_t)'\n';
    }
    if (code == 14u) {
        return (int32_t)'\b';
    }
    if (code == 15u) {
        return (int32_t)'\t';
    }
    if (code == 57u) {
        return (int32_t)' ';
    }
    if (code < sizeof(letter) && letter[code] != 0) {
        uint8_t c = letter[code];
        if (shift) {
            c = (uint8_t)(c - 32u);
        }
        return (int32_t)c;
    }
    if (code < sizeof(digit) && digit[code] != 0) {
        return (int32_t)(shift ? digit_s[code] : digit[code]);
    }
    return -1;
}

static int32_t apply_event(struct in_fill *d, int32_t type, int32_t code, int32_t value) {
    if (d == NULL) {
        return -1;
    }
    if (type == EV_ABS) {
        if (code == ABS_X) {
            d->abs_x = value;
        } else if (code == ABS_Y) {
            d->abs_y = value;
        }
        return 0;
    }
    if (type != EV_KEY) {
        return type == EV_SYN ? 0 : -1;
    }
    if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
        d->shift = value != 0 ? 1u : 0u;
        return 0;
    }
    if (code == (int32_t)BTN_LEFT || code == (int32_t)BTN_TOUCH) {
        d->btn = value != 0 ? 1u : 0u;
        return 0;
    }
    if (value == 0) {
        return 0;
    }
    {
        int32_t key = map_key((uint32_t)code, d->shift);
        if (key < 0) {
            return 0;
        }
        return pm_metal_input_push(key);
    }
}

static int32_t fill_open(void *ctx) {
    return ctx != NULL ? 0 : -1;
}

static void fill_close(void *ctx) {
    struct in_fill *d = ctx;
    if (d != NULL) {
        d->used = 0;
        d->dt_id = -1;
        d->in_h = -1;
#if defined(PM_METAL_FIRMWARE)
        d->common = NULL;
        d->vqmem = NULL;
        d->evbuf = NULL;
#endif
    }
}

#if defined(PM_METAL_FIRMWARE)
#define FW_IN_QSZ 16u
#define FW_DESC_F_WRITE 2u
#define VIRTIO_PCI_CAP_COMMON 1u
#define VIRTIO_PCI_CAP_NOTIFY 2u
#define VIRTIO_F_VERSION_1 32u

static uint8_t mmio_r8(volatile uint8_t *p) {
    return *p;
}

static void mmio_w8(volatile uint8_t *p, uint8_t v) {
    *p = v;
}

static uint16_t mmio_r16(volatile uint8_t *p) {
    return *(volatile uint16_t *)p;
}

static void mmio_w16(volatile uint8_t *p, uint16_t v) {
    *(volatile uint16_t *)p = v;
}

static uint32_t mmio_r32(volatile uint8_t *p) {
    return *(volatile uint32_t *)p;
}

static void mmio_w32(volatile uint8_t *p, uint32_t v) {
    *(volatile uint32_t *)p = v;
}

static void mmio_w64(volatile uint8_t *p, uint64_t v) {
    mmio_w32(p, (uint32_t)v);
    mmio_w32(p + 4, (uint32_t)(v >> 32));
}

static uint64_t pci_bar(uint32_t bus, uint32_t dev, uint32_t fn, uint32_t bar) {
    uint32_t lo = pm_metal_bus_pci_cfg_read32(bus, dev, fn, 0x10u + bar * 4u);
    uint32_t hi;
    if ((lo & 1u) != 0) {
        return (uint64_t)(lo & 0xfffffffcu);
    }
    if (((lo >> 1) & 3u) == 2u) {
        hi = pm_metal_bus_pci_cfg_read32(bus, dev, fn, 0x10u + (bar + 1u) * 4u);
        return ((uint64_t)hi << 32) | (uint64_t)(lo & 0xfffffff0u);
    }
    return (uint64_t)(lo & 0xfffffff0u);
}

static int32_t virtio_pci_caps(uint32_t bus, uint32_t dev, uint32_t fn, volatile uint8_t **common,
    volatile uint8_t **notify, uint32_t *notify_mult) {
    uint32_t status = pm_metal_bus_pci_cfg_read32(bus, dev, fn, 0x04u);
    uint32_t cap;
    *common = NULL;
    *notify = NULL;
    *notify_mult = 1;
    pm_metal_bus_pci_cfg_write32(bus, dev, fn, 0x04u, status | 0x7u);
    cap = pm_metal_bus_pci_cfg_read32(bus, dev, fn, 0x34u) & 0xffu;
    while (cap != 0 && cap != 0xffu) {
        uint32_t dw0 = pm_metal_bus_pci_cfg_read32(bus, dev, fn, cap);
        uint32_t dw1 = pm_metal_bus_pci_cfg_read32(bus, dev, fn, cap + 4u);
        uint32_t off = pm_metal_bus_pci_cfg_read32(bus, dev, fn, cap + 8u);
        uint8_t id = (uint8_t)dw0;
        uint8_t next = (uint8_t)(dw0 >> 8);
        uint8_t cfg_type;
        uint8_t bar;
        uint64_t base;
        volatile uint8_t *mmio;
        if (id != 0x09u) {
            cap = next;
            continue;
        }
        cfg_type = (uint8_t)(dw0 >> 24);
        bar = (uint8_t)dw1;
        base = pci_bar(bus, dev, fn, bar);
        if (base == 0) {
            cap = next;
            continue;
        }
        mmio = (volatile uint8_t *)(uintptr_t)base + off;
        if (cfg_type == VIRTIO_PCI_CAP_COMMON) {
            *common = mmio;
        } else if (cfg_type == VIRTIO_PCI_CAP_NOTIFY) {
            *notify = mmio;
            *notify_mult = pm_metal_bus_pci_cfg_read32(bus, dev, fn, cap + 16u);
            if (*notify_mult == 0) {
                *notify_mult = 1;
            }
        }
        cap = next;
    }
    return (*common != NULL && *notify != NULL) ? 0 : -1;
}

static void desc_set(uint8_t *de, uint64_t addr, uint32_t len, uint16_t flags, uint16_t next) {
    uint32_t i;
    for (i = 0; i < 8u; i++) {
        de[i] = (uint8_t)(addr >> (8u * i));
    }
    de[8] = (uint8_t)len;
    de[9] = (uint8_t)(len >> 8);
    de[10] = (uint8_t)(len >> 16);
    de[11] = (uint8_t)(len >> 24);
    de[12] = (uint8_t)flags;
    de[13] = (uint8_t)(flags >> 8);
    de[14] = (uint8_t)next;
    de[15] = (uint8_t)(next >> 8);
}

static void requeue(struct in_fill *d, uint16_t id) {
    uint8_t *avail = d->vqmem + 256;
    uint16_t aidx = (uint16_t)(avail[2] | ((uint16_t)avail[3] << 8));
    avail[4 + (aidx % FW_IN_QSZ) * 2u] = (uint8_t)id;
    avail[5 + (aidx % FW_IN_QSZ) * 2u] = (uint8_t)(id >> 8);
    aidx++;
    avail[2] = (uint8_t)aidx;
    avail[3] = (uint8_t)(aidx >> 8);
}

static int32_t fw_poll(struct in_fill *d) {
    uint8_t *used;
    uint16_t uidx;
    int32_t n = 0;
    if (d == NULL || d->vqmem == NULL || d->evbuf == NULL) {
        return 0;
    }
    used = d->vqmem + 512;
    pm_cpu_load_fence();
    uidx = (uint16_t)(used[2] | ((uint16_t)used[3] << 8));
    while (d->last_used != uidx) {
        uint32_t slot = (uint32_t)(d->last_used % FW_IN_QSZ);
        uint8_t *ue = used + 4u + slot * 8u;
        uint16_t id = (uint16_t)(ue[0] | ((uint16_t)ue[1] << 8));
        uint8_t *ev;
        int32_t type;
        int32_t code;
        int32_t value;
        if (id >= FW_IN_QSZ) {
            d->last_used++;
            continue;
        }
        ev = d->evbuf + (uint32_t)id * 8u;
        type = (int32_t)(ev[0] | ((uint16_t)ev[1] << 8));
        code = (int32_t)(ev[2] | ((uint16_t)ev[3] << 8));
        value = (int32_t)((uint32_t)ev[4] | ((uint32_t)ev[5] << 8) | ((uint32_t)ev[6] << 16)
            | ((uint32_t)ev[7] << 24));
        if (apply_event(d, type, code, value) == 0) {
            n++;
        }
        requeue(d, id);
        d->last_used++;
        pm_cpu_store_fence();
        mmio_w16(d->notify + (uint32_t)d->nqoff * d->notify_mult, 0);
    }
    return n;
}
#endif

static int32_t fill_poll(void *ctx) {
    struct in_fill *d = ctx;
    if (d == NULL) {
        return -1;
    }
#if defined(PM_METAL_FIRMWARE)
    if (d->common != NULL) {
        return fw_poll(d);
    }
#endif
    return 0;
}

static int32_t fill_inject(void *ctx, int32_t key) {
    struct in_fill *d = ctx;
    if (d == NULL || key < 0) {
        return -1;
    }
    return pm_metal_input_push(key);
}

static int32_t bind_fill(struct in_fill *d, int32_t dt) {
    memset(d, 0, sizeof(*d));
    d->used = 1;
    d->ops.open = fill_open;
    d->ops.close = fill_close;
    d->ops.poll = fill_poll;
    d->ops.inject = fill_inject;
    d->ops.ctx = d;
    d->dt_id = dt;
    d->in_h = pm_metal_drivers_input_bind(dt, &d->ops);
    if (d->in_h < 0) {
        d->used = 0;
        return -1;
    }
    return d->in_h;
}

static int32_t virtio_attach(int32_t bus, uint32_t loc0, uint32_t loc1, uint32_t loc2, uint32_t loc3) {
    uint32_t i;
    int32_t dt;
    if (s_arena == NULL) {
        return -1;
    }
    dt = pm_metal_dt_add(PM_METAL_DT_CLASS_INPUT, "virtio-input", bus, loc0, loc1, loc2, loc3);
    if (dt < 0) {
        return -1;
    }
    for (i = 0; i < IN_FILL_MAX; i++) {
        if (s_dev[i].used && s_dev[i].dt_id == dt) {
            return s_dev[i].in_h;
        }
    }
    for (i = 0; i < IN_FILL_MAX; i++) {
        if (!s_dev[i].used) {
            return bind_fill(&s_dev[i], dt);
        }
    }
    return -1;
}

#if defined(PM_METAL_FIRMWARE)
static int32_t fw_attach_pci(uint32_t bus, uint32_t dev, uint32_t fn) {
    volatile uint8_t *common;
    volatile uint8_t *notify;
    uint32_t notify_mult;
    uint32_t feat1;
    uint32_t i;
    uint32_t id;
    struct in_fill *d;
    uint8_t *desc;
    uint8_t *avail;
    uint8_t *used;
    uint16_t qsz = (uint16_t)FW_IN_QSZ;
    int32_t dt;
    if (virtio_pci_caps(bus, dev, fn, &common, &notify, &notify_mult) != 0) {
        return -1;
    }
    mmio_w8(common + 20, 0);
    while (mmio_r8(common + 20) != 0) {
        pm_cpu_pause();
    }
    mmio_w8(common + 20, 1u | 2u);
    mmio_w32(common + 0, 1);
    feat1 = mmio_r32(common + 4);
    mmio_w32(common + 8, 0);
    mmio_w32(common + 12, 0);
    mmio_w32(common + 8, 1);
    mmio_w32(common + 12, feat1 & 1u);
    mmio_w8(common + 20, (uint8_t)(1u | 2u | 8u));
    if ((mmio_r8(common + 20) & 8u) == 0) {
        return -1;
    }
    for (i = 0; i < IN_FILL_MAX; i++) {
        if (!s_dev[i].used) {
            break;
        }
    }
    if (i >= IN_FILL_MAX) {
        return -1;
    }
    d = &s_dev[i];
    memset(d, 0, sizeof(*d));
    d->used = 1;
    d->common = common;
    d->notify = notify;
    d->notify_mult = notify_mult;
    d->vqmem = pm_util_mem_memalign(s_arena, 4096, 4096);
    d->evbuf = pm_util_mem_alloc(s_arena, FW_IN_QSZ * 8u);
    if (d->vqmem == NULL || d->evbuf == NULL) {
        d->used = 0;
        return -1;
    }
    memset(d->vqmem, 0, 4096);
    memset(d->evbuf, 0, FW_IN_QSZ * 8u);
    mmio_w16(common + 22, 0);
    mmio_w16(common + 24, qsz);
    if (mmio_r16(common + 24) == 0) {
        d->used = 0;
        return -1;
    }
    desc = d->vqmem;
    avail = d->vqmem + 256;
    used = d->vqmem + 512;
    (void)used;
    mmio_w64(common + 32, (uint64_t)(uintptr_t)desc);
    mmio_w64(common + 40, (uint64_t)(uintptr_t)avail);
    mmio_w64(common + 48, (uint64_t)(uintptr_t)d->vqmem + 512u);
    mmio_w16(common + 28, 1);
    d->nqoff = mmio_r16(common + 30);
    d->last_used = 0;
    for (i = 0; i < FW_IN_QSZ; i++) {
        desc_set(desc + i * 16u, (uint64_t)(uintptr_t)(d->evbuf + i * 8u), 8, FW_DESC_F_WRITE, 0);
        avail[4 + i * 2u] = (uint8_t)i;
        avail[5 + i * 2u] = 0;
    }
    avail[2] = (uint8_t)FW_IN_QSZ;
    avail[3] = 0;
    mmio_w8(common + 20, (uint8_t)(1u | 2u | 4u | 8u));
    pm_cpu_store_fence();
    mmio_w16(notify + (uint32_t)d->nqoff * notify_mult, 0);
    d->ops.open = fill_open;
    d->ops.close = fill_close;
    d->ops.poll = fill_poll;
    d->ops.inject = fill_inject;
    d->ops.ctx = d;
    id = pm_metal_bus_pci_cfg_read32(bus, dev, fn, 0);
    dt = pm_metal_dt_add(PM_METAL_DT_CLASS_INPUT, "virtio-input", PM_METAL_DT_BUS_PCI, bus,
        (dev << 8) | fn, id & 0xffffu, (id >> 16) & 0xffffu);
    if (dt < 0) {
        d->used = 0;
        return -1;
    }
    d->dt_id = dt;
    d->in_h = pm_metal_drivers_input_bind(dt, &d->ops);
    if (d->in_h < 0) {
        d->used = 0;
        return -1;
    }
    return d->in_h;
}
#endif

int32_t pm_metal_drivers_input_virtio_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    memset(s_dev, 0, sizeof(s_dev));
    return 0;
}

void pm_metal_drivers_input_virtio_deinit(void) {
    memset(s_dev, 0, sizeof(s_dev));
    s_arena = NULL;
}

int32_t pm_metal_drivers_input_virtio_probe(void) {
    uint32_t i;
    for (i = 0; i < IN_FILL_MAX; i++) {
        if (!s_dev[i].used) {
            return virtio_attach(PM_METAL_DT_BUS_PLATFORM, 0, 0, 0, i);
        }
    }
    return -1;
}

int32_t pm_metal_drivers_input_virtio_event(int32_t type, int32_t code, int32_t value) {
    uint32_t i;
    int32_t st = -1;
    for (i = 0; i < IN_FILL_MAX; i++) {
        if (!s_dev[i].used) {
            continue;
        }
        if (apply_event(&s_dev[i], type, code, value) == 0) {
            st = 0;
        }
    }
    return st;
}

int32_t pm_metal_drivers_input_virtio_abs(int32_t *x, int32_t *y) {
    uint32_t i;
    for (i = 0; i < IN_FILL_MAX; i++) {
        if (!s_dev[i].used) {
            continue;
        }
        if (x != NULL) {
            *x = s_dev[i].abs_x;
        }
        if (y != NULL) {
            *y = s_dev[i].abs_y;
        }
        return 0;
    }
    return -1;
}

int32_t pm_metal_drivers_input_virtio_up(void) {
    uint32_t i;
    if (s_arena == NULL) {
        return -1;
    }
    for (i = 0; i < IN_FILL_MAX; i++) {
        if (s_dev[i].used) {
            return 0;
        }
    }
    return pm_metal_drivers_input_virtio_probe() >= 0 ? 0 : -1;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.drivers.input.virtio, pm_metal_drivers_input_virtio_init, pm_metal_drivers_input_virtio_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.input.virtio, pm_metal_drivers_input_virtio_deinit, pm_metal_drivers_input_virtio_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.input.virtio, pm_metal_drivers_input_virtio_probe, pm_metal_drivers_input_virtio_probe, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.input.virtio, pm_metal_drivers_input_virtio_event, pm_metal_drivers_input_virtio_event, int32_t(int32_t, int32_t, int32_t));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.input.virtio, pm_metal_drivers_input_virtio_abs, pm_metal_drivers_input_virtio_abs, int32_t(int32_t *, int32_t *));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.input.virtio, pm_metal_drivers_input_virtio_up, pm_metal_drivers_input_virtio_up, int32_t(void));

PM_MOD_BOOT_C(pymergetic.metal.drivers.input.virtio, pm_metal_drivers_input_virtio_init, pm_metal_drivers_input_virtio_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.input.virtio, pymergetic.metal.drivers.input);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.input.virtio, pymergetic.metal.bus.virtio);

static int32_t virtio_drv_attach(int32_t bus, uint32_t loc0, uint32_t loc1, uint32_t loc2,
    uint32_t loc3) {
    (void)loc2;
    (void)loc3;
#if defined(PM_METAL_FIRMWARE)
    if (bus == PM_METAL_DT_BUS_PCI) {
        return fw_attach_pci(loc0, (loc1 >> 8) & 0x1fu, loc1 & 0x7u) >= 0 ? 0 : -1;
    }
#endif
    return virtio_attach(bus, loc0, loc1, loc2, loc3) >= 0 ? 0 : -1;
}

PM_METAL_DRV_PCI_C(pymergetic.metal.drivers.input.virtio, PM_METAL_BUS_VIRTIO_VENDOR,
    PM_METAL_BUS_VIRTIO_DEV_INPUT, virtio_drv_attach);
