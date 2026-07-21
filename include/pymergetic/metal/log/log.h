/*
 * Unified host log: one ring buffer, viewports with markers.
 *
 * Boot: UEFI viewport (live) → EBS closes it (marker remembered) →
 * UART resumes from marker → UI pulls full history → last detach clears
 * the buffer. After that, only direct viewports remain.
 */
#ifndef PYMERGETIC_METAL_LOG_H_
#define PYMERGETIC_METAL_LOG_H_

#include <stdarg.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

typedef enum {
  PM_METAL_LOG_VP_UEFI = 0,
  PM_METAL_LOG_VP_UART = 1,
  PM_METAL_LOG_VP_UI   = 2,
  PM_METAL_LOG_VP_COUNT
} pm_metal_log_vp_t;

/** Install UEFI ConOut/serial viewport (pre-EBS). Idempotent. */
void pm_metal_log_init(void);

/** Append one line (no trailing newline required). */
void pm_metal_log(const char *line);

/**
 * printf-style append.
 * On EFI, EFIAPI so VA_LIST matches PrintLib (ms_abi).
 */
#if defined(EFIAPI)
void EFIAPI pm_metal_logf(const char *fmt, ...);
#else
void pm_metal_logf(const char *fmt, ...);
#endif

/**
 * Close UEFI viewport at EBS; remember marker for UART resume.
 * Safe if UEFI was never opened.
 */
void pm_metal_log_ebs_close_uefi(void);

/**
 * Attach UART viewport: drain from remembered EBS marker, then direct.
 * Call when Metal console (COM1/virtio) should own the terminal.
 */
void pm_metal_log_attach_uart(void);

/**
 * Attach UI viewport: drain full buffer, then direct.
 * Call when UI console exists (last init step).
 */
void pm_metal_log_attach_ui(void);

/** Viewport still on the boot buffer? */
int pm_metal_log_buffer_live(void);

/**
 * End boot-log epoch (after UI attach). Drops the ring; further logs
 * go direct to live viewports only.
 */
void pm_metal_log_boot_complete(void);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_LOG_H_ */
