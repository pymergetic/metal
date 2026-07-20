/*
 * Internal helpers shared by efi_platform.c / efi_wasi_fs.c.
 */
#ifndef EFI_WAMR_INTERNAL_H_
#define EFI_WAMR_INTERNAL_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Feed bytes into the WASI stdout/stderr line buffer.
 * On newline (or flush): pm_metal_ui_tab_puts; if also_serial, Print too.
 */
void efi_wamr_feed_stdout(const char *buf, size_t len, int also_serial);

/** Flush any pending partial line. */
void efi_wamr_flush_stdout(int also_serial);

#ifdef __cplusplus
}
#endif

#endif /* EFI_WAMR_INTERNAL_H_ */
