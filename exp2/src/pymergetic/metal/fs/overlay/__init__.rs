//! Overlay fstype: RW upper shadows RO lower (mkdir/unlink/write hit upper).
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(dead_code, non_camel_case_types)]

use core::ffi::c_void;

use pymergetic_metal_fs::{
    pm_metal_fs_ops_register, pm_metal_fs_ops_t, pm_metal_fs_set_active_ops, pm_metal_fs_stat_t,
    PM_METAL_FS_INVALID, PM_METAL_FS_O_CREAT, PM_METAL_FS_O_DIRECTORY, PM_METAL_FS_O_RDWR,
    PM_METAL_FS_O_WRONLY, PM_METAL_FS_TYPE_DIR,
};
use pymergetic_metal_rt as _;
use pymergetic_metal_vfs as vfs;

extern "C" {
    fn pm_metal_async_completed_u32(v: u32) -> u32;
}

const MAX_OV: usize = 8;
const MAX_OPEN: usize = 32;

#[derive(Clone, Copy)]
struct Ov {
    lower_ops: *const pm_metal_fs_ops_t,
    lower_ctx: *mut c_void,
    upper_ops: *const pm_metal_fs_ops_t,
    upper_ctx: *mut c_void,
    used: bool,
}

#[derive(Clone, Copy)]
struct OpenH {
    ov: usize,
    on_upper: bool,
    inner: u32,
    used: bool,
}

static mut OVS: [Ov; MAX_OV] = [Ov {
    lower_ops: core::ptr::null(),
    lower_ctx: core::ptr::null_mut(),
    upper_ops: core::ptr::null(),
    upper_ctx: core::ptr::null_mut(),
    used: false,
}; MAX_OV];
static mut FILES: [OpenH; MAX_OPEN] = [OpenH {
    ov: 0,
    on_upper: false,
    inner: 0,
    used: false,
}; MAX_OPEN];
static mut OPS_READY: bool = false;
static OV_NAME: &[u8] = b"overlay\0";

static OV_OPS: pm_metal_fs_ops_t = pm_metal_fs_ops_t {
    name: OV_NAME.as_ptr(),
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
    fsync: None,
};

fn done(v: u32) -> u32 {
    unsafe { pm_metal_async_completed_u32(v) }
}

unsafe fn ensure_ops() {
    if !OPS_READY {
        let _ = pm_metal_fs_ops_register(&OV_OPS);
        OPS_READY = true;
    }
}

/// Mount overlay at `target`: writes go to upper; reads fall through to lower.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_overlay_mount(
    target: *const u8,
    lower_ops: *const pm_metal_fs_ops_t,
    lower_ctx: *mut c_void,
    upper_ops: *const pm_metal_fs_ops_t,
    upper_ctx: *mut c_void,
) -> i32 {
    if target.is_null() || lower_ops.is_null() || upper_ops.is_null() {
        return -1;
    }
    ensure_ops();
    let mut id = 1usize;
    while id < MAX_OV {
        if !OVS[id].used {
            break;
        }
        id += 1;
    }
    if id >= MAX_OV {
        return -1;
    }
    OVS[id] = Ov {
        lower_ops,
        lower_ctx,
        upper_ops,
        upper_ctx,
        used: true,
    };
    let ctx = id as *mut c_void;
    if vfs::pm_metal_vfs_mount(target, &OV_OPS as *const _ as *const c_void, ctx) == 0 {
        OVS[id].used = false;
        return -1;
    }
    0
}

unsafe fn ov_get(ctx: *mut c_void) -> Option<&'static Ov> {
    let id = ctx as usize;
    if id == 0 || id >= MAX_OV || !OVS[id].used {
        None
    } else {
        Some(&OVS[id])
    }
}

/* Direct open attempt: drivers return async handle; for Ready-completed, result_u32 works. */
extern "C" {
    fn pm_metal_async_result_u32(h: u32) -> u32;
}

unsafe fn open_res(ops: *const pm_metal_fs_ops_t, ctx: *mut c_void, path: *const u8, flags: u32) -> u32 {
    let Some(open) = (*ops).open else {
        return PM_METAL_FS_INVALID;
    };
    let h = open(ctx, path, flags);
    pm_metal_async_result_u32(h)
}

unsafe fn path_exists(ops: *const pm_metal_fs_ops_t, ctx: *mut c_void, path: *const u8) -> bool {
    let Some(stat) = (*ops).stat else {
        return false;
    };
    let mut st = pm_metal_fs_stat_t { size: 0, type_: 0 };
    let h = stat(ctx, path, &mut st as *mut _ as *mut u8);
    pm_metal_async_result_u32(h) != PM_METAL_FS_INVALID
}

