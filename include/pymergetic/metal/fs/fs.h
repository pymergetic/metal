/*
 * Metal ESP file I/O — guest/host dual ABI (fd-shaped v1).
 *
 * Product path: open → (sync lseek) → fread/fwrite/fpread/fpwrite → close,
 * each async op → await → fs_result. Path-based size/read/write are sync
 * transitional helpers (bounded ESP I/O on guest linear buffers).
 *
 * impl: common — src/pymergetic/metal/fs/fs.c
 */
#ifndef PYMERGETIC_METAL_FS_FS_H_
#define PYMERGETIC_METAL_FS_FS_H_

#include <stdint.h>

#include "pymergetic/metal/runtime/async/async.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t pm_metal_fs_h;

#define PM_METAL_FS_INVALID  ((pm_metal_fs_h)0xffffffffu)

#define PM_METAL_FS_O_RDONLY    1u
#define PM_METAL_FS_O_WRONLY    2u
#define PM_METAL_FS_O_RDWR      3u
#define PM_METAL_FS_O_CREAT     4u
#define PM_METAL_FS_O_TRUNC     8u
#define PM_METAL_FS_O_APPEND    16u
#define PM_METAL_FS_O_DIRECTORY 32u

#define PM_METAL_FS_SEEK_SET  0u
#define PM_METAL_FS_SEEK_CUR  1u
#define PM_METAL_FS_SEEK_END  2u

#define PM_METAL_FS_TYPE_FILE  1u
#define PM_METAL_FS_TYPE_DIR   2u

typedef struct {
	uint32_t size;
	uint32_t type;
} pm_metal_fs_stat_t;

/** Guest linear offset (wasm) or host pointer — for buffer args. */
#if defined(__wasm__)
#define PM_METAL_FS_IO_PTR(p) ((uint32_t)(uintptr_t)(p))
#else
#define PM_METAL_FS_IO_PTR(p) (p)
#endif

#define PM_METAL_FS_WASI_MODULE "pymergetic.metal.fs"

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_FS_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_FS_WASI_MODULE, name)

extern pm_metal_async_handle_t pm_metal_fs_open_async(const char *path,
						      uint32_t flags)
	PM_METAL_FS_IMPORT(pm_metal_fs_open_async);
extern pm_metal_async_handle_t pm_metal_fs_close_async(pm_metal_fs_h h)
	PM_METAL_FS_IMPORT(pm_metal_fs_close_async);
extern pm_metal_async_handle_t pm_metal_fs_fread_async(pm_metal_fs_h h,
						       uint32_t dest,
						       uint32_t len)
	PM_METAL_FS_IMPORT(pm_metal_fs_fread_async);
extern pm_metal_async_handle_t pm_metal_fs_fwrite_async(pm_metal_fs_h h,
							uint32_t src,
							uint32_t len)
	PM_METAL_FS_IMPORT(pm_metal_fs_fwrite_async);
extern pm_metal_async_handle_t pm_metal_fs_fpread_async(pm_metal_fs_h h,
							uint32_t off,
							uint32_t dest,
							uint32_t len)
	PM_METAL_FS_IMPORT(pm_metal_fs_fpread_async);
extern pm_metal_async_handle_t pm_metal_fs_fpwrite_async(pm_metal_fs_h h,
							 uint32_t off,
							 uint32_t src,
							 uint32_t len)
	PM_METAL_FS_IMPORT(pm_metal_fs_fpwrite_async);
extern int32_t pm_metal_fs_lseek(pm_metal_fs_h h, int32_t off, uint32_t whence)
	PM_METAL_FS_IMPORT(pm_metal_fs_lseek);
extern pm_metal_async_handle_t pm_metal_fs_stat_async(const char *path,
						      uint32_t dest)
	PM_METAL_FS_IMPORT(pm_metal_fs_stat_async);
extern pm_metal_async_handle_t pm_metal_fs_fstat_async(pm_metal_fs_h h,
						       uint32_t dest)
	PM_METAL_FS_IMPORT(pm_metal_fs_fstat_async);
extern pm_metal_async_handle_t pm_metal_fs_readdir_async(pm_metal_fs_h h,
							 uint32_t name_dest,
							 uint32_t name_cap)
	PM_METAL_FS_IMPORT(pm_metal_fs_readdir_async);
