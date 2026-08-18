/* pymergetic.metal.bus.virtio — net vs blk IDs. */
#include "pymergetic/metal/bus/virtio.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "metal.bus.virtio test: %s\n", why);
    return 1;
}

int32_t pm_metal_bus_virtio_tests(void) {
    uint32_t regs[32];
    if (pm_metal_bus_virtio_init(NULL) != -1) {
        return fail("init null");
    }
    if (!pm_metal_bus_virtio_is_net(0x1af4u, 0x1041u) || pm_metal_bus_virtio_is_net(0x1af4u, 0x1042u)) {
        return fail("is_net");
    }
    if (!pm_metal_bus_virtio_is_blk(0x1af4u, 0x1042u) || pm_metal_bus_virtio_is_blk(0x1af4u, 0x1041u)) {
        return fail("is_blk");
    }
    if (!pm_metal_bus_virtio_is_gpu(0x1af4u, 0x1050u) || pm_metal_bus_virtio_is_gpu(0x1af4u, 0x1041u)) {
        return fail("is_gpu");
    }
    if (!pm_metal_bus_virtio_is_input(0x1af4u, 0x1052u)
        || pm_metal_bus_virtio_is_input(0x1af4u, 0x1050u)) {
        return fail("is_input");
    }
    memset(regs, 0, sizeof(regs));
    regs[0] = 0x74726976u;
    regs[2] = 1u;
    if (!pm_metal_bus_virtio_mmio_net_ok(regs)) {
        return fail("mmio net");
    }
    regs[2] = 2u;
    if (pm_metal_bus_virtio_mmio_net_ok(regs)) {
        return fail("mmio blk");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.bus.virtio, tests, pm_metal_bus_virtio_tests);
