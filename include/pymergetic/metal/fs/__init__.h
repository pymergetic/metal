/*
 * pymergetic.metal.fs — async fd API (RS callee, C ABI).
 * Path == module.
 */
#ifndef PYMERGETIC_METAL_FS_INIT_H_
#define PYMERGETIC_METAL_FS_INIT_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t pm_metal_fs_h;
#define PM_METAL_FS_INVALID 0xffffffffu

#define PM_METAL_FS_O_RDONLY 1u
#define PM_METAL_FS_O_WRONLY 2u
#define PM_METAL_FS_O_RDWR 3u
#define PM_METAL_FS_O_CREAT 4u
#define PM_METAL_FS_O_TRUNC 8u
#define PM_METAL_FS_O_APPEND 16u
#define PM_METAL_FS_O_DIRECTORY 32u

#define PM_METAL_FS_SEEK_SET 0u
#define PM_METAL_FS_SEEK_CUR 1u
#define PM_METAL_FS_SEEK_END 2u

#define PM_METAL_FS_TYPE_FILE 1u
#define PM_METAL_FS_TYPE_DIR 2u

#define PM_METAL_FS_ST_RDONLY 1u

typedef struct pm_metal_fs_stat {
    uint32_t size;
    uint32_t type_;
} pm_metal_fs_stat_t;

typedef struct pm_metal_fs_statfs {
    uint64_t total;
    uint64_t used;
    uint32_t flags;
} pm_metal_fs_statfs_t;

typedef uint32_t (*pm_metal_fs_op_open_fn)(void *ctx, const uint8_t *path, uint32_t flags);
typedef uint32_t (*pm_metal_fs_op_path_fn)(void *ctx, const uint8_t *path);
typedef uint32_t (*pm_metal_fs_op_h_fn)(void *ctx, uint32_t h);
typedef uint32_t (*pm_metal_fs_op_read_fn)(void *ctx, uint32_t h, uint8_t *dest, uint32_t len);
typedef uint32_t (*pm_metal_fs_op_write_fn)(void *ctx, uint32_t h, const uint8_t *src, uint32_t len);
typedef uint32_t (*pm_metal_fs_op_pread_fn)(void *ctx, uint32_t h, uint32_t off, uint8_t *dest,
                                            uint32_t len);
typedef uint32_t (*pm_metal_fs_op_pwrite_fn)(void *ctx, uint32_t h, uint32_t off, const uint8_t *src,
                                             uint32_t len);
typedef uint32_t (*pm_metal_fs_op_stat_fn)(void *ctx, const uint8_t *path, uint8_t *st_out);
typedef uint32_t (*pm_metal_fs_op_readdir_fn)(void *ctx, uint32_t h, uint8_t *name_out,
                                              uint32_t name_cap);
typedef uint32_t (*pm_metal_fs_op_rename_fn)(void *ctx, const uint8_t *old, const uint8_t *newp);
typedef int32_t (*pm_metal_fs_op_lseek_fn)(void *ctx, uint32_t h, int32_t off, uint32_t whence);
typedef int32_t (*pm_metal_fs_op_statfs_fn)(void *ctx, pm_metal_fs_statfs_t *out);

typedef struct pm_metal_fs_ops {
    const uint8_t *name;
    pm_metal_fs_op_open_fn open;
    pm_metal_fs_op_h_fn close;
    pm_metal_fs_op_read_fn fread;
    pm_metal_fs_op_write_fn fwrite;
    pm_metal_fs_op_pread_fn fpread;
    pm_metal_fs_op_pwrite_fn fpwrite;
    pm_metal_fs_op_lseek_fn lseek;
    pm_metal_fs_op_stat_fn stat;
    pm_metal_fs_op_readdir_fn readdir;
    pm_metal_fs_op_path_fn mkdir;
    pm_metal_fs_op_path_fn unlink;
    pm_metal_fs_op_rename_fn rename;
    pm_metal_fs_op_h_fn fsync;
    pm_metal_fs_op_statfs_fn statfs;
} pm_metal_fs_ops_t;

uint32_t pm_metal_fs_open_async(const uint8_t *path, uint32_t flags);
uint32_t pm_metal_fs_close_async(pm_metal_fs_h h);
uint32_t pm_metal_fs_fread_async(pm_metal_fs_h h, uint8_t *dest, uint32_t len);
uint32_t pm_metal_fs_fwrite_async(pm_metal_fs_h h, const uint8_t *src, uint32_t len);
uint32_t pm_metal_fs_fpread_async(pm_metal_fs_h h, uint32_t off, uint8_t *dest, uint32_t len);
uint32_t pm_metal_fs_fpwrite_async(pm_metal_fs_h h, uint32_t off, const uint8_t *src, uint32_t len);
int32_t pm_metal_fs_lseek(pm_metal_fs_h h, int32_t off, uint32_t whence);
uint32_t pm_metal_fs_stat_async(const uint8_t *path, uint8_t *dest);
uint32_t pm_metal_fs_readdir_async(pm_metal_fs_h h, uint8_t *name_dest, uint32_t name_cap);
uint32_t pm_metal_fs_mkdir_async(const uint8_t *path);
uint32_t pm_metal_fs_unlink_async(const uint8_t *path);
uint32_t pm_metal_fs_rename_async(const uint8_t *old, const uint8_t *newp);
uint32_t pm_metal_fs_fsync_async(pm_metal_fs_h h);
uint32_t pm_metal_fs_fstat_async(pm_metal_fs_h h, uint8_t *dest);
uint32_t pm_metal_fs_size_async(const uint8_t *path);
uint32_t pm_metal_fs_read_async(const uint8_t *path, uint8_t *dest, uint32_t dest_len);
uint32_t pm_metal_fs_write_async(const uint8_t *path, const uint8_t *src, uint32_t src_len);
uint32_t pm_metal_fs_read_mem_async(const uint8_t *path, uint32_t dest_cookie, uint32_t dest_len);
uint32_t pm_metal_fs_write_mem_async(const uint8_t *path, uint32_t src_cookie, uint32_t src_len);
uint32_t pm_metal_fs_result(uint32_t h);
int32_t pm_metal_fs_mount_statfs(uint32_t index, pm_metal_fs_statfs_t *out);
void pm_metal_fs_set_active_ops(const pm_metal_fs_ops_t *ops, void *ctx);

int32_t pm_metal_fs_ops_register(const pm_metal_fs_ops_t *ops);
const pm_metal_fs_ops_t *pm_metal_fs_ops_lookup(const uint8_t *name);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_FS_INIT_H_ */
