/*
 * Shared port-I/O convenience wrappers over pm_metal_boot_io_ops().
 * Platform ops tables live in bios/efi io_port.c.
 */
#if !defined(__wasm__)

#include <stdint.h>

#include <pymergetic/metal/boot/platform/io.h>

void pm_metal_boot_outb(uint16_t port, uint8_t val)
{
  pm_metal_boot_io_ops()->outb(port, val);
}

uint8_t pm_metal_boot_inb(uint16_t port)
{
  return pm_metal_boot_io_ops()->inb(port);
}

void pm_metal_boot_out16(uint16_t port, uint16_t val)
{
  pm_metal_boot_io_ops()->out16(port, val);
}

uint16_t pm_metal_boot_in16(uint16_t port)
{
  return pm_metal_boot_io_ops()->in16(port);
}

void pm_metal_boot_out32(uint16_t port, uint32_t val)
{
  pm_metal_boot_io_ops()->out32(port, val);
}

uint32_t pm_metal_boot_in32(uint16_t port)
{
  return pm_metal_boot_io_ops()->in32(port);
}

#endif /* !__wasm__ */
