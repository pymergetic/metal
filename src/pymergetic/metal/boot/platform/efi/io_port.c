#if defined(PM_METAL_BOOT_TARGET_EFI)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_BIOS)
#error "boot/efi/io_port.c built with PM_METAL_BOOT_TARGET_BIOS"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

#include <stdint.h>

#include <pymergetic/metal/boot/platform/io.h>

/* x86 EFI still has port I/O; no EDK2 IoLib required. */
static void efi_outb(uint16_t port, uint8_t val)
{
  __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static uint8_t efi_inb(uint16_t port)
{
  uint8_t val;

  __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
  return val;
}

static void efi_out32(uint16_t port, uint32_t val)
{
  __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static uint32_t efi_in32(uint16_t port)
{
  uint32_t val;

  __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
  return val;
}

static const pm_metal_boot_io_ops_t g_ops = {
  .outb = efi_outb,
  .inb = efi_inb,
  .out32 = efi_out32,
  .in32 = efi_in32,
};

const pm_metal_boot_io_ops_t *pm_metal_boot_io_ops(void)
{
  return &g_ops;
}
