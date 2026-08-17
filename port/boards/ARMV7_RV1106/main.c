/* Luckfox Pico Max — DRAM fill, then one pm_metal_boot(). LED is the live signal. */
#include "pymergetic/metal/boot.h"
#include "extmod/metal/port/upy/firmware_upy.h"

#include <stdint.h>

void led_init(void);
void led_set(int on);
void uart_init(void);

extern char __pm_metal_image_base[] __attribute__((weak));
extern char __pm_metal_image_end[] __attribute__((weak));

#ifndef PM_METAL_RV1106_DRAM_LEN
#define PM_METAL_RV1106_DRAM_LEN (256u * 1024u * 1024u)
#endif

static void led_wait(void) {
    uint32_t n = 250000000u;
    __asm__ volatile("1:\n\tsubs %0, %0, #1\n\tbne 1b" : "+r"(n) : : "cc");
}

static void led_loop(void) {
    int on = 1;
    for (;;) {
        led_set(on);
        led_wait();
        on = !on;
    }
}

const char *pm_metal_boot_fill_seat(void) {
    return "rv1106";
}

void pm_metal_boot_fill_avoid(uint64_t *lo, uint64_t *hi) {
    if (lo != NULL) {
        *lo = (uint64_t)(uintptr_t)__pm_metal_image_base;
    }
    if (hi != NULL) {
        *hi = (uint64_t)(uintptr_t)__pm_metal_image_end;
    }
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

void pm_metal_bios_main(uint32_t magic, void *mb_info) {
    (void)magic;
    (void)mb_info;
    uart_init();
    led_init();
    if (pm_metal_boot_feed_span(0, PM_METAL_RV1106_DRAM_LEN) != 0 || pm_metal_boot() != 0) {
        led_loop();
    }
    (void)pm_metal_firmware_upy();
    led_loop();
}
