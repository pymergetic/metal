/*
 * ACPI RSDP/MADT — cpu_count + APIC ids for SMP bringup.
 */
#include "pymergetic/metal/dev/acpi/__init__.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/reg/mod.h>

/* RegMod declare (C SoT) — loaded via pm_metal_dev_acpi_reg_load. */
static pm_metal_reg_export_t dev_acpi_exports[] = {
    PM_METAL_REG_EXPORT(set_rsdp),
    PM_METAL_REG_EXPORT(rsdp),
    PM_METAL_REG_EXPORT(init),
    PM_METAL_REG_EXPORT(cpu_count),
    PM_METAL_REG_EXPORT(apic_id),
    PM_METAL_REG_EXPORT(lapic_base),
};
PM_METAL_REG_REF(dev_acpi, set_rsdp, 0);
PM_METAL_REG_REF(dev_acpi, rsdp, 1);
PM_METAL_REG_REF(dev_acpi, init, 2);
PM_METAL_REG_REF(dev_acpi, cpu_count, 3);
PM_METAL_REG_REF(dev_acpi, apic_id, 4);
PM_METAL_REG_REF(dev_acpi, lapic_base, 5);
PM_METAL_REG_MOD(dev_acpi, "pymergetic.metal.dev.acpi")

static int32_t dev_acpi_register_symbols(void *ctx)
{
    (void)ctx;
    pm_metal_reg_export_publish(dev_acpi_set_rsdp, (void *)pm_metal_dev_acpi_set_rsdp);
    pm_metal_reg_export_publish(dev_acpi_rsdp, (void *)pm_metal_dev_acpi_rsdp);
    pm_metal_reg_export_publish(dev_acpi_init, (void *)pm_metal_dev_acpi_init);
    pm_metal_reg_export_publish(dev_acpi_cpu_count, (void *)pm_metal_dev_acpi_cpu_count);
    pm_metal_reg_export_publish(dev_acpi_apic_id, (void *)pm_metal_dev_acpi_apic_id);
    pm_metal_reg_export_publish(dev_acpi_lapic_base, (void *)pm_metal_dev_acpi_lapic_base);
    return 0;
}

#ifndef PM_METAL_ACPI_MAX_CPU
#define PM_METAL_ACPI_MAX_CPU 8
#endif

static uint64_t g_rsdp;
static uint64_t g_lapic_base = 0xFEE00000ull;
static uint32_t g_cpu_count;
static uint32_t g_apic_ids[PM_METAL_ACPI_MAX_CPU];
static int g_inited;

void pm_metal_dev_acpi_set_rsdp(uint64_t addr)
{
    g_rsdp = addr;
}

uint64_t pm_metal_dev_acpi_rsdp(void)
{
    return g_rsdp;
}

uint64_t pm_metal_dev_acpi_lapic_base(void)
{
    return g_lapic_base;
}

uint32_t pm_metal_dev_acpi_cpu_count(void)
{
    return g_cpu_count;
}

uint32_t pm_metal_dev_acpi_apic_id(uint32_t cpu_index)
{
    if (cpu_index >= g_cpu_count) {
        return 0xffffffffu;
    }
    return g_apic_ids[cpu_index];
}

static int checksum_ok(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    uint8_t s = 0;

    for (i = 0; i < n; i++) {
        s = (uint8_t)(s + p[i]);
    }
    return s == 0;
}

static uint64_t scan_rsdp_range(uint64_t start, uint64_t len)
{
    uint64_t addr;

    for (addr = start; addr + 20 <= start + len; addr += 16) {
        const uint8_t *p = (const uint8_t *)(uintptr_t)addr;
        if (p[0] != 'R' || p[1] != 'S' || p[2] != 'D' || p[3] != ' ' ||
            p[4] != 'P' || p[5] != 'T' || p[6] != 'R' || p[7] != ' ') {
            continue;
        }
        if (!checksum_ok(p, 20)) {
            continue;
        }
        if (p[15] >= 2) {
            uint32_t len20 = (uint32_t)p[20] | ((uint32_t)p[21] << 8) |
                             ((uint32_t)p[22] << 16) | ((uint32_t)p[23] << 24);
            if (len20 >= 36 && checksum_ok(p, len20)) {
                return addr;
            }
        }
        return addr;
    }
    return 0;
}

static uint64_t find_rsdp(void)
{
    uint16_t ebda_seg;
    uint64_t ebda;
    uint64_t found;

    if (g_rsdp != 0) {
        return g_rsdp;
    }
    ebda_seg = *(volatile uint16_t *)(uintptr_t)0x40Eu;
    ebda = (uint64_t)ebda_seg << 4;
    if (ebda != 0) {
        found = scan_rsdp_range(ebda, 1024);
        if (found != 0) {
            return found;
        }
    }
    return scan_rsdp_range(0xE0000ull, 0x20000ull);
}

static void add_cpu(uint32_t apic_id)
{
    uint32_t i;

    if (g_cpu_count >= PM_METAL_ACPI_MAX_CPU) {
        return;
    }
    for (i = 0; i < g_cpu_count; i++) {
        if (g_apic_ids[i] == apic_id) {
            return;
        }
    }
    g_apic_ids[g_cpu_count++] = apic_id;
}

static int parse_madt(const uint8_t *madt, uint32_t len)
{
    uint32_t off;
    uint32_t lapic32;

    if (len < 44) {
        return -1;
    }
    lapic32 = (uint32_t)madt[36] | ((uint32_t)madt[37] << 8) |
              ((uint32_t)madt[38] << 16) | ((uint32_t)madt[39] << 24);
    if (lapic32 != 0) {
        g_lapic_base = (uint64_t)lapic32;
    }
    off = 44;
    while (off + 2 <= len) {
        uint8_t type = madt[off];
        uint8_t entlen = madt[off + 1];

        if (entlen < 2 || off + entlen > len) {
            break;
        }
        if (type == 0 && entlen >= 8) {
            /* Processor Local APIC */
            uint8_t apic_id = madt[off + 3];
            uint32_t flags = (uint32_t)madt[off + 4] | ((uint32_t)madt[off + 5] << 8) |
                             ((uint32_t)madt[off + 6] << 16) | ((uint32_t)madt[off + 7] << 24);
            if (flags & 1u) {
                add_cpu(apic_id);
            }
        } else if (type == 9 && entlen >= 16) {
            /* Processor Local x2APIC */
            uint32_t apic_id = (uint32_t)madt[off + 4] | ((uint32_t)madt[off + 5] << 8) |
                               ((uint32_t)madt[off + 6] << 16) | ((uint32_t)madt[off + 7] << 24);
            uint32_t flags = (uint32_t)madt[off + 8] | ((uint32_t)madt[off + 9] << 8) |
                             ((uint32_t)madt[off + 10] << 16) | ((uint32_t)madt[off + 11] << 24);
            if (flags & 1u) {
                add_cpu(apic_id);
            }
        } else if (type == 5 && entlen >= 12) {
            uint64_t ovr = (uint64_t)madt[off + 4] | ((uint64_t)madt[off + 5] << 8) |
                           ((uint64_t)madt[off + 6] << 16) | ((uint64_t)madt[off + 7] << 24) |
                           ((uint64_t)madt[off + 8] << 32) | ((uint64_t)madt[off + 9] << 40) |
                           ((uint64_t)madt[off + 10] << 48) | ((uint64_t)madt[off + 11] << 56);
            if (ovr != 0) {
                g_lapic_base = ovr;
            }
        }
        off += entlen;
    }
    return g_cpu_count > 0 ? 0 : -1;
}

