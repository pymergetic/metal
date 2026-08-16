/* pymergetic.metal.drivers.rtc.cmos — CMOS RTC (ports 0x70/0x71) on firmware; time(2) on host. */
#include "pymergetic/metal/drivers/rtc/cmos/__exports__.h"

#include "pymergetic/metal/dt.h"
#include "pymergetic/metal/drivers/rtc.h"

#include <string.h>

#if !defined(PM_METAL_FIRMWARE)
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#endif

static pm_util_mem_arena_t *s_arena;
static uint32_t s_used;
static int32_t s_dt;
static int32_t s_h;
static pm_metal_rtc_ops_t s_ops;

#if defined(PM_METAL_FIRMWARE)
static inline void cmos_outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t cmos_inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static uint8_t cmos_read(uint8_t idx) {
    cmos_outb(0x70u, idx);
    return cmos_inb(0x71u);
}

static uint8_t bcd_bin(uint8_t v, uint8_t binary) {
    if (binary) {
        return v;
    }
    return (uint8_t)((v & 0x0fu) + 10u * ((v >> 4) & 0x0fu));
}

static int64_t cmos_get(void *ctx) {
    uint8_t st_b = cmos_read(0x0bu);
    uint8_t binary = (uint8_t)(st_b & 0x04u);
    uint8_t sec = bcd_bin(cmos_read(0x00u), binary);
    uint8_t min = bcd_bin(cmos_read(0x02u), binary);
    uint8_t hour = bcd_bin(cmos_read(0x04u) & 0x7fu, binary);
    uint8_t day = bcd_bin(cmos_read(0x07u), binary);
    uint8_t mon = bcd_bin(cmos_read(0x08u), binary);
    uint8_t year = bcd_bin(cmos_read(0x09u), binary);
    int32_t y = 2000 + (int32_t)year;
    int32_t mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int32_t i;
    int64_t days;
    (void)ctx;
    if ((y % 4) == 0) {
        mdays[1] = 29;
    }
    days = (int64_t)(y - 1970) * 365 + (int64_t)((y - 1969) / 4);
    for (i = 1; i < (int32_t)mon && i <= 12; i++) {
        days += mdays[i - 1];
    }
    days += (int64_t)day - 1;
    return days * 86400ll + (int64_t)hour * 3600ll + (int64_t)min * 60ll + (int64_t)sec;
}

static int32_t cmos_set(void *ctx, int64_t unix_s) {
    (void)ctx;
    (void)unix_s;
    return -1;
}
#else
static int64_t cmos_get(void *ctx) {
    (void)ctx;
    return (int64_t)time(NULL);
}

static int32_t cmos_set(void *ctx, int64_t unix_s) {
    (void)ctx;
    (void)unix_s;
    return -1;
}
#endif

int32_t pm_metal_drivers_rtc_cmos_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_arena = arena;
    s_used = 0;
    s_dt = -1;
    s_h = -1;
    memset(&s_ops, 0, sizeof(s_ops));
    s_ops.get = cmos_get;
    s_ops.set = cmos_set;
    return 0;
}

void pm_metal_drivers_rtc_cmos_deinit(void) {
    s_used = 0;
    s_arena = NULL;
}

int32_t pm_metal_drivers_rtc_cmos_probe(void) {
    if (s_arena == NULL) {
        return -1;
    }
    if (s_used) {
        return s_h;
    }
    s_dt = pm_metal_dt_add(PM_METAL_DT_CLASS_RTC, "cmos", PM_METAL_DT_BUS_ISA, 0x70u, 0, 0, 0);
    if (s_dt < 0) {
        return -1;
    }
    s_ops.ctx = NULL;
    s_h = pm_metal_drivers_rtc_bind(s_dt, &s_ops);
    if (s_h < 0) {
        (void)pm_metal_dt_unbind(s_dt);
        return -1;
    }
    s_used = 1;
    return s_h;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.drivers.rtc.cmos, pm_metal_drivers_rtc_cmos_init, pm_metal_drivers_rtc_cmos_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.rtc.cmos, pm_metal_drivers_rtc_cmos_deinit, pm_metal_drivers_rtc_cmos_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.drivers.rtc.cmos, pm_metal_drivers_rtc_cmos_probe, pm_metal_drivers_rtc_cmos_probe, int32_t(void));

PM_MOD_BOOT_C(pymergetic.metal.drivers.rtc.cmos, pm_metal_drivers_rtc_cmos_init, pm_metal_drivers_rtc_cmos_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.drivers.rtc.cmos, pymergetic.metal.drivers.rtc);

static int32_t cmos_drv_attach(int32_t bus, uint32_t loc0, uint32_t loc1, uint32_t loc2,
    uint32_t loc3) {
    (void)bus;
    (void)loc0;
    (void)loc1;
    (void)loc2;
    (void)loc3;
    return pm_metal_drivers_rtc_cmos_probe() >= 0 ? 0 : -1;
}

#include "pymergetic/metal/drivers/__types__.h"

PM_METAL_DRV_ISA_C(pymergetic.metal.drivers.rtc.cmos, 0x70u, cmos_drv_attach);
