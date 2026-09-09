/* ARMV7_QEMU (virt) — HW fill, then one pm_metal_boot(). Same prove shape
 * as the BIOS board: virtio-net-pci behind the ECAM window, the upy CDN
 * autoexec, isa-debug-exit... no — the virt machine has no isa bus; the
 * prove ends with a PSCI system-off (virt has PSCI 0.2+). Serial is the
 * marker channel, QEMU exits when the guest asks PSCI SYSTEM_OFF. */
#include "pymergetic/metal/boot.h"
#include "pymergetic/metal/drivers.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/wasmmod/io.h"
#include "ports/freestanding/io_ops.h"
#include "extmod/metal/port/upy/firmware_upy.h"

#include <stdint.h>
#include <string.h>

void uart_init(void);
void uart_puts(const char *s);

extern char __pm_metal_image_base[] __attribute__((weak));
extern char __pm_metal_image_end[] __attribute__((weak));

const char *pm_metal_boot_fill_seat(void) {
    return "armv7qemu";
}

void pm_metal_boot_fill_avoid(uint64_t *lo, uint64_t *hi) {
    /* Nothing to avoid: the whole 1GiB below the image is free RAM, and
     * the image itself is the only thing loaded. Avoid just the image. */
    if (lo != NULL) {
        *lo = (uint64_t)(uintptr_t)__pm_metal_image_base;
    }
    if (hi != NULL) {
        *hi = (uint64_t)(uintptr_t)__pm_metal_image_end;
    }
}

void pm_metal_boot_fill_io(void) {
    pm_wasmmod_host_io_ops_init();
    pm_wasmmod_io_set(&pm_wasmmod_host_io_ops);
}

int pm_metal_boot_fill_kernel(uint64_t *base, uint64_t *len) {
    uint64_t lo = (uint64_t)(uintptr_t)__pm_metal_image_base;
    uint64_t hi = (uint64_t)(uintptr_t)__pm_metal_image_end;
    if (base == NULL || len == NULL || hi <= lo) {
        return -1;
    }
    *base = lo;
    *len = hi - lo;
    return 0;
}

static void psci_system_off(void) {
    /* PSCI 0.2 SYSTEM_OFF (0x84000008) via HVC — the virt machine's PSCI
     * node says the conduit is hvc. The raw .inst bypasses clang's
     * armv7ve-only check on the hvc mnemonic (the TCG cortex-a7 traps it
     * to QEMU's PSCI dispatcher regardless). Never returns. */
    register uint32_t r0 __asm__("r0") = 0x84000008u;
    __asm__ volatile(".inst 0xe1400070" : : "r"(r0) : "memory");
    for (;;) {
        __asm__ volatile("wfi");
    }
}

static void fail(const char *why) {
    uart_puts(why);
    psci_system_off();
}

static void prove_arm(void) {
    int32_t h0;
    int32_t h1;
    uint8_t frame[64];
    uint8_t mac[6];

    h0 = pm_metal_drivers_net_by_compat("virtio-net", 0);
    h1 = pm_metal_drivers_net_by_compat("virtio-net", 1);
    if (h0 < 0 || h1 < 0) {
        fail("virtio-net ecam");
    }
    if (pm_metal_drivers_unbind(pm_metal_drivers_net_dt_id(h1)) != 0) {
        fail("unbind nic1");
    }
    if (pm_metal_drivers_net_by_compat("virtio-net", 1) >= 0) {
        fail("nic1 gone");
    }
    if (pm_metal_drivers_net_by_compat("virtio-net", 0) != h0) {
        fail("nic0");
    }
    memset(frame, 0, sizeof(frame));
    memset(frame, 0xff, 6);
    pm_metal_drivers_net_mac(h0, mac);
    memcpy(frame + 6, mac, 6);
    frame[12] = 0x08;
    frame[13] = 0x00;
    if (pm_metal_drivers_net_tx(h0, frame, (uint16_t)sizeof(frame)) != 0) {
        fail("tx nic0");
    }
    (void)pm_metal_drivers_net_poll(h0);
}

void pm_metal_bios_main(uint32_t magic, void *mb_info) {
    (void)magic;
    (void)mb_info;
    uart_init();
    uart_puts("metal ARMV7_QEMU");
    /* QEMU virt RAM: 1GiB at 0x40000000 (the -m 1024 geometry; the memory
     * node in the machine DTB). Same feed the RV1106 board does for its
     * DRAM — firmware has no memmap provider here. */
    if (pm_metal_boot_feed_span(0x40000000u, 1024u * 1024u * 1024u) != 0) {
        fail("feed");
    }
    if (pm_metal_boot() != 0) {
        fail("boot");
    }
    prove_arm();
    if (pm_metal_firmware_upy() != 0) {
        fail("upy");
    }
    psci_system_off();
}
