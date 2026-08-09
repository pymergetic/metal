//! Async-first fd API — resolve via vfs, dispatch through public [`ops`].
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(dead_code, non_camel_case_types)]

use core::ffi::c_void;

use pymergetic_metal_async as _;
use pymergetic_metal_rt as _;
use pymergetic_metal_fs_vfs as vfs;

#[path = "ops.rs"]
mod ops;

pub use ops::*;

extern "C" {
    fn pm_metal_async_completed_u32(v: u32) -> u32;
    fn pm_metal_async_result_u32(h: u32) -> u32;
    fn pm_metal_async_coro_close(h: u32);
    fn pm_metal_mem_guest_ptr(cookie: u32) -> *mut u8;
    fn pm_metal_mem_guest_size(cookie: u32) -> u32;
}

pub type pm_metal_fs_h = u32;
pub const PM_METAL_FS_INVALID: u32 = 0xffff_ffff;

pub const PM_METAL_FS_O_RDONLY: u32 = 1;
pub const PM_METAL_FS_O_WRONLY: u32 = 2;
pub const PM_METAL_FS_O_RDWR: u32 = 3;
pub const PM_METAL_FS_O_CREAT: u32 = 4;
pub const PM_METAL_FS_O_TRUNC: u32 = 8;
pub const PM_METAL_FS_O_APPEND: u32 = 16;
pub const PM_METAL_FS_O_DIRECTORY: u32 = 32;

pub const PM_METAL_FS_SEEK_SET: u32 = 0;
pub const PM_METAL_FS_SEEK_CUR: u32 = 1;
pub const PM_METAL_FS_SEEK_END: u32 = 2;

pub const PM_METAL_FS_TYPE_FILE: u32 = 1;
pub const PM_METAL_FS_TYPE_DIR: u32 = 2;

#[repr(C)]
pub struct pm_metal_fs_stat_t {
    pub size: u32,
    pub type_: u32,
}

fn done(v: u32) -> u32 {
    unsafe { pm_metal_async_completed_u32(v) }
}

fn err() -> u32 {
    done(PM_METAL_FS_INVALID)
}

