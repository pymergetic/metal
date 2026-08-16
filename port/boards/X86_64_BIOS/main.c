/* Freestanding BIOS/UEFI entry — live driver core on Q35 (arena from CLASS_MEM). */
#include "pymergetic/metal/drivers.h"
#include "pymergetic/metal/drivers/blk.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/drivers/rtc.h"
#include "pymergetic/metal/fw/memmap.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/boot.h"
#include "pymergetic/wasmmod/io.h"
#include "ports/metal/io_ops.h"
#include "extmod/metal/port/upy/firmware_upy.h"

#include <stdint.h>
#include <string.h>

void uart_init(void);
void uart_puts(const char *s);

#define FW_ARENA_SPAN (48u * 1024u * 1024u)

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

static int32_t arena_from_memmap(pm_util_mem_arena_t **out) {
    /* BIOS trampoline page tables live near 1MiB. The largest CLASS_MEM
     * split is often below the 64MiB image and would mmap live CR3.
     * UEFI has no trampoline — avoid only the loaded PE. */
#ifdef PM_METAL_UEFI
    uint64_t avoid_lo = image_lo();
#else
    uint64_t avoid_lo = 0;
#endif
    uint64_t avoid_hi = image_hi();
    uint64_t base = 0;
    uint64_t len = 0;
    if (out == NULL) {
        return -1;
    }
    if (avoid_hi < image_lo()) {
        avoid_hi = image_lo();
    }
    if (pm_metal_fw_memmap_pick(avoid_lo, avoid_hi, FW_ARENA_SPAN, &base, &len) != 0) {
        return -1;
    }
    *out = pm_util_mem_arena_create((void *)(uintptr_t)base, (size_t)len);
    return *out == NULL ? -1 : 0;
}

void pm_metal_bios_main(uint32_t magic, void *mb_info) {
    pm_util_mem_arena_t *arena;
    int32_t h0 = -1;
    int32_t h1 = -1;
    int32_t blk;
    int32_t rtc;
    uint8_t frame[64];
    uint8_t sec[512];
    uint8_t mac[6];

    uart_init();
    uart_puts("metal X86_64_BIOS cake\n");

    if (magic == 0x2BADB002u && mb_info != NULL) {
        if (pm_metal_fw_memmap_feed(magic, mb_info) != 0) {
            fail("feed");
        }
    }
    if (arena_from_memmap(&arena) != 0) {
        fail("arena");
    }
    if (pm_mod_boot_run(arena) != 0) {
        fail("boot");
    }
    pm_wasmmod_metal_io_ops_init();
    pm_wasmmod_io_set(&pm_wasmmod_metal_io_ops);
    if (pm_metal_drivers_probe() != 0) {
        fail("probe");
    }
    if (pm_metal_fw_memmap_count() <= 0) {
        fail("memmap");
    }
    uart_puts("memmap ok\n");

    h0 = pm_metal_drivers_net_by_compat("virtio-net", 0);
    h1 = pm_metal_drivers_net_by_compat("virtio-net", 1);
    if (h0 < 0 || h1 < 0) {
        fail("virtio-net pci");
    }
    if (pm_metal_net_ip_if_up_h(h0, 0x0a000001u) != 0
        || pm_metal_net_ip_if_up_h(h1, 0x0a000002u) != 0) {
        fail("if_up_h");
    }
    uart_puts("nics up\n");

    rtc = pm_metal_drivers_rtc_by_compat("cmos", 0);
    if (rtc < 0 || pm_metal_drivers_rtc_get(rtc) <= 0) {
        fail("cmos");
    }
    uart_puts("cmos ok\n");

    blk = pm_metal_drivers_blk_by_compat("virtio-blk", 0);
    if (blk < 0 || pm_metal_drivers_blk_ready(blk) != 1 || pm_metal_drivers_blk_capacity(blk) == 0) {
        fail("virtio-blk pci");
    }
    memset(sec, 0, sizeof(sec));
    if (pm_metal_drivers_blk_read(blk, 0, sec, 1) != 0 || sec[0] != 'C' || sec[1] != 'A'
        || sec[2] != 'K' || sec[3] != 'E') {
        fail("blk read");
    }
    uart_puts("blk ok\n");

    if (pm_metal_drivers_unbind(pm_metal_drivers_net_dt_id(h1)) != 0) {
        fail("unbind nic1");
    }
    if (pm_metal_drivers_net_count() != 1) {
        fail("nic1 gone");
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
    uart_puts("unbind+tx ok\n");
    pm_metal_firmware_bind_arena(arena);
    pm_metal_set_ready(1);
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
