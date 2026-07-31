//! littlefs on an in-memory buffer — format/seed (C) + mount/ops (Rust).
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(dead_code, non_camel_case_types)]

use core::ffi::c_void;
use core::ptr::{addr_of, addr_of_mut};

use pymergetic_metal_fs::{
    pm_metal_fs_ops_register, pm_metal_fs_ops_t, pm_metal_fs_set_active_ops, pm_metal_fs_statfs_t,
    PM_METAL_FS_INVALID,
};
use pymergetic_metal_rt as _;
use pymergetic_metal_vfs as vfs;

extern "C" {
    fn pm_metal_async_completed_u32(v: u32) -> u32;
    fn pm_metal_fs_littlefs_open_buf(buf: *mut u8, len: usize) -> u32;
    fn pm_metal_fs_littlefs_close_vol(vol: u32) -> i32;
    fn pm_metal_fs_littlefs_vol_bytes(vol: u32) -> usize;
    fn pm_metal_fs_littlefs_op_open(vol: u32, path: *const u8, flags: u32) -> u32;
    fn pm_metal_fs_littlefs_op_close(h: u32) -> u32;
    fn pm_metal_fs_littlefs_op_fread(h: u32, dest: *mut u8, len: u32) -> u32;
    fn pm_metal_fs_littlefs_op_fwrite(h: u32, src: *const u8, len: u32) -> u32;
    fn pm_metal_fs_littlefs_op_fpread(h: u32, off: u32, dest: *mut u8, len: u32) -> u32;
    fn pm_metal_fs_littlefs_op_fpwrite(h: u32, off: u32, src: *const u8, len: u32) -> u32;
    fn pm_metal_fs_littlefs_op_lseek(h: u32, off: i32, whence: u32) -> i32;
    fn pm_metal_fs_littlefs_op_stat(vol: u32, path: *const u8, st_out: *mut u8) -> u32;
    fn pm_metal_fs_littlefs_op_readdir(h: u32, name_out: *mut u8, name_cap: u32) -> u32;
    fn pm_metal_fs_littlefs_op_mkdir(vol: u32, path: *const u8) -> u32;
    fn pm_metal_fs_littlefs_op_unlink(vol: u32, path: *const u8) -> u32;
    fn pm_metal_fs_littlefs_op_fsync(h: u32) -> u32;
}

static mut OPS_READY: bool = false;
static LFS_NAME: &[u8] = b"littlefs\0";

static LFS_OPS: pm_metal_fs_ops_t = pm_metal_fs_ops_t {
    name: LFS_NAME.as_ptr(),
    open: Some(op_open),
    close: Some(op_close),
    fread: Some(op_fread),
    fwrite: Some(op_fwrite),
    fpread: Some(op_fpread),
    fpwrite: Some(op_fpwrite),
    lseek: Some(op_lseek),
    stat: Some(op_stat),
    readdir: Some(op_readdir),
    mkdir: Some(op_mkdir),
    unlink: Some(op_unlink),
    rename: None,
    fsync: Some(op_fsync),
    statfs: Some(op_statfs),
};

fn done(v: u32) -> u32 {
    unsafe { pm_metal_async_completed_u32(v) }
}

unsafe fn ensure_ops() {
    if !*addr_of!(OPS_READY) {
        let _ = pm_metal_fs_ops_register(&LFS_OPS);
        *addr_of_mut!(OPS_READY) = true;
    }
}

/// Mount littlefs buffer at `target`. Returns 0 ok.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_littlefs_mount(
    target: *const u8,
    buf: *mut u8,
    len: usize,
) -> i32 {
    let id = pm_metal_fs_littlefs_open_buf(buf, len);
    if id == 0 {
        return -1;
    }
    ensure_ops();
    let ctx = id as usize as *mut c_void;
    if vfs::pm_metal_vfs_mount(target, &LFS_OPS as *const _ as *const c_void, ctx) == 0 {
        let _ = pm_metal_fs_littlefs_close_vol(id);
        return -1;
    }
    0
}

unsafe extern "C" fn op_open(ctx: *mut c_void, path: *const u8, flags: u32) -> u32 {
    let vol = ctx as u32;
    let h = pm_metal_fs_littlefs_op_open(vol, path, flags);
    if h != PM_METAL_FS_INVALID {
        pm_metal_fs_set_active_ops(&LFS_OPS, ctx);
    }
    done(h)
}

unsafe extern "C" fn op_close(_ctx: *mut c_void, h: u32) -> u32 {
    done(pm_metal_fs_littlefs_op_close(h))
}

unsafe extern "C" fn op_fread(_ctx: *mut c_void, h: u32, dest: *mut u8, len: u32) -> u32 {
    done(pm_metal_fs_littlefs_op_fread(h, dest, len))
}

unsafe extern "C" fn op_fwrite(_ctx: *mut c_void, h: u32, src: *const u8, len: u32) -> u32 {
    done(pm_metal_fs_littlefs_op_fwrite(h, src, len))
}

unsafe extern "C" fn op_fpread(
    _ctx: *mut c_void,
    h: u32,
    off: u32,
    dest: *mut u8,
    len: u32,
) -> u32 {
    done(pm_metal_fs_littlefs_op_fpread(h, off, dest, len))
}

unsafe extern "C" fn op_fpwrite(
    _ctx: *mut c_void,
    h: u32,
    off: u32,
    src: *const u8,
    len: u32,
) -> u32 {
    done(pm_metal_fs_littlefs_op_fpwrite(h, off, src, len))
}

unsafe extern "C" fn op_lseek(_ctx: *mut c_void, h: u32, off: i32, whence: u32) -> i32 {
    pm_metal_fs_littlefs_op_lseek(h, off, whence)
}

unsafe extern "C" fn op_stat(ctx: *mut c_void, path: *const u8, st_out: *mut u8) -> u32 {
    done(pm_metal_fs_littlefs_op_stat(ctx as u32, path, st_out))
}

unsafe extern "C" fn op_statfs(ctx: *mut c_void, out: *mut pm_metal_fs_statfs_t) -> i32 {
    if out.is_null() {
        return -1;
    }
    let n = pm_metal_fs_littlefs_vol_bytes(ctx as u32);
    if n == 0 {
        return -1;
    }
    (*out).total = n as u64;
    (*out).used = n as u64;
    (*out).flags = 0;
    0
}

unsafe extern "C" fn op_readdir(
    _ctx: *mut c_void,
    h: u32,
    name_out: *mut u8,
    name_cap: u32,
) -> u32 {
    done(pm_metal_fs_littlefs_op_readdir(h, name_out, name_cap))
}

unsafe extern "C" fn op_mkdir(ctx: *mut c_void, path: *const u8) -> u32 {
    done(pm_metal_fs_littlefs_op_mkdir(ctx as u32, path))
}

unsafe extern "C" fn op_unlink(ctx: *mut c_void, path: *const u8) -> u32 {
    done(pm_metal_fs_littlefs_op_unlink(ctx as u32, path))
}

unsafe extern "C" fn op_fsync(_ctx: *mut c_void, h: u32) -> u32 {
    done(pm_metal_fs_littlefs_op_fsync(h))
}
