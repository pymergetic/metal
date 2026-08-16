/* pymergetic.metal.fw.memmap — two ranges in dt. */
#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/fw/memmap.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void put_test_u64(uint8_t *p, uint64_t v) {
    uint32_t i;
    for (i = 0; i < 8u; i++) {
        p[i] = (uint8_t)(v >> (8u * i));
    }
}

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.fw.memmap test: %s\n", why);
    return 1;
}

int32_t pm_metal_fw_memmap_tests(void) {
    int32_t a;
    int32_t b;
    if (pm_metal_fw_memmap_init(NULL) != -1) {
        return fail("init null");
    }
    a = pm_metal_fw_memmap_add(0x100000u, 0, 0x100000u, 0);
    b = pm_metal_fw_memmap_add(0x200000u, 0, 0x80000u, 0);
    if (a < 0 || b < 0 || a == b) {
        return fail("add two");
    }
    if (pm_metal_fw_memmap_count() != 2) {
        return fail("count");
    }
    if (pm_metal_dt_class(a) != PM_METAL_DT_CLASS_MEM) {
        return fail("class");
    }
    {
        uint8_t mmap[24];
        memset(mmap, 0, sizeof(mmap));
        mmap[0] = 20;
        mmap[4] = 0x00;
        mmap[5] = 0x00;
        mmap[6] = 0x10;
        mmap[12] = 0x00;
        mmap[13] = 0x00;
        mmap[14] = 0x20;
        mmap[20] = 1;
        if (pm_metal_fw_memmap_load_mmap(mmap, sizeof(mmap)) != 0) {
            return fail("load mmap");
        }
        if (pm_metal_fw_memmap_count() != 3) {
            return fail("count after mmap");
        }
        if (pm_metal_fw_memmap_load_mb(0, NULL) != -1) {
            return fail("mb null");
        }
    }
    {
        uint8_t mmap[24];
        uint64_t base = 0;
        uint64_t len = 0;
        memset(mmap, 0, sizeof(mmap));
        mmap[0] = 20;
        mmap[4] = 0x00;
        mmap[5] = 0x00;
        mmap[6] = 0x10;
        mmap[12] = 0x00;
        mmap[13] = 0x00;
        mmap[14] = 0x40;
        mmap[20] = 1;
        if (pm_metal_fw_memmap_feed_mmap(mmap, sizeof(mmap)) != 0) {
            return fail("feed mmap");
        }
        if (pm_metal_fw_memmap_pick(0, 0, 0x100000u, &base, &len) != 0) {
            return fail("pick");
        }
        if (base != 0x100000u || len != 0x100000u) {
            return fail("pick span");
        }
        if (pm_metal_fw_memmap_pick(0x100000u, 0x140000u, 0, &base, &len) != 0) {
            return fail("pick avoid");
        }
        if (base != 0x140000u) {
            return fail("pick after avoid");
        }
    }
    {
        uint8_t efi[40u * 48u];
        uint32_t i;
        memset(efi, 0, sizeof(efi));
        for (i = 0; i < 40u; i++) {
            uint8_t *d = efi + i * 48u;
            d[0] = 7;
            put_test_u64(d + 8, 0x200000ull + (uint64_t)i * 0x10000ull);
            put_test_u64(d + 24, 8);
        }
        if (pm_metal_fw_memmap_feed_efi(efi, 48u, sizeof(efi)) != 0) {
            return fail("feed efi many");
        }
        if (pm_metal_fw_memmap_load_efi(efi, 48u, sizeof(efi)) != 0) {
            return fail("load efi many");
        }
        if (pm_metal_fw_memmap_count() < 3) {
            return fail("count after efi");
        }
    }
    return 0;
}