unsafe extern "C" fn op_open(ctx: *mut c_void, path: *const u8, flags: u32) -> u32 {
    let Some(ov) = ov_get(ctx) else {
        return done(PM_METAL_FS_INVALID);
    };
    let want_write = (flags & (PM_METAL_FS_O_WRONLY | PM_METAL_FS_O_RDWR | PM_METAL_FS_O_CREAT)) != 0;
    let mut on_upper = false;
    let inner = if want_write || path_exists(ov.upper_ops, ov.upper_ctx, path) {
        on_upper = true;
        let f = if want_write {
            flags | PM_METAL_FS_O_CREAT
        } else {
            flags
        };
        open_res(ov.upper_ops, ov.upper_ctx, path, f)
    } else {
        open_res(ov.lower_ops, ov.lower_ctx, path, flags & !PM_METAL_FS_O_CREAT)
    };
    if inner == PM_METAL_FS_INVALID {
        return done(PM_METAL_FS_INVALID);
    }
    let mut slot = None;
    for i in 0..MAX_OPEN {
        if !FILES[i].used {
            slot = Some(i);
            break;
        }
    }
    let Some(fi) = slot else {
        return done(PM_METAL_FS_INVALID);
    };
    FILES[fi] = OpenH {
        ov: ctx as usize,
        on_upper,
        inner,
        used: true,
    };
    pm_metal_fs_set_active_ops(&OV_OPS, ctx);
    /* Activate underlying ops for fread dispatch through LAST_OPS — re-set to overlay. */
    pm_metal_fs_set_active_ops(&OV_OPS, ctx);
    let _ = PM_METAL_FS_O_DIRECTORY;
    let _ = PM_METAL_FS_TYPE_DIR;
    done(fi as u32)
}

unsafe fn with_inner(h: u32) -> Option<(bool, *const pm_metal_fs_ops_t, *mut c_void, u32)> {
    let f = FILES.get(h as usize)?;
    if !f.used {
        return None;
    }
    let ov = ov_get(f.ov as *mut c_void)?;
    if f.on_upper {
        Some((true, ov.upper_ops, ov.upper_ctx, f.inner))
    } else {
        Some((false, ov.lower_ops, ov.lower_ctx, f.inner))
    }
}

unsafe extern "C" fn op_close(_ctx: *mut c_void, h: u32) -> u32 {
    if let Some((_, ops, ctx, inner)) = with_inner(h) {
        if let Some(c) = (*ops).close {
            let _ = c(ctx, inner);
        }
    }
    if (h as usize) < MAX_OPEN {
        FILES[h as usize].used = false;
    }
    done(0)
}

unsafe extern "C" fn op_fread(_ctx: *mut c_void, h: u32, dest: *mut u8, len: u32) -> u32 {
    let Some((_, ops, ctx, inner)) = with_inner(h) else {
        return done(0);
    };
    let Some(f) = (*ops).fread else {
        return done(0);
    };
    f(ctx, inner, dest, len)
}

unsafe extern "C" fn op_fwrite(_ctx: *mut c_void, h: u32, src: *const u8, len: u32) -> u32 {
    let Some((on_upper, ops, ctx, inner)) = with_inner(h) else {
        return done(0);
    };
    if !on_upper {
        return done(0);
    }
    let Some(f) = (*ops).fwrite else {
        return done(0);
    };
    f(ctx, inner, src, len)
}

unsafe extern "C" fn op_fpread(_ctx: *mut c_void, h: u32, off: u32, dest: *mut u8, len: u32) -> u32 {
    let Some((_, ops, ctx, inner)) = with_inner(h) else {
        return done(0);
    };
    let Some(f) = (*ops).fpread else {
        return done(0);
    };
    f(ctx, inner, off, dest, len)
}

unsafe extern "C" fn op_fpwrite(_ctx: *mut c_void, h: u32, off: u32, src: *const u8, len: u32) -> u32 {
    let Some((on_upper, ops, ctx, inner)) = with_inner(h) else {
        return done(0);
    };
    if !on_upper {
        return done(0);
    }
    let Some(f) = (*ops).fpwrite else {
        return done(0);
    };
    f(ctx, inner, off, src, len)
}

unsafe extern "C" fn op_lseek(_ctx: *mut c_void, h: u32, off: i32, whence: u32) -> i32 {
    let Some((_, ops, ctx, inner)) = with_inner(h) else {
        return -1;
    };
    let Some(f) = (*ops).lseek else {
        return -1;
    };
    f(ctx, inner, off, whence)
}

unsafe extern "C" fn op_stat(ctx: *mut c_void, path: *const u8, st_out: *mut u8) -> u32 {
    let Some(ov) = ov_get(ctx) else {
        return done(PM_METAL_FS_INVALID);
    };
    if path_exists(ov.upper_ops, ov.upper_ctx, path) {
        if let Some(s) = (*ov.upper_ops).stat {
            return s(ov.upper_ctx, path, st_out);
        }
    }
    if let Some(s) = (*ov.lower_ops).stat {
        return s(ov.lower_ctx, path, st_out);
    }
    done(PM_METAL_FS_INVALID)
}

unsafe extern "C" fn op_readdir(ctx: *mut c_void, h: u32, name_out: *mut u8, name_cap: u32) -> u32 {
    /* v1: readdir upper only if open was on upper; else lower. Merge later. */
    let _ = ctx;
    let Some((_, ops, c, inner)) = with_inner(h) else {
        return done(0);
    };
    let Some(f) = (*ops).readdir else {
        return done(0);
    };
    f(c, inner, name_out, name_cap)
}

unsafe extern "C" fn op_mkdir(ctx: *mut c_void, path: *const u8) -> u32 {
    let Some(ov) = ov_get(ctx) else {
        return done(PM_METAL_FS_INVALID);
    };
    let Some(m) = (*ov.upper_ops).mkdir else {
        return done(PM_METAL_FS_INVALID);
    };
    m(ov.upper_ctx, path)
}

unsafe extern "C" fn op_unlink(ctx: *mut c_void, path: *const u8) -> u32 {
    let Some(ov) = ov_get(ctx) else {
        return done(PM_METAL_FS_INVALID);
    };
    let Some(u) = (*ov.upper_ops).unlink else {
        return done(PM_METAL_FS_INVALID);
    };
    u(ov.upper_ctx, path)
}
