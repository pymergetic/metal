/*
 * EFI ESP (boot volume) — host-only file helpers for package files.
 *
 * impl: common — src/pymergetic/metal/fs/esp/esp.c
 * impl: port  — src/{bios,efi}/pymergetic/metal/fs/esp/esp_port.c
 */
#ifndef PYMERGETIC_METAL_FS_ESP_ESP_H_
#define PYMERGETIC_METAL_FS_ESP_ESP_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

#define PM_METAL_ESP_TYPE_FILE  1u
#define PM_METAL_ESP_TYPE_DIR   2u

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
 * Read an ESP-relative path (e.g. "mods/apps/hello/payload.bin").
 * Allocates with pm_metal_mem_alloc; caller frees with pm_metal_mem_free.
 * Returns 0 and sets *out and *len, or -1.
 */
int pm_metal_esp_read_file(const char *path, uint8_t **out, uint32_t *len);

/**
 * Create/truncate write an ESP-relative path.
 * Returns 0 on success, -1 on failure.
 */
int pm_metal_esp_write_file(const char *path, const uint8_t *data, uint32_t len);

/** Stat: size + PM_METAL_ESP_TYPE_* (file or dir). Returns 0 or -1. */
int pm_metal_esp_stat(const char *path, uint32_t *size, uint32_t *type);

/** Read up to len bytes at off into buf; sets *nread. Returns 0 or -1. */
int pm_metal_esp_read_at(const char *path, uint32_t off, uint8_t *buf,
			 uint32_t len, uint32_t *nread);

/** Patch write at off; extends file. truncate!=0 replaces whole file first. */
int pm_metal_esp_write_at(const char *path, uint32_t off, const uint8_t *data,
			    uint32_t len, int truncate);

/** Flush cached file to live volume (no-op when port unavailable). Returns 0 or -1. */
int pm_metal_esp_fsync(const char *path);

int pm_metal_esp_mkdir(const char *path);
int pm_metal_esp_unlink(const char *path);
int pm_metal_esp_rename(const char *old_path, const char *new_path);

/**
 * Directory iteration: index 0..N-1 fills name; returns 1 if entry, 0 if EOF, -1 error.
 */
int pm_metal_esp_readdir(const char *path, uint32_t index, char *name,
			 uint32_t name_cap);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_FS_ESP_ESP_H_ */