static const uint8_t *find_sdt(const uint8_t *rsdp, const char *sig)
{
    uint8_t rev = rsdp[15];
    uint32_t i;
    uint32_t n;
    uint64_t ptrs[64];

    n = 0;
    if (rev >= 2) {
        uint64_t xsdt = (uint64_t)rsdp[24] | ((uint64_t)rsdp[25] << 8) |
                        ((uint64_t)rsdp[26] << 16) | ((uint64_t)rsdp[27] << 24) |
                        ((uint64_t)rsdp[28] << 32) | ((uint64_t)rsdp[29] << 40) |
                        ((uint64_t)rsdp[30] << 48) | ((uint64_t)rsdp[31] << 56);
        const uint8_t *x = (const uint8_t *)(uintptr_t)xsdt;
        uint32_t xlen;

        if (xsdt == 0 || x[0] != 'X' || x[1] != 'S' || x[2] != 'D' || x[3] != 'T') {
            goto rsdt;
        }
        xlen = (uint32_t)x[4] | ((uint32_t)x[5] << 8) | ((uint32_t)x[6] << 16) |
               ((uint32_t)x[7] << 24);
        if (xlen < 36 || !checksum_ok(x, xlen)) {
            goto rsdt;
        }
        for (i = 36; i + 8 <= xlen && n < 64; i += 8) {
            ptrs[n++] = (uint64_t)x[i] | ((uint64_t)x[i + 1] << 8) |
                        ((uint64_t)x[i + 2] << 16) | ((uint64_t)x[i + 3] << 24) |
                        ((uint64_t)x[i + 4] << 32) | ((uint64_t)x[i + 5] << 40) |
                        ((uint64_t)x[i + 6] << 48) | ((uint64_t)x[i + 7] << 56);
        }
    } else {
    rsdt:;
        {
            uint32_t rsdt32 = (uint32_t)rsdp[16] | ((uint32_t)rsdp[17] << 8) |
                              ((uint32_t)rsdp[18] << 16) | ((uint32_t)rsdp[19] << 24);
            const uint8_t *r = (const uint8_t *)(uintptr_t)rsdt32;
            uint32_t rlen;

            if (rsdt32 == 0 || r[0] != 'R' || r[1] != 'S' || r[2] != 'D' || r[3] != 'T') {
                return NULL;
            }
            rlen = (uint32_t)r[4] | ((uint32_t)r[5] << 8) | ((uint32_t)r[6] << 16) |
                   ((uint32_t)r[7] << 24);
            if (rlen < 36 || !checksum_ok(r, rlen)) {
                return NULL;
            }
            for (i = 36; i + 4 <= rlen && n < 64; i += 4) {
                ptrs[n++] = (uint32_t)r[i] | ((uint32_t)r[i + 1] << 8) |
                            ((uint32_t)r[i + 2] << 16) | ((uint32_t)r[i + 3] << 24);
            }
        }
    }
    for (i = 0; i < n; i++) {
        const uint8_t *t = (const uint8_t *)(uintptr_t)ptrs[i];
        if (t[0] == (uint8_t)sig[0] && t[1] == (uint8_t)sig[1] &&
            t[2] == (uint8_t)sig[2] && t[3] == (uint8_t)sig[3]) {
            uint32_t tlen = (uint32_t)t[4] | ((uint32_t)t[5] << 8) |
                            ((uint32_t)t[6] << 16) | ((uint32_t)t[7] << 24);
            if (tlen >= 36 && checksum_ok(t, tlen)) {
                return t;
            }
        }
    }
    return NULL;
}

static void order_bsp_first(void)
{
    uint32_t bsp;
    uint32_t i;
    uint32_t eax, ebx, ecx, edx;

    if (g_cpu_count < 2) {
        return;
    }
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(1u), "c"(0u));
    bsp = (ebx >> 24) & 0xffu;
    for (i = 0; i < g_cpu_count; i++) {
        if (g_apic_ids[i] == bsp) {
            uint32_t tmp = g_apic_ids[0];
            g_apic_ids[0] = g_apic_ids[i];
            g_apic_ids[i] = tmp;
            return;
        }
    }
}

int32_t pm_metal_dev_acpi_init(void)
{
    uint64_t rsdp_a;
    const uint8_t *rsdp;
    const uint8_t *madt;
    uint32_t madt_len;

    if (g_inited) {
        return g_cpu_count > 0 ? 0 : -1;
    }
    g_cpu_count = 0;
    g_lapic_base = 0xFEE00000ull;
    memset(g_apic_ids, 0, sizeof(g_apic_ids));

    rsdp_a = find_rsdp();
    if (rsdp_a == 0) {
        g_inited = 1;
        return -1;
    }
    g_rsdp = rsdp_a;
    rsdp = (const uint8_t *)(uintptr_t)rsdp_a;
    madt = find_sdt(rsdp, "APIC");
    if (madt == NULL) {
        g_inited = 1;
        return -1;
    }
    madt_len = (uint32_t)madt[4] | ((uint32_t)madt[5] << 8) |
               ((uint32_t)madt[6] << 16) | ((uint32_t)madt[7] << 24);
    if (parse_madt(madt, madt_len) != 0) {
        g_inited = 1;
        return -1;
    }
    order_bsp_first();
    g_inited = 1;
    return 0;
}
