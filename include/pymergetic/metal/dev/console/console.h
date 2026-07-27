/*
 * Host virtio-console (product serial after EBS).
 *
 * impl: common — src/pymergetic/metal/dev/console/virtio_console.c
 */
#ifndef PYMERGETIC_METAL_DEV_CONSOLE_CONSOLE_H_
#define PYMERGETIC_METAL_DEV_CONSOLE_CONSOLE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

/** Probe virtio-console; 0 on success. */
int pm_metal_console_virtio_probe(void);
int pm_metal_console_ready(void);
/** Write bytes to TX queue (sync short-write). */
uint32_t pm_metal_console_write(const void *ptr, uint32_t len);
/** Write to COM1 (QEMU -serial); works after EBS. */
void pm_metal_console_com1_write(const void *ptr, uint32_t len);
/**
 * Optional byte mirror of COM1 TX (prompt, echo, log UART path).
 * Used so SSH is the same console viewport as UART — not a private shell.
 * fn may be NULL to clear. Only one mirror; last setter wins.
 */
typedef void (*pm_metal_console_mirror_fn)(const void *ptr, uint32_t len, void *ctx);
void pm_metal_console_set_mirror(pm_metal_console_mirror_fn fn, void *ctx);
/** Poll RX into attached stdin ring / internal buffer. */
void pm_metal_console_poll(void);
/** Pop up to len RX bytes; returns count. */
uint32_t pm_metal_console_read(void *ptr, uint32_t len);
/**
 * Inject host keys into the same RX ring COM1/virtio use (SSH viewport →
 * shared MetalShellHandleAscii path). Returns bytes accepted.
 */
uint32_t pm_metal_console_inject_rx(const void *ptr, uint32_t len);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_CONSOLE_CONSOLE_H_ */