extern pm_metal_async_handle_t pm_metal_fs_mkdir_async(const char *path)
	PM_METAL_FS_IMPORT(pm_metal_fs_mkdir_async);
extern pm_metal_async_handle_t pm_metal_fs_unlink_async(const char *path)
	PM_METAL_FS_IMPORT(pm_metal_fs_unlink_async);
extern pm_metal_async_handle_t pm_metal_fs_rename_async(const char *old_path,
						       const char *new_path)
	PM_METAL_FS_IMPORT(pm_metal_fs_rename_async);
extern pm_metal_async_handle_t pm_metal_fs_fsync_async(pm_metal_fs_h h)
	PM_METAL_FS_IMPORT(pm_metal_fs_fsync_async);

extern pm_metal_async_handle_t pm_metal_fs_size_async(const char *path)
	PM_METAL_FS_IMPORT(pm_metal_fs_size_async);
extern pm_metal_async_handle_t pm_metal_fs_read_async(const char *path,
						      uint32_t dest,
						      uint32_t dest_len)
	PM_METAL_FS_IMPORT(pm_metal_fs_read_async);
extern pm_metal_async_handle_t pm_metal_fs_write_async(const char *path,
						       uint32_t src,
						       uint32_t src_len)
	PM_METAL_FS_IMPORT(pm_metal_fs_write_async);
extern uint32_t pm_metal_fs_result(pm_metal_async_handle_t self_h)
	PM_METAL_FS_IMPORT(pm_metal_fs_result);

extern uint32_t pm_metal_fs_size(const char *path)
	PM_METAL_FS_IMPORT(pm_metal_fs_size);
extern uint32_t pm_metal_fs_read(const char *path, uint32_t dest,
				 uint32_t dest_len)
	PM_METAL_FS_IMPORT(pm_metal_fs_read);
extern uint32_t pm_metal_fs_write(const char *path, uint32_t src,
				  uint32_t src_len)
	PM_METAL_FS_IMPORT(pm_metal_fs_write);
#else
pm_metal_async_handle_t pm_metal_fs_open_async(const char *path, uint32_t flags);
pm_metal_async_handle_t pm_metal_fs_close_async(pm_metal_fs_h h);
pm_metal_async_handle_t pm_metal_fs_fread_async(pm_metal_fs_h h, void *dest,
						uint32_t len);
pm_metal_async_handle_t pm_metal_fs_fwrite_async(pm_metal_fs_h h,
						 const void *src, uint32_t len);
pm_metal_async_handle_t pm_metal_fs_fpread_async(pm_metal_fs_h h, uint32_t off,
						 void *dest, uint32_t len);
pm_metal_async_handle_t pm_metal_fs_fpwrite_async(pm_metal_fs_h h, uint32_t off,
						  const void *src, uint32_t len);
int32_t pm_metal_fs_lseek(pm_metal_fs_h h, int32_t off, uint32_t whence);
pm_metal_async_handle_t pm_metal_fs_stat_async(const char *path, void *dest);
pm_metal_async_handle_t pm_metal_fs_fstat_async(pm_metal_fs_h h, void *dest);
pm_metal_async_handle_t pm_metal_fs_readdir_async(pm_metal_fs_h h, char *name_dest,
						  uint32_t name_cap);
pm_metal_async_handle_t pm_metal_fs_mkdir_async(const char *path);
pm_metal_async_handle_t pm_metal_fs_unlink_async(const char *path);
pm_metal_async_handle_t pm_metal_fs_rename_async(const char *old_path,
					       const char *new_path);
pm_metal_async_handle_t pm_metal_fs_fsync_async(pm_metal_fs_h h);

pm_metal_async_handle_t pm_metal_fs_size_async(const char *path);
pm_metal_async_handle_t pm_metal_fs_read_async(const char *path, void *dest,
					       uint32_t dest_len);
pm_metal_async_handle_t pm_metal_fs_write_async(const char *path,
						const void *src,
						uint32_t src_len);
uint32_t pm_metal_fs_result(pm_metal_async_handle_t self_h);

uint32_t pm_metal_fs_size(const char *path);
uint32_t pm_metal_fs_read(const char *path, void *dest, uint32_t dest_len);
uint32_t pm_metal_fs_write(const char *path, const void *src, uint32_t src_len);
void pm_metal_fs_bind_inst(void *module_inst);
int pm_metal_fs_native_register(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_FS_FS_H_ */