unsafe fn resolve_ops(
    path: *const u8,
) -> Option<(&'static pm_metal_fs_ops_t, *mut c_void, *const u8)> {
    let mut r = vfs::pm_metal_fs_vfs_resolve_t {
        ops: core::ptr::null(),
        ctx: core::ptr::null_mut(),
        rel: core::ptr::null(),
        mount: 0,
    };
    if vfs::pm_metal_fs_vfs_resolve(path, &mut r) != 0 || r.ops.is_null() {
        return None;
    }
    let ops = &*(r.ops as *const pm_metal_fs_ops_t);
    Some((ops, r.ctx, r.rel))
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_open_async(path: *const u8, flags: u32) -> u32 {
    let Some((ops, ctx, rel)) = resolve_ops(path) else {
        return err();
    };
    let Some(f) = ops.open else {
        return err();
    };
    f(ctx, rel, flags)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_close_async(h: pm_metal_fs_h) -> u32 {
    LAST_OPS
        .and_then(|ops| ops.close.map(|f| f(LAST_CTX, h)))
        .unwrap_or_else(|| done(0))
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_fread_async(
    h: pm_metal_fs_h,
    dest: *mut u8,
    len: u32,
) -> u32 {
    /* Driver-global fd table: try each registered ops.fread until one accepts.
     * Simpler v1: require open to return handle with driver id — for now call
     * through a thread-local last ops from open. */
    LAST_OPS.map(|ops| {
        let f = ops.fread?;
        Some(f(LAST_CTX, h, dest, len))
    })
    .flatten()
    .unwrap_or_else(err)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_fwrite_async(
    h: pm_metal_fs_h,
    src: *const u8,
    len: u32,
) -> u32 {
    LAST_OPS
        .and_then(|ops| ops.fwrite.map(|f| f(LAST_CTX, h, src, len)))
        .unwrap_or_else(err)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_fpread_async(
    h: pm_metal_fs_h,
    off: u32,
    dest: *mut u8,
    len: u32,
) -> u32 {
    LAST_OPS
        .and_then(|ops| ops.fpread.map(|f| f(LAST_CTX, h, off, dest, len)))
        .unwrap_or_else(err)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_fpwrite_async(
    h: pm_metal_fs_h,
    off: u32,
    src: *const u8,
    len: u32,
) -> u32 {
    match LAST_OPS.and_then(|ops| ops.fpwrite) {
        Some(f) => f(LAST_CTX, h, off, src, len),
        None => err(),
    }
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_lseek(h: pm_metal_fs_h, off: i32, whence: u32) -> i32 {
    LAST_OPS
        .and_then(|ops| ops.lseek.map(|f| f(LAST_CTX, h, off, whence)))
        .unwrap_or(-1)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_stat_async(path: *const u8, dest: *mut u8) -> u32 {
    let Some((ops, ctx, rel)) = resolve_ops(path) else {
        return err();
    };
    let Some(f) = ops.stat else {
        return err();
    };
    f(ctx, rel, dest)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_readdir_async(
    h: pm_metal_fs_h,
    name_dest: *mut u8,
    name_cap: u32,
) -> u32 {
    LAST_OPS
        .and_then(|ops| ops.readdir.map(|f| f(LAST_CTX, h, name_dest, name_cap)))
        .unwrap_or_else(err)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_mkdir_async(path: *const u8) -> u32 {
    let Some((ops, ctx, rel)) = resolve_ops(path) else {
        return err();
    };
    let Some(f) = ops.mkdir else {
        return err();
    };
    f(ctx, rel)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_unlink_async(path: *const u8) -> u32 {
    let Some((ops, ctx, rel)) = resolve_ops(path) else {
        return err();
    };
    let Some(f) = ops.unlink else {
        return err();
    };
    f(ctx, rel)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_rename_async(old: *const u8, new: *const u8) -> u32 {
    let Some((ops, ctx, rel)) = resolve_ops(old) else {
        return err();
    };
    let Some(f) = ops.rename else {
        return err();
    };
    /* new path: resolve for same mount rel — pass full new for driver */
    f(ctx, rel, new)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_fsync_async(h: pm_metal_fs_h) -> u32 {
    LAST_OPS
        .and_then(|ops| ops.fsync.map(|f| f(LAST_CTX, h)))
        .unwrap_or_else(|| done(0))
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_fstat_async(h: pm_metal_fs_h, dest: *mut u8) -> u32 {
    if dest.is_null() {
        return err();
    }
    let end = pm_metal_fs_lseek(h, 0, PM_METAL_FS_SEEK_END);
    if end < 0 {
        return err();
    }
    let _ = pm_metal_fs_lseek(h, 0, PM_METAL_FS_SEEK_SET);
    let st = dest as *mut pm_metal_fs_stat_t;
    (*st).size = end as u32;
    (*st).type_ = PM_METAL_FS_TYPE_FILE;
    done(0)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_size_async(path: *const u8) -> u32 {
    let mut st = pm_metal_fs_stat_t {
        size: 0,
        type_: 0,
    };
    let h = pm_metal_fs_stat_async(path, &mut st as *mut _ as *mut u8);
    let rc = pm_metal_async_result_u32(h);
    let sz = st.size;
    /* Free the stat handle; callers await the size handle we return. */
    pm_metal_async_coro_close(h);
    if rc == PM_METAL_FS_INVALID {
        return err();
    }
    done(sz)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_read_async(
    path: *const u8,
    dest: *mut u8,
    dest_len: u32,
) -> u32 {
    if dest.is_null() || dest_len == 0 {
        return done(0);
    }
    let oh = pm_metal_fs_open_async(path, PM_METAL_FS_O_RDONLY);
    let fd = pm_metal_async_result_u32(oh);
    if fd == PM_METAL_FS_INVALID {
        return err();
    }
    let rh = pm_metal_fs_fread_async(fd, dest, dest_len);
    let n = pm_metal_async_result_u32(rh);
    let _ = pm_metal_fs_close_async(fd);
    done(n)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_write_async(
    path: *const u8,
    src: *const u8,
    src_len: u32,
) -> u32 {
    if src_len > 0 && src.is_null() {
        return err();
    }
    let flags = PM_METAL_FS_O_WRONLY | PM_METAL_FS_O_CREAT | PM_METAL_FS_O_TRUNC;
    let oh = pm_metal_fs_open_async(path, flags);
    let fd = pm_metal_async_result_u32(oh);
    if fd == PM_METAL_FS_INVALID {
        return err();
    }
    let wh = pm_metal_fs_fwrite_async(fd, src, src_len);
    let n = pm_metal_async_result_u32(wh);
    let _ = pm_metal_fs_close_async(fd);
    done(n)
}

/// Awaitable read into a mem guest cookie (survives coro unpin).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_read_mem_async(
    path: *const u8,
    dest_cookie: u32,
    dest_len: u32,
) -> u32 {
    let native = pm_metal_mem_guest_ptr(dest_cookie);
    let cap = pm_metal_mem_guest_size(dest_cookie);
    if native.is_null() || dest_len == 0 || dest_len > cap {
        return err();
    }
    pm_metal_fs_read_async(path, native, dest_len)
}

/// Awaitable write from a mem guest cookie.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_write_mem_async(
    path: *const u8,
    src_cookie: u32,
    src_len: u32,
) -> u32 {
    if src_len == 0 {
        return pm_metal_fs_write_async(path, core::ptr::null(), 0);
    }
    let native = pm_metal_mem_guest_ptr(src_cookie);
    let cap = pm_metal_mem_guest_size(src_cookie);
    if native.is_null() || src_len > cap {
        return err();
    }
    pm_metal_fs_write_async(path, native, src_len)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_result(h: u32) -> u32 {
    pm_metal_async_result_u32(h)
}

/// Volume stats for mount at dense vfs index. Returns 0 ok, -1 missing/bad.
/// When `ops.statfs` is absent, fills `flags` from `fwrite == None` and leaves sizes 0.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_mount_statfs(
    index: u32,
    out: *mut pm_metal_fs_statfs_t,
) -> i32 {
    if out.is_null() {
        return -1;
    }
    let mut ops_p: *const c_void = core::ptr::null();
    let mut ctx: *mut c_void = core::ptr::null_mut();
    if vfs::pm_metal_fs_vfs_mount_get(index, &mut ops_p, &mut ctx) != 0 || ops_p.is_null() {
        return -1;
    }
    let ops = &*(ops_p as *const pm_metal_fs_ops_t);
    *out = pm_metal_fs_statfs_t {
        total: 0,
        used: 0,
        flags: if ops.fwrite.is_none() {
            PM_METAL_FS_ST_RDONLY
        } else {
            0
        },
    };
    if let Some(f) = ops.statfs {
        if f(ctx, out) != 0 {
            return -1;
        }
    }
    0
}

/// Remember ops/ctx from last successful open (v1 fd routing).
pub static mut LAST_OPS: Option<&'static pm_metal_fs_ops_t> = None;
pub static mut LAST_CTX: *mut c_void = core::ptr::null_mut();

/// Called by drivers after open succeeds.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_set_active_ops(
    ops: *const pm_metal_fs_ops_t,
    ctx: *mut c_void,
) {
    if ops.is_null() {
        LAST_OPS = None;
        LAST_CTX = core::ptr::null_mut();
    } else {
        LAST_OPS = Some(&*ops);
        LAST_CTX = ctx;
    }
}