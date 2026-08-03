/*
 * EFI GOP helpers — stash FrameBufferBase before ExitBootServices;
 * Blt port for gop_blt scanout (pre-EBS only).
 *
 * Bodies: boot/platform/{efi,bios}/gop_*.c
 */
#ifndef PYMERGETIC_METAL_BOOT_PLATFORM_GOP_H_
#define PYMERGETIC_METAL_BOOT_PLATFORM_GOP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Locate GOP while Boot Services are alive; stash FB + protocol. */
void pm_metal_boot_efi_gop_stash(void);

/**
 * Read stashed GOP harvest.
 * `gop_out` is non-NULL only while Boot Services are still alive.
 * Returns 0 on success, -1 if no stash (BIOS / failed locate).
 */
int32_t pm_metal_boot_efi_gop_stash_get(
    uint32_t **fb_out,
    uint32_t *w_out,
    uint32_t *h_out,
    uint32_t *ppsl_out,
    void **gop_out);

/**
 * EFI: EFI_GRAPHICS_OUTPUT_PROTOCOL::Blt (BufferToVideo).
 * BIOS: always -1.
 */
int32_t pm_metal_boot_gop_port_blt(
    void *gop,
    const uint32_t *src,
    uint32_t src_x,
    uint32_t src_y,
    uint32_t dst_x,
    uint32_t dst_y,
    uint32_t w,
    uint32_t h,
    uint32_t delta);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_PLATFORM_GOP_H_ */
