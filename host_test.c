/* Host prove for metal: dt / bus / drivers / async / net. No µPy. */
#include "pymergetic/metal/async.h"
#include "pymergetic/metal/bus/pci.h"
#include "pymergetic/metal/bus/virtio.h"
#include "pymergetic/metal/drivers.h"
#include "pymergetic/metal/drivers/blk.h"
#include "pymergetic/metal/drivers/blk/ide.h"
#include "pymergetic/metal/drivers/blk/virtio.h"
#include "pymergetic/metal/drivers/net.h"
#include "pymergetic/metal/drivers/net/bge.h"
#include "pymergetic/metal/drivers/net/sim.h"
#include "pymergetic/metal/drivers/net/tap.h"
#include "pymergetic/metal/drivers/net/virtio.h"
#include "pymergetic/metal/drivers/rtc.h"
#include "pymergetic/metal/drivers/rtc/cmos.h"
#include "pymergetic/metal/drivers/rtc/sim.h"
#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/fw/memmap.h"
#include "pymergetic/metal/net/dhcp.h"
#include "pymergetic/metal/net/dns.h"
#include "pymergetic/metal/net/http.h"
#include "pymergetic/metal/net/http/asgi.h"
#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/net/ntp.h"
#include "pymergetic/metal/net/ssh.h"
#include "pymergetic/metal/net/tftp.h"
#include "pymergetic/metal/net/tls.h"
#include "pymergetic/metal/net/wg.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/boot.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int32_t pm_metal_dt_tests(void);
int32_t pm_metal_bus_pci_tests(void);
int32_t pm_metal_bus_virtio_tests(void);
int32_t pm_metal_drivers_tests(void);
int32_t pm_metal_drivers_probe_tests(void);
int32_t pm_metal_drivers_net_tests(void);
int32_t pm_metal_fw_memmap_tests(void);
int32_t pm_metal_drivers_rtc_sim_tests(void);
int32_t pm_metal_drivers_rtc_cmos_tests(void);
int32_t pm_metal_drivers_blk_virtio_tests(void);
int32_t pm_metal_drivers_blk_ide_tests(void);
int32_t pm_metal_async_tests(void);
int32_t pm_metal_net_ip_tests(void);
int32_t pm_metal_net_tls_tests(void);
int32_t pm_metal_net_http_tests(void);
int32_t pm_metal_net_http_asgi_tests(void);
int32_t pm_metal_drivers_net_tap_tests(void);
int32_t pm_metal_drivers_net_bge_tests(void);
int32_t pm_metal_net_dns_tests(void);
int32_t pm_metal_drivers_net_sim_tests(void);
int32_t pm_metal_drivers_net_virtio_tests(void);
int32_t pm_metal_net_wg_tests(void);
int32_t pm_metal_net_ntp_tests(void);
int32_t pm_metal_net_dhcp_tests(void);
int32_t pm_metal_net_tftp_tests(void);
int32_t pm_metal_net_ssh_tests(void);

static void teardown(pm_util_mem_arena_t *arena, void *backing) {
    pm_mod_boot_unwind();
    pm_util_mem_arena_destroy(arena);
    free(backing);
}

static int32_t late_boot_init(pm_util_mem_arena_t *a) {
    (void)a;
    return 0;
}

static void late_boot_deinit(void) {}

int main(void) {
    enum { SPAN = 2u * 1024u * 1024u };
    void *backing = malloc(SPAN);
    if (backing == NULL) {
        fprintf(stderr, "metal.async host: malloc\n");
        return 1;
    }
    pm_util_mem_arena_t *arena = pm_util_mem_arena_create(backing, SPAN);
    if (arena == NULL) {
        fprintf(stderr, "metal.async host: arena_create\n");
        free(backing);
        return 1;
    }
    if (pm_mod_boot_run(arena) != 0) {
        fprintf(stderr, "metal.host: boot\n");
        teardown(arena, backing);
        return 1;
    }
    {
        static const pm_mod_boot_t late = {
            "pymergetic.test.lateboot", late_boot_init, late_boot_deinit, NULL
        };
        if (pm_mod_boot_add(&late) != 0) {
            fprintf(stderr, "metal.host: late boot add\n");
            teardown(arena, backing);
            return 1;
        }
    }
    int32_t st = pm_metal_dt_tests();
    if (st == 0) {
        st = pm_metal_bus_pci_tests();
    }
    if (st == 0) {
        st = pm_metal_bus_virtio_tests();
    }
    if (st == 0) {
        st = pm_metal_drivers_tests();
    }
    if (st == 0) {
        st = pm_metal_drivers_net_tests();
    }
    if (st == 0) {
        st = pm_metal_fw_memmap_tests();
    }
    if (st == 0) {
        st = pm_metal_drivers_rtc_sim_tests();
    }
    if (st == 0) {
        st = pm_metal_drivers_rtc_cmos_tests();
    }
    if (st == 0) {
        st = pm_metal_drivers_blk_virtio_tests();
    }
    if (st == 0) {
        st = pm_metal_drivers_blk_ide_tests();
    }
    if (st == 0) {
        st = pm_metal_async_tests();
    }
    if (st == 0) {
        st = pm_metal_net_ip_tests();
    }
    if (st == 0) {
        st = pm_metal_net_tls_tests();
    }
    if (st == 0) {
        st = pm_metal_net_http_tests();
    }
    if (st == 0) {
        st = pm_metal_net_http_asgi_tests();
    }
    if (st == 0) {
        st = pm_metal_drivers_net_tap_tests();
    }
    if (st == 0) {
        st = pm_metal_net_dns_tests();
    }
    if (st == 0) {
        st = pm_metal_drivers_net_sim_tests();
    }
    if (st == 0) {
        st = pm_metal_drivers_net_virtio_tests();
    }
    if (st == 0) {
        st = pm_metal_net_wg_tests();
    }
    if (st == 0) {
        st = pm_metal_net_ntp_tests();
    }
    if (st == 0) {
        st = pm_metal_net_dhcp_tests();
    }
    if (st == 0) {
        st = pm_metal_net_tftp_tests();
    }
    if (st == 0) {
        st = pm_metal_net_ssh_tests();
    }
    if (st == 0) {
        st = pm_metal_drivers_net_bge_tests();
    }
    if (st == 0) {
        st = pm_metal_drivers_probe_tests();
    }
    teardown(arena, backing);
    if (st != 0) {
        return 1;
    }
    puts("metal dt+bus+drivers+blk+rtc+async+ip+tls+http+asgi+tap+dns+sim+virtio+wg+ntp+dhcp+tftp+ssh+bge host tests ok");
    return 0;
}
