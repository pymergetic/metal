/*
 * EFI ESP (boot volume) — host-only read helpers for package files.
 */
#ifndef PYMERGETIC_METAL_ESP_H_
#define PYMERGETIC_METAL_ESP_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

/** Bind to the loaded-image device (call once from UefiMain). */
int pm_metal_esp_init(void *image_handle);

int pm_metal_esp_ready(void);

/**
 * Read an ESP-relative path (e.g. "mods/apps/doom/doom1.wad").
 * Allocates with pm_metal_mem_alloc; caller frees with pm_metal_mem_free.
 * Returns 0 and sets *out and *len, or -1.
 */
int pm_metal_esp_read_file(const char *path, uint8_t **out, uint32_t *len);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_ESP_H_ */
