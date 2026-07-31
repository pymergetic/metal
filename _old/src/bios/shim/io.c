/* Freestanding I/O port helpers for Metal BIOS. */
#include "PmBiosUefi.h"
#include "Library/IoLib.h"

UINT8
IoRead8(UINTN Port)
{
  UINT8 v;
  __asm__ __volatile__("inb %w1, %b0" : "=a"(v) : "Nd"((UINT16)Port));
  return v;
}

UINT16
IoRead16(UINTN Port)
{
  UINT16 v;
  __asm__ __volatile__("inw %w1, %w0" : "=a"(v) : "Nd"((UINT16)Port));
  return v;
}

UINT32
IoRead32(UINTN Port)
{
  UINT32 v;
  __asm__ __volatile__("inl %w1, %0" : "=a"(v) : "Nd"((UINT16)Port));
  return v;
}

VOID
IoWrite8(UINTN Port, UINT8 Value)
{
  __asm__ __volatile__("outb %b0, %w1" : : "a"(Value), "Nd"((UINT16)Port));
}

VOID
IoWrite16(UINTN Port, UINT16 Value)
{
  __asm__ __volatile__("outw %w0, %w1" : : "a"(Value), "Nd"((UINT16)Port));
}

VOID
IoWrite32(UINTN Port, UINT32 Value)
{
  __asm__ __volatile__("outl %0, %w1" : : "a"(Value), "Nd"((UINT16)Port));
}
