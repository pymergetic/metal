/*
 * UART lower half (host-only). Target poke only — not a device driver.
 * Unified serial access: metal/dev/serial. Console ring: metal/console.
 *
 * Bodies: boot/platform/bios/uart.c, boot/platform/efi/uart.c
 */
#ifndef PYMERGETIC_METAL_BOOT_UART_H_
#define PYMERGETIC_METAL_BOOT_UART_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

typedef struct pm_metal_boot_uart_ops {
  /** Write n ASCII bytes from s. Must not be NULL. */
  void (*write)(const char *s, size_t n);
} pm_metal_boot_uart_ops_t;

const pm_metal_boot_uart_ops_t *pm_metal_boot_uart_ops(void);

static inline void pm_metal_boot_uart_write(const char *s, size_t n)
{
  pm_metal_boot_uart_ops()->write(s, n);
}

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_UART_H_ */
