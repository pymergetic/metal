/* Freestanding board clock — software µs advanced by delay / poll. */
#include "pymergetic/metal/async/board_time.h"

#include <stdint.h>

static uint64_t g_mono_us;

uint64_t pm_metal_board_mono_us(void)
{
    return g_mono_us;
}

void pm_metal_board_time_advance_us(uint64_t us)
{
    g_mono_us += us;
}

#if defined(__x86_64__) || defined(__i386__) || defined(__i686__)

static void cpu_pause(void)
{
    __asm__ volatile("pause");
}

static uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void outb(uint16_t port, uint8_t v)
{
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

/* 8254 channel 2 — same recipe as boot/platform/bios/time.c (TSC cal). */
static void pit_delay_ms(uint32_t ms)
{
    while (ms--) {
        uint32_t count = 1193u; /* ~1ms @ 1.193182 MHz */
        uint32_t spins = 0;

        outb(0x61u, (uint8_t)((inb(0x61u) & (uint8_t)~0x02u) | 0x01u));
        outb(0x43u, 0xB0u);
        outb(0x42u, (uint8_t)(count & 0xffu));
        outb(0x42u, (uint8_t)(count >> 8));
        while ((inb(0x61u) & 0x20u) == 0u) {
            if (++spins > 2000000u) {
                return;
            }
            cpu_pause();
        }
    }
}

void pm_metal_board_sleep_us(uint64_t us)
{
    /* Round up to whole ms — PIT path is ms-granular. */
    uint32_t ms = (uint32_t)((us + 999ull) / 1000ull);
    if (ms == 0u) {
        return;
    }
    pit_delay_ms(ms);
}

#else

void pm_metal_board_sleep_us(uint64_t us)
{
    /* Non-x86 freestanding: crude spin; mono advanced by time_sleep. */
    volatile uint64_t spins = us * 10ull;
    while (spins--) {
    }
}

#endif
