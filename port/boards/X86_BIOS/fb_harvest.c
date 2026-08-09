/* BIOS FB harvest: Bochs/QEMU stdvga + Multiboot LFB (iPXE/QEMU). */
#include "fb_harvest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "io_pci.h"
#include "pymergetic/metal/bus/pci.h"

/* Provided by crt0 on X86_64_BIOS; weak so X86_BIOS can link without them. */
uint32_t metal_boot_magic __attribute__((weak));
uint32_t metal_boot_info __attribute__((weak));

#define VBE_DISPI_IOPORT_INDEX 0x01CEu
#define VBE_DISPI_IOPORT_DATA  0x01CFu

static void vbe_write(uint16_t index, uint16_t value)
{
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, value);
}

static uint16_t vbe_read(uint16_t index)
{
    uint16_t val;
    outw(VBE_DISPI_IOPORT_INDEX, index);
    __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"((uint16_t)VBE_DISPI_IOPORT_DATA));
    return val;
}

static int harvest_bochs(pm_metal_scanout_bind_t *out)
{
    uint8_t bus, dev, func;
    uint64_t bar;
    uint16_t id;

    if (pm_metal_bus_pci_find(0x1234u, 0x1111u, &bus, &dev, &func) != 0) {
        return -1;
    }
    bar = pm_metal_bus_pci_bar_mmio(bus, dev, func, 0, NULL);
    if (bar == 0) {
        return -1;
    }
    pm_metal_bus_pci_enable_mem_bm(bus, dev, func);

    vbe_write(0, 0xB0C5); /* INDEX_ID */
    id = vbe_read(0);
    if (id < 0xB0C0u || id > 0xB0C6u) {
        return -1;
    }

    vbe_write(4, 0); /* disable */
    vbe_write(1, 1024);
    vbe_write(2, 768);
    vbe_write(3, 32);
    vbe_write(4, 0x41); /* ENABLED | LFB */

    memset(out, 0, sizeof(*out));
    out->fb = (uint32_t *)(uintptr_t)bar;
    out->fb_ppsl = 1024;
    out->mode_w = 1024;
    out->mode_h = 768;
    out->owned = 1;
    out->gop = NULL;
    return 0;
}

static int harvest_multiboot(pm_metal_scanout_bind_t *out)
{
    const uint32_t *info;
    uint32_t flags;
    uint64_t addr;
    uint32_t pitch;
    uint32_t w;
    uint32_t h;
    uint8_t bpp;

    if (metal_boot_magic != 0x2BADB002u || metal_boot_info == 0u) {
        return -1;
    }
    info = (const uint32_t *)(uintptr_t)metal_boot_info;
    flags = info[0];
    if ((flags & (1u << 12)) == 0u) {
        return -1;
    }
    /* Multiboot1 framebuffer fields at offset 88. */
    addr = *(const uint64_t *)(const void *)(info + 22);
    pitch = info[24];
    w = info[25];
    h = info[26];
    bpp = (uint8_t)(info[27] & 0xffu);
    if (addr == 0 || w < 320u || h < 200u || bpp != 32u || pitch < w * 4u) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->fb = (uint32_t *)(uintptr_t)addr;
    out->fb_ppsl = pitch / 4u;
    out->mode_w = w;
    out->mode_h = h;
    out->owned = 1;
    out->gop = NULL;
    return 0;
}

int pm_metal_bios_fb_harvest(pm_metal_scanout_bind_t *out)
{
    if (out == NULL) {
        return -1;
    }
    if (harvest_bochs(out) == 0) {
        return 0;
    }
    return harvest_multiboot(out);
}
