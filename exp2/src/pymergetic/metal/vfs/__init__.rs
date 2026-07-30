//! Mount table — resolve paths to (ops, ctx, relative path). No open/read.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(dead_code, non_camel_case_types)]

use core::ffi::c_void;
use core::ptr::{addr_of, addr_of_mut};
use core::sync::atomic::{AtomicU32, Ordering};

use pymergetic_metal_rt as _;

/// Opaque mount id (0 = invalid).
pub type pm_metal_vfs_mount_h = u32;

const MAX_MOUNTS: usize = 32;
const TARGET_MAX: usize = 128;

#[repr(C)]
pub struct pm_metal_vfs_resolve_t {
    pub ops: *const c_void,
    pub ctx: *mut c_void,
    pub rel: *const u8,
    pub mount: pm_metal_vfs_mount_h,
}

#[derive(Clone, Copy)]
struct Mount {
    target: [u8; TARGET_MAX],
    target_len: usize,
    ops: *const c_void,
    ctx: *mut c_void,
    id: u32,
    used: bool,
}

static mut MOUNTS: [Mount; MAX_MOUNTS] = [Mount {
    target: [0; TARGET_MAX],
    target_len: 0,
    ops: core::ptr::null(),
    ctx: core::ptr::null_mut(),
    id: 0,
    used: false,
}; MAX_MOUNTS];
static NEXT_ID: AtomicU32 = AtomicU32::new(1);
static mut REL_BUF: [u8; 512] = [0; 512];

fn copy_target(dst: &mut [u8], src: &str) -> Option<usize> {
    let s = if src.is_empty() || src == "/" {
        "/"
    } else {
        src.trim_end_matches('/')
    };
    let b = s.as_bytes();
    if b.len() >= dst.len() {
        return None;
    }
    dst[..b.len()].copy_from_slice(b);
    dst[b.len()] = 0;
    Some(b.len())
}

fn target_str(m: &Mount) -> &str {
    core::str::from_utf8(&m.target[..m.target_len]).unwrap_or("")
}

/// Mount `ops`+`ctx` at absolute `target` (e.g. `/mods`). Returns mount id or 0.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_vfs_mount(
    target: *const u8,
    ops: *const c_void,
    ctx: *mut c_void,
) -> pm_metal_vfs_mount_h {
    if target.is_null() || ops.is_null() {
        return 0;
    }
    let t = cstr(target);
    let mounts = &mut *addr_of_mut!(MOUNTS);
    let mut slot = None;
    for i in 0..MAX_MOUNTS {
        if mounts[i].used && target_str(&mounts[i]) == norm_view(t) {
            slot = Some(i);
            break;
        }
        if !mounts[i].used && slot.is_none() {
            slot = Some(i);
        }
    }
    let Some(i) = slot else {
        return 0;
    };
    let Some(len) = copy_target(&mut mounts[i].target, t) else {
        return 0;
    };
    let id = NEXT_ID.fetch_add(1, Ordering::Relaxed);
    mounts[i].target_len = len;
    mounts[i].ops = ops;
    mounts[i].ctx = ctx;
    mounts[i].id = id;
    mounts[i].used = true;
    id
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_vfs_umount(target: *const u8) -> i32 {
    if target.is_null() {
        return -1;
    }
    let t = norm_view(cstr(target));
    let mounts = &mut *addr_of_mut!(MOUNTS);
    for i in 0..MAX_MOUNTS {
        if mounts[i].used && target_str(&mounts[i]) == t {
            mounts[i].used = false;
            return 0;
        }
    }
    -1
}

/// Longest-prefix resolve. On success fills `out` (`rel` points at static buf).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_vfs_resolve(
    path: *const u8,
    out: *mut pm_metal_vfs_resolve_t,
) -> i32 {
    if path.is_null() || out.is_null() {
        return -1;
    }
    let p = cstr(path);
    let p = if p.is_empty() { "/" } else { p };
    let mounts = &*addr_of!(MOUNTS);
    let rel_buf = &mut *addr_of_mut!(REL_BUF);
    let mut best: Option<usize> = None;
    let mut best_len = 0usize;
    for i in 0..MAX_MOUNTS {
        if !mounts[i].used {
            continue;
        }
        let mt = target_str(&mounts[i]);
        let hit = if mt == "/" {
            true
        } else if p == mt {
            true
        } else if p.starts_with(mt) {
            let rest = &p[mt.len()..];
            rest.is_empty() || rest.starts_with('/')
        } else {
            false
        };
        if hit && mt.len() >= best_len {
            best_len = mt.len();
            best = Some(i);
        }
    }
    let Some(i) = best else {
        return -1;
    };
    let mt = target_str(&mounts[i]);
    let rel = if mt == "/" {
        if p == "/" {
            ""
        } else {
            p.trim_start_matches('/')
        }
    } else if p.len() == mt.len() {
        ""
    } else {
        p[mt.len()..].trim_start_matches('/')
    };
    *rel_buf = [0u8; 512];
    let rb = rel.as_bytes();
    if rb.len() >= rel_buf.len() {
        return -1;
    }
    rel_buf[..rb.len()].copy_from_slice(rb);
    rel_buf[rb.len()] = 0;
    (*out).ops = mounts[i].ops;
    (*out).ctx = mounts[i].ctx;
    (*out).rel = addr_of!(REL_BUF).cast::<u8>();
    (*out).mount = mounts[i].id;
    0
}

fn norm_view(t: &str) -> &str {
    if t.is_empty() || t == "/" {
        "/"
    } else {
        t.trim_end_matches('/')
    }
}

fn cstr<'a>(p: *const u8) -> &'a str {
    unsafe {
        let mut n = 0usize;
        while *p.add(n) != 0 {
            n += 1;
            if n > 1024 {
                break;
            }
        }
        core::str::from_utf8_unchecked(core::slice::from_raw_parts(p, n))
    }
}
