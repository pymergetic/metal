#if defined(PM_METAL_BOOT_TARGET_BIOS)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_EFI)
#error "boot/bios/uart.c built with PM_METAL_BOOT_TARGET_EFI"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

#include <stddef.h>
#include <stdint.h>

#include <pymergetic/metal/boot/platform/uart.h>

#include "io.h"

#define COM1_BASE 0x3F8u

static int g_com1_ready;

static void com1_init(void)
{
  /* 115200 baud, 8N1, FIFO on — classic PC COM1. */
  pm_metal_bios_outb(COM1_BASE + 1u, 0x00);
  pm_metal_bios_outb(COM1_BASE + 3u, 0x80);
  pm_metal_bios_outb(COM1_BASE + 0u, 0x01);
  pm_metal_bios_outb(COM1_BASE + 1u, 0x00);
  pm_metal_bios_outb(COM1_BASE + 3u, 0x03);
  pm_metal_bios_outb(COM1_BASE + 2u, 0xC7);
  pm_metal_bios_outb(COM1_BASE + 4u, 0x0B);
  g_com1_ready = 1;
}

static void com1_putc(char c)
{
  uint32_t spins;

  if (!g_com1_ready) {
    com1_init();
  }
  for (spins = 0; spins < 100000u; spins++) {
    if ((pm_metal_bios_inb(COM1_BASE + 5u) & 0x20u) != 0u) {
      break;
    }
  }
  pm_metal_bios_outb(COM1_BASE, (uint8_t)c);
}

static void bios_uart_write(const char *s, size_t n)
{
  size_t i;

  if (s == NULL || n == 0) {
    return;
  }
  if (!g_com1_ready) {
    com1_init();
  }
  for (i = 0; i < n; i++) {
    char c = s[i];

    if (c == '\n') {
      com1_putc('\r');
    }
    com1_putc(c);
  }
}

static const pm_metal_boot_uart_ops_t g_ops = {
  .write = bios_uart_write,
};

const pm_metal_boot_uart_ops_t *pm_metal_boot_uart_ops(void)
{
  return &g_ops;
}
