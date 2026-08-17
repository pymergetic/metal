/* Luckfox work LED — GPIO3 pin 22 (PC6) at 0xff550000. */
#include <stdint.h>

#define GPIO3_BASE ((volatile uint32_t *)(uintptr_t)0xff550000u)
#define GPIO_SWPORT_DR_H 1
#define GPIO_SWPORT_DDR_H 3
#define LED_BIT 6u
#define HIWORD_MASK (1u << (16u + LED_BIT))
#define BIT (1u << LED_BIT)

static int g_ready;

void led_init(void) {
    GPIO3_BASE[GPIO_SWPORT_DDR_H] = HIWORD_MASK | BIT;
    g_ready = 1;
}

void led_set(int on) {
    uint32_t dr;
    if (!g_ready) {
        led_init();
    }
    dr = HIWORD_MASK;
    if (on) {
        dr |= BIT;
    }
    GPIO3_BASE[GPIO_SWPORT_DR_H] = dr;
}
