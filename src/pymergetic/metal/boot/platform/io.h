/*
 * Port I/O lower half (host-only). Target poke for PCI cfg / ISA probes.
 *
 * Ops bodies: boot/platform/bios/io_port.c, boot/platform/efi/io_port.c
 * Convenience wrappers: boot/platform/io.c (real symbols, not static inline).
 */
#ifndef PYMERGETIC_METAL_BOOT_IO_H_
#define PYMERGETIC_METAL_BOOT_IO_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

typedef struct pm_metal_boot_io_ops {
  void (*outb)(uint16_t port, uint8_t val);
  uint8_t (*inb)(uint16_t port);
  void (*out16)(uint16_t port, uint16_t val);
  uint16_t (*in16)(uint16_t port);
  void (*out32)(uint16_t port, uint32_t val);
  uint32_t (*in32)(uint16_t port);
} pm_metal_boot_io_ops_t;

const pm_metal_boot_io_ops_t *pm_metal_boot_io_ops(void);

void pm_metal_boot_outb(uint16_t port, uint8_t val);
uint8_t pm_metal_boot_inb(uint16_t port);
void pm_metal_boot_out16(uint16_t port, uint16_t val);
uint16_t pm_metal_boot_in16(uint16_t port);
void pm_metal_boot_out32(uint16_t port, uint32_t val);
uint32_t pm_metal_boot_in32(uint16_t port);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_IO_H_ */
