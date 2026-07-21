/*
 * Host virtio-console (product serial after EBS).
 */
#ifndef PYMERGETIC_METAL_CONSOLE_H_
#define PYMERGETIC_METAL_CONSOLE_H_

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
/** Poll RX into attached stdin ring / internal buffer. */
void pm_metal_console_poll(void);
/** Pop up to len RX bytes; returns count. */
uint32_t pm_metal_console_read(void *ptr, uint32_t len);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_CONSOLE_H_ */
