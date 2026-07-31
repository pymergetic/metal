/** @file
  x86 IN/OUT port I/O — plain compiler intrinsics (inline asm), not EDK2 API,
  so no port-split needed (same two instructions on every x86/x86_64 target
  this repo builds for). Replaces every Library/IoLib IoRead/IoWrite use
  in Metal-side code.

  Internal implementation header; nothing outside this tree's own .c files
  should include it.
**/
#ifndef PM_METAL_RUNTIME_IO_IO_H
#define PM_METAL_RUNTIME_IO_IO_H

#include <stdint.h>

static inline uint8_t pm_metal_io_in8(uint16_t port)
{
  uint8_t v;

  __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
  return v;
}

static inline uint16_t pm_metal_io_in16(uint16_t port)
{
  uint16_t v;

  __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
  return v;
}

static inline uint32_t pm_metal_io_in32(uint16_t port)
{
  uint32_t v;

  __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
  return v;
}

static inline void pm_metal_io_out8(uint16_t port, uint8_t val)
{
  __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void pm_metal_io_out16(uint16_t port, uint16_t val)
{
  __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void pm_metal_io_out32(uint16_t port, uint32_t val)
{
  __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

#endif /* PM_METAL_RUNTIME_IO_IO_H */
