/* Port I/O for BIOS floor (COM1, QEMU debug-exit, KBC reset). */
#ifndef PM_METAL_BOOT_BIOS_IO_H_
#define PM_METAL_BOOT_BIOS_IO_H_

#include <stdint.h>

static inline void pm_metal_bios_outb(uint16_t port, uint8_t val)
{
  __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t pm_metal_bios_inb(uint16_t port)
{
  uint8_t val;

  __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
  return val;
}

static inline void pm_metal_bios_outw(uint16_t port, uint16_t val)
{
  __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

#endif /* PM_METAL_BOOT_BIOS_IO_H_ */
