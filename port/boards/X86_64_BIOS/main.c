/* Freestanding BIOS/UEFI entry — HW fill, then one pm_metal_boot(). */
#include "pymergetic/metal/boot.h"
#include "pymergetic/metal/drivers.h"
#include "pymergetic/metal/drivers/blk.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/drivers/rtc.h"
#include "pymergetic/metal/fw/memmap.h"
#include "pymergetic/wasmmod/io.h"
#include "ports/freestanding/io_ops.h"
#include "extmod/metal/port/upy/firmware_upy.h"

#include <stdint.h>
#include <string.h>

void uart_init(void);
void uart_puts(const char *s);

extern char __pm_metal_image_base[] __attribute__((weak));
extern char __pm_metal_image_end[] __attribute__((weak));
#ifdef PM_METAL_UEFI
extern unsigned char __ImageBase;
#endif

#if defined(__i386__) || defined(__x86_64__)
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
#endif

static void halt_code(uint16_t code) {
    outw(0x501u, code);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

static void fail(const char *why) {
    uart_puts(why);
    uart_puts("\n");
    halt_code(1);
}

static uint64_t image_lo(void) {
#ifdef PM_METAL_UEFI
    return (uint64_t)(uintptr_t)&__ImageBase;
#else
    return (uint64_t)(uintptr_t)__pm_metal_image_base;
#endif
}

static uint64_t image_hi(void) {
#ifdef PM_METAL_UEFI
    const uint8_t *base = (const uint8_t *)&__ImageBase;
    uint32_t e_lfanew;
    uint32_t sz;
    const uint8_t *p;
    e_lfanew = (uint32_t)base[0x3c] | ((uint32_t)base[0x3d] << 8) | ((uint32_t)base[0x3e] << 16)
        | ((uint32_t)base[0x3f] << 24);
    p = base + e_lfanew + 80;
    sz = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return (uint64_t)(uintptr_t)base + sz;
#else
    return (uint64_t)(uintptr_t)__pm_metal_image_end;
#endif
}

const char *pm_metal_boot_fill_seat(void) {
#ifdef PM_METAL_UEFI
    return "uefi";
#else
    return "bios";
#endif
}

void pm_metal_boot_fill_avoid(uint64_t *lo, uint64_t *hi) {
    /* BIOS trampoline page tables live near 1MiB. The largest CLASS_MEM
     * split is often below the 64MiB image and would mmap live CR3.
     * UEFI has no trampoline — avoid only the loaded PE. */
#ifdef PM_METAL_UEFI
    uint64_t avoid_lo = image_lo();
#else
    uint64_t avoid_lo = 0;
#endif
    uint64_t avoid_hi = image_hi();
    if (avoid_hi < image_lo()) {
        avoid_hi = image_lo();
    }
    if (lo != NULL) {
        *lo = avoid_lo;
    }
    if (hi != NULL) {
        *hi = avoid_hi;
    }
}

void pm_metal_boot_fill_io(void) {
    pm_wasmmod_host_io_ops_init();
    pm_wasmmod_io_set(&pm_wasmmod_host_io_ops);
}

int pm_metal_boot_fill_kernel(uint64_t *base, uint64_t *len) {
    uint64_t lo = image_lo();
    uint64_t hi = image_hi();
    if (base == NULL || len == NULL || hi <= lo) {
        return -1;
    }
    *base = lo;
    *len = hi - lo;
    return 0;
}

static void prove_x86(void) {
    int32_t h0;
    int32_t h1;
    int32_t blk;
    int32_t rtc;
    uint8_t frame[64];
    uint8_t sec[512];
    uint8_t mac[6];

    h0 = pm_metal_drivers_net_by_compat("virtio-net", 0);
    h1 = pm_metal_drivers_net_by_compat("virtio-net", 1);
    if (h0 < 0 || h1 < 0) {
        fail("virtio-net pci");
    }
    rtc = pm_metal_drivers_rtc_by_compat("cmos", 0);
    if (rtc < 0 || pm_metal_drivers_rtc_get(rtc) <= 0) {
        fail("cmos");
    }
    blk = pm_metal_drivers_blk_by_compat("virtio-blk", 0);
    if (blk < 0 || pm_metal_drivers_blk_ready(blk) != 1 || pm_metal_drivers_blk_capacity(blk) == 0) {
        fail("virtio-blk pci");
    }
    memset(sec, 0, sizeof(sec));
    if (pm_metal_drivers_blk_read(blk, 0, sec, 1) != 0 || sec[0] != 'M' || sec[1] != 'E'
        || sec[2] != 'T' || sec[3] != 'L') {
        fail("blk read");
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
    uart_init();
#ifdef PM_METAL_UEFI
    uart_puts("metal X86_64_UEFI\n");
#else
    uart_puts("metal X86_64_BIOS\n");
#endif

    if (magic == 0x2BADB002u && mb_info != NULL) {
        if (pm_metal_fw_memmap_feed(magic, mb_info) != 0) {
            fail("feed");
        }
    }
    if (pm_metal_boot() != 0) {
        fail("boot");
    }
    prove_x86();
    if (pm_metal_firmware_upy() != 0) {
        fail("upy");
    }
    halt_code(0);
}

#ifdef PM_METAL_UEFI
#define EFI_BUFFER_TOO_SMALL 0x8000000000000005ull

void pm_metal_uefi_crt_init(void);

typedef uint64_t (*efi_get_memory_map_fn)(uint64_t *size, void *map, uint64_t *key, uint64_t *desc_size,
    uint32_t *desc_ver);

static int32_t efi_feed(void *systab) {
    static uint8_t mapbuf[16u * 1024u];
    uint8_t *bs;
    efi_get_memory_map_fn getmap;
    uint64_t sz;
    uint64_t key;
    uint64_t desc_size;
    uint32_t desc_ver;
    uint64_t st;
    if (systab == NULL) {
        return -1;
    }
    bs = *(uint8_t **)((uint8_t *)systab + 96);
    if (bs == NULL) {
        return -1;
    }
    getmap = *(efi_get_memory_map_fn *)(bs + 56);
    if (getmap == NULL) {
        return -1;
    }
    sz = sizeof(mapbuf);
    st = getmap(&sz, mapbuf, &key, &desc_size, &desc_ver);
    if (st == EFI_BUFFER_TOO_SMALL) {
        return -1;
    }
    if (st != 0 || desc_size < 32u || sz < desc_size) {
        return -1;
    }
    return pm_metal_fw_memmap_feed_efi(mapbuf, (uint32_t)desc_size, (uint32_t)sz);
}

int efi_main(void *image, void *systab) {
    (void)image;
    pm_metal_uefi_crt_init();
    if (efi_feed(systab) != 0) {
        uart_init();
        fail("efi mmap");
    }
    pm_metal_bios_main(0, 0);
    return 0;
}
#endif
