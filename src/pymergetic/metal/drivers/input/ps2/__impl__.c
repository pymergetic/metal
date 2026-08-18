/* pymergetic.metal.drivers.input.ps2 — i8042 data 0x60; US set-1 on firmware. */
#include "pymergetic/metal/drivers/input/ps2/__exports__.h"

#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/__types__.h"
#include "pymergetic/metal/drivers/input.h"
#include "pymergetic/metal/input.h"

#include <string.h>

static pm_util_mem_arena_t *s_arena;
static uint32_t s_used;
static int32_t s_dt;
static int32_t s_h;
static pm_metal_input_ops_t s_ops;

#if defined(PM_METAL_FIRMWARE) && (defined(__i386__) || defined(__x86_64__))
/* AT set-1 make codes → US ASCII. 0 means ignore (break / prefix / unused). */
static const uint8_t s_set1[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5', [0x07] = '6',
    [0x08] = '7', [0x09] = '8', [0x0a] = '9', [0x0b] = '0', [0x0e] = '\b', [0x0f] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't', [0x15] = 'y',
    [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p', [0x1c] = '\n', [0x1e] = 'a',
    [0x1f] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g', [0x23] = 'h', [0x24] = 'j',
    [0x25] = 'k', [0x26] = 'l', [0x2c] = 'z', [0x2d] = 'x', [0x2e] = 'c', [0x2f] = 'v',
    [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x39] = ' ',
};

static inline uint8_t ps2_inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static int32_t ps2_poll(void *ctx) {
    uint8_t st;
    uint8_t sc;
    int32_t key;
    (void)ctx;
    st = ps2_inb(0x64u);
    if ((st & 1u) == 0u) {
        return 0;
    }
    sc = ps2_inb(0x60u);
    if (sc == 0xe0u || sc == 0xe1u || (sc & 0x80u) != 0u) {
        return 0;
    }
    if (sc >= 0x3bu && sc <= 0x40u) {
        return pm_metal_input_push(PM_METAL_INPUT_KEY_F1 + (int32_t)(sc - 0x3bu));
    }
    key = (int32_t)s_set1[sc & 0x7fu];
    if (key == 0) {
        return 0;
    }
    return pm_metal_input_push(key);
}
#else
static int32_t ps2_poll(void *ctx) {
    (void)ctx;
    return 0;
}
#endif

static int32_t ps2_open(void *ctx) {
    (void)ctx;
    return 0;
}

static void ps2_close(void *ctx) {
    (void)ctx;
    s_used = 0;
    s_dt = -1;
    s_h = -1;
}

static int32_t ps2_inject(void *ctx, int32_t key) {
    (void)ctx;
    if (key < 0) {
        return -1;
    }
    return pm_metal_input_push(key);
}

int32_t pm_metal_drivers_input_ps2_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    s_used = 0;
    s_dt = -1;
    s_h = -1;
    memset(&s_ops, 0, sizeof(s_ops));
    s_ops.open = ps2_open;
    s_ops.close = ps2_close;
    s_ops.poll = ps2_poll;
    s_ops.inject = ps2_inject;
    return 0;
}

void pm_metal_drivers_input_ps2_deinit(void) {
    s_used = 0;
    s_arena = NULL;
}

int32_t pm_metal_drivers_input_ps2_probe(void) {
    if (s_arena == NULL) {
        return -1;
    }
    if (s_used) {
        return s_h;
    }
    s_dt = pm_metal_dt_add(PM_METAL_DT_CLASS_INPUT, "ps2", PM_METAL_DT_BUS_ISA, 0x60u, 0, 0, 0);
    if (s_dt < 0) {
        return -1;
    }
    s_ops.ctx = NULL;
    s_h = pm_metal_drivers_input_bind(s_dt, &s_ops);
    if (s_h < 0) {
        (void)pm_metal_dt_unbind(s_dt);
        return -1;
    }
    s_used = 1;
    return s_h;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.drivers.input.ps2, pm_metal_drivers_input_ps2_init, pm_metal_drivers_input_ps2_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.input.ps2, pm_metal_drivers_input_ps2_deinit, pm_metal_drivers_input_ps2_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.input.ps2, pm_metal_drivers_input_ps2_probe, pm_metal_drivers_input_ps2_probe, int32_t(void));

PM_MOD_BOOT_C(pymergetic.metal.drivers.input.ps2, pm_metal_drivers_input_ps2_init, pm_metal_drivers_input_ps2_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.input.ps2, pymergetic.metal.drivers.input);

static int32_t ps2_drv_attach(int32_t bus, uint32_t loc0, uint32_t loc1, uint32_t loc2,
    uint32_t loc3) {
    (void)bus;
    (void)loc0;
    (void)loc1;
    (void)loc2;
    (void)loc3;
    return pm_metal_drivers_input_ps2_probe() >= 0 ? 0 : -1;
}

PM_METAL_DRV_ISA_C(pymergetic.metal.drivers.input.ps2, 0x60u, ps2_drv_attach);
