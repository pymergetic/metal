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
 * Preload an ESP path into RAM so size/read/write work after EBS.
 * Call on the sync floor while Simple File System is alive.
 */
int pm_metal_esp_preload(const char *path);

/**
 * Seed/replace a RAM-cache entry (works after EBS).
 * Used when the file is known/embedded and ESP is gone.
 */
int pm_metal_esp_cache_put(const char *path, const uint8_t *data, uint32_t len);

/**
 * ESP-relative path size in bytes (GetInfo only — no data read).
 * Returns 0 and sets *len, or -1 (missing / error). Zero-length files OK.
 */
int pm_metal_esp_file_size(const char *path, uint32_t *len);

/**
 * Read an ESP-relative path (e.g. "mods/apps/doom/doom1.wad").
 * Allocates with pm_metal_mem_alloc; caller frees with pm_metal_mem_free.
 * Returns 0 and sets *out and *len, or -1.
 */
int pm_metal_esp_read_file(const char *path, uint8_t **out, uint32_t *len);

/**
 * Create/truncate write an ESP-relative path.
 * Returns 0 on success, -1 on failure.
 */
int pm_metal_esp_write_file(const char *path, const uint8_t *data, uint32_t len);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_ESP_H_ */
