/*
 * pymergetic.metal.fs.embed — emit C/Rust source for a byte image.
 */
#ifndef PYMERGETIC_METAL_FS_EMBED_H_
#define PYMERGETIC_METAL_FS_EMBED_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t pm_metal_fs_embed_c(const uint8_t *name, const uint8_t *data, size_t len, uint8_t *out,
                            size_t out_cap, size_t *out_len);
int32_t pm_metal_fs_embed_rs(const uint8_t *name, const uint8_t *data, size_t len, uint8_t *out,
                             size_t out_cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif
