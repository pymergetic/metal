#if defined(PM_METAL_BOOT_TARGET_EFI)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_BIOS)
#error "boot/efi/uart.c built with PM_METAL_BOOT_TARGET_BIOS"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

/*
 * EFI uart lower half — freestanding stub until EDK2 Serial IO is wired.
 * Honest: write() drops bytes (image not linked yet). Same ops shape as BIOS.
 */
#include <stddef.h>

#include <pymergetic/metal/boot/platform/uart.h>

static void efi_uart_write(const char *s, size_t n)
{
  (void)s;
  (void)n;
  /* No EDK2 ConOut/SerialIO in this tree yet — discard. */
}

static const pm_metal_boot_uart_ops_t g_ops = {
  .write = efi_uart_write,
};

const pm_metal_boot_uart_ops_t *pm_metal_boot_uart_ops(void)
{
  return &g_ops;
}
