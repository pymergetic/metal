/* pymergetic.metal.fs — add then read a path; import a tiny FAT12. */
#include "pymergetic/metal/drivers/blk.h"
#include "pymergetic/metal/drivers/blk/virtio.h"
#include "pymergetic/metal/fs.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.fs test: %s\n", why);
    return 1;
}

int32_t pm_metal_fs_tests(void) {
    const uint8_t body[] = { 'h', 'i' };
    uint8_t out[8];
    uint32_t n = sizeof(out);
    uint32_t len = 0;
    if (pm_metal_fs_add("/hi.txt", body, 2) < 0) {
        return fail("add");
    }
    if (pm_metal_fs_stat("/hi.txt", &len) != 0 || len != 2u) {
        return fail("stat");
    }
    if (pm_metal_fs_read("/hi.txt", out, &n) != 0 || n != 2u || out[0] != 'h' || out[1] != 'i') {
        return fail("read");
    }
    n = sizeof(out);
    if (pm_metal_fs_read("/nope", out, &n) == 0) {
        return fail("missing");
    }
    {
        uint8_t big[8192];
        uint8_t got[8192];
        uint32_t gn = sizeof(got);
        memset(big, 0x5a, sizeof(big));
        if (pm_metal_fs_add("/metal/big.bin", big, sizeof(big)) < 0) {
            return fail("add big");
        }
        if (pm_metal_fs_read("/metal/big.bin", got, &gn) != 0 || gn != sizeof(big)
            || got[8191] != 0x5a) {
            return fail("read big");
        }
    }
    {
        char path[96];
        uint32_t i;
        const uint8_t one[] = { 1 };
        uint8_t got[1];
        uint32_t gn = 1;
        path[0] = '/';
        for (i = 1; i < 80u; i++) {
            path[i] = 'a';
        }
        path[80] = 0;
        if (pm_metal_fs_add(path, one, 1) < 0) {
            return fail("add long name");
        }
        if (pm_metal_fs_read(path, got, &gn) != 0 || gn != 1u || got[0] != 1) {
            return fail("read long name");
        }
    }
    if (pm_metal_fs_up() != 0) {
        return fail("up");
    }
    {
        uint8_t got[16];
        uint32_t gn = sizeof(got);
        if (pm_metal_fs_read("/metal/hello.txt", got, &gn) != 0 || gn < 8u || got[0] != 'm') {
            return fail("embed");
        }
    }
    {
        uint8_t img[16u * 512u];
        uint8_t got[8];
        uint32_t gn = sizeof(got);
        int32_t h;
        memset(img, 0, sizeof(img));
        img[0] = 0xeb;
        img[1] = 0x3c;
        img[2] = 0x90;
        memcpy(img + 3, "MSDOS5.0", 8);
        img[11] = 0x00;
        img[12] = 0x02;
        img[13] = 1;
        img[14] = 1;
        img[15] = 0;
        img[16] = 1;
        img[17] = 16;
        img[18] = 0;
        img[19] = 16;
        img[20] = 0;
        img[21] = 0xf8;
        img[22] = 1;
        img[23] = 0;
        img[510] = 0x55;
        img[511] = 0xaa;
        img[512] = 0xf8;
        memset(img + 513, 0xff, 16);
        memcpy(img + 1024, "HI      TXT", 11);
        img[1024 + 11] = 0x20;
        img[1024 + 26] = 2;
        img[1024 + 28] = 3;
        memcpy(img + 1024 + 32, "SUB        ", 11);
        img[1024 + 32 + 11] = 0x10;
        img[1024 + 32 + 26] = 3;
        {
            static const uint8_t pos[] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
            static const char lfn[] = "hello.txt";
            static const char s83[] = "HELLO   TXT";
            uint8_t *e = img + 1024 + 64;
            uint8_t sum = 0;
            uint32_t i;
            for (i = 0; i < 11u; i++) {
                sum = (uint8_t)(((sum & 1u) ? 0x80u : 0u) + (sum >> 1) + (uint8_t)s83[i]);
            }
            memset(e, 0xff, 32);
            e[0] = 0x41;
            e[11] = 0x0f;
            e[12] = 0;
            e[13] = sum;
            e[26] = 0;
            e[27] = 0;
            for (i = 0; i < 13u; i++) {
                uint16_t u = 0xffffu;
                if (i < sizeof(lfn) - 1u) {
                    u = (uint8_t)lfn[i];
                } else if (i == sizeof(lfn) - 1u) {
                    u = 0;
                }
                e[pos[i]] = (uint8_t)u;
                e[pos[i] + 1u] = (uint8_t)(u >> 8);
            }
            memcpy(img + 1024 + 96, s83, 11);
            img[1024 + 96 + 11] = 0x20;
            img[1024 + 96 + 26] = 5;
            img[1024 + 96 + 28] = 3;
        }
        img[1536] = 'h';
        img[1537] = 'i';
        img[1538] = '\n';
        memcpy(img + 2048, "X       TXT", 11);
        img[2048 + 11] = 0x20;
        img[2048 + 26] = 4;
        img[2048 + 28] = 1;
        img[2560] = 'x';
        img[3072] = 'o';
        img[3073] = 'k';
        img[3074] = '\n';
        h = pm_metal_drivers_blk_virtio_probe(16);
        if (h < 0) {
            return fail("fat probe");
        }
        if (pm_metal_drivers_blk_write(h, 0, img, 16) != 0) {
            return fail("fat write");
        }
        if (pm_metal_fs_import_blk(h) < 3) {
            return fail("import");
        }
        if (pm_metal_fs_read("/esp/HI.TXT", got, &gn) != 0 || gn != 3u || got[0] != 'h'
            || got[2] != '\n') {
            return fail("fat read");
        }
        gn = sizeof(got);
        if (pm_metal_fs_read("/esp/SUB/X.TXT", got, &gn) != 0 || gn != 1u || got[0] != 'x') {
            return fail("fat dir");
        }
        gn = sizeof(got);
        if (pm_metal_fs_read("/esp/hello.txt", got, &gn) != 0 || gn != 3u || got[0] != 'o') {
            return fail("fat lfn");
        }
        if (pm_metal_fs_import_blk(-1) != -1) {
            return fail("import bad");
        }
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.fs, tests, pm_metal_fs_tests);
