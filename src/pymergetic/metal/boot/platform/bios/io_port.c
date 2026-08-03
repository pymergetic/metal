#if defined(PM_METAL_BOOT_TARGET_BIOS)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_EFI)
#error "boot/bios/io_port.c built with PM_METAL_BOOT_TARGET_EFI"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

#include <stdint.h>

#include <pymergetic/metal/boot/platform/io.h>

#include "io.h"

static void bios_outb(uint16_t port, uint8_t val)
{
  pm_metal_bios_outb(port, val);
}

static uint8_t bios_inb(uint16_t port)
{
  return pm_metal_bios_inb(port);
}

static void bios_out16(uint16_t port, uint16_t val)
{
  __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static uint16_t bios_in16(uint16_t port)
{
  uint16_t val;

  __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
  return val;
}

static void bios_out32(uint16_t port, uint32_t val)
{
  __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static uint32_t bios_in32(uint16_t port)
{
  uint32_t val;

  __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
  return val;
}

static const pm_metal_boot_io_ops_t g_ops = {
  .outb = bios_outb,
  .inb = bios_inb,
  .out16 = bios_out16,
  .in16 = bios_in16,
  .out32 = bios_out32,
  .in32 = bios_in32,
};

const pm_metal_boot_io_ops_t *pm_metal_boot_io_ops(void)
{
  return &g_ops;
}
