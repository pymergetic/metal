//! RO VFS fstype over a wasmmod **MPWP** payload (zlib-per-file optional).
//!
//! Mount: `/mods/<pack.name>` — pack-relative paths are VFS paths under the mount.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(dead_code, non_camel_case_types)]

extern crate alloc;

use alloc::string::String;
use alloc::vec::Vec;
use core::ffi::c_void;
use core::ptr::{addr_of, addr_of_mut};

use pymergetic_metal_fs::{
    pm_metal_fs_ops_register, pm_metal_fs_ops_t, pm_metal_fs_set_active_ops, pm_metal_fs_stat_t,
    pm_metal_fs_statfs_t, PM_METAL_FS_INVALID, PM_METAL_FS_O_CREAT, PM_METAL_FS_O_DIRECTORY,
    PM_METAL_FS_O_RDWR, PM_METAL_FS_O_WRONLY, PM_METAL_FS_SEEK_CUR,
    PM_METAL_FS_SEEK_END, PM_METAL_FS_SEEK_SET, PM_METAL_FS_ST_RDONLY, PM_METAL_FS_TYPE_DIR,
    PM_METAL_FS_TYPE_FILE,
};
use pymergetic_metal_rt as _;
use pymergetic_metal_fs_vfs as vfs;

extern "C" {
    fn pm_metal_async_completed_u32(v: u32) -> u32;
}

const MAX_MOUNTS: usize = 8;
const MAX_OPEN: usize = 32;
const MAX_FILES: usize = 256;
const NAME_MAX: usize = 96;
const PATH_MAX: usize = 192;

const MPWP: &[u8; 4] = b"MPWP";
const FILE_FLAG_ZLIB: u8 = 1;

#[derive(Clone, Copy)]
struct FileEnt {
    path: [u8; PATH_MAX],
    path_len: usize,
    data: *const u8,
    data_len: u32,
    raw_len: u32,
    flags: u8,
    kind: u8,
}

#[derive(Clone, Copy)]
struct PackMount {
    used: bool,
    name: [u8; NAME_MAX],
    name_len: usize,
    files: [FileEnt; MAX_FILES],
    n_files: usize,
    artifact: *const u8,
    artifact_len: usize,
}

#[derive(Clone, Copy)]
struct OpenH {
    mount: usize,
    file: usize, /* MAX_FILES = directory cursor root */
    is_dir: bool,
    pos: u32,
    dir_idx: u32,
}

static mut MOUNTS: [PackMount; MAX_MOUNTS] = [PackMount {
    used: false,
    name: [0; NAME_MAX],
    name_len: 0,
    files: [FileEnt {
        path: [0; PATH_MAX],
        path_len: 0,
        data: core::ptr::null(),
        data_len: 0,
        raw_len: 0,
        flags: 0,
        kind: 0,
    }; MAX_FILES],
    n_files: 0,
    artifact: core::ptr::null(),
    artifact_len: 0,
}; MAX_MOUNTS];

static mut FILES: [Option<OpenH>; MAX_OPEN] = [const { None }; MAX_OPEN];
static mut OPS_READY: bool = false;

fn done(v: u32) -> u32 {
    unsafe { pm_metal_async_completed_u32(v) }
}

fn read_u16(p: &[u8], off: usize) -> Option<u16> {
    if off + 2 > p.len() {
        return None;
    }
    Some(u16::from_le_bytes([p[off], p[off + 1]]))
}

fn read_u32(p: &[u8], off: usize) -> Option<u32> {
    if off + 4 > p.len() {
        return None;
    }
    Some(u32::from_le_bytes([
        p[off],
        p[off + 1],
        p[off + 2],
        p[off + 3],
    ]))
}

fn cstr(p: *const u8) -> &'static str {
    if p.is_null() {
        return "";
    }
    unsafe {
        let mut n = 0usize;
        while *p.add(n) != 0 {
            n += 1;
            if n > 512 {
                break;
            }
        }
        core::str::from_utf8_unchecked(core::slice::from_raw_parts(p, n))
    }
}

fn norm_rel(path: &str) -> &str {
    let s = path.trim().trim_start_matches('/');
    if s.is_empty() || s == "." {
        ""
    } else {
        s.trim_end_matches('/')
    }
}

fn copy_bytes(dst: &mut [u8], src: &[u8]) -> Option<usize> {
    if src.len() >= dst.len() {
        return None;
    }
    dst[..src.len()].copy_from_slice(src);
    dst[src.len()] = 0;
    Some(src.len())
}

fn parse_mpwp(blob: &[u8], m: &mut PackMount) -> bool {
    if blob.len() < 14 || &blob[0..4] != MPWP {
        return false;
    }
    let version = match read_u16(blob, 4) {
        Some(v) => v,
        None => return false,
    };
    if version < 1 || version > 3 {
        return false;
    }
    let name_len = match read_u16(blob, 8) {
        Some(v) => v as usize,
        None => return false,
    };
    if 10 + name_len + 4 > blob.len() {
        return false;
    }
    let name = &blob[10..10 + name_len];
    let Some(nl) = copy_bytes(&mut m.name, name) else {
        return false;
    };
    m.name_len = nl;
    let mut off = 10 + name_len;
    let n_files = match read_u32(blob, off) {
        Some(v) => v as usize,
        None => return false,
    };
    off += 4;
    if n_files > MAX_FILES {
        return false;
    }
    let v3 = version >= 3;
    m.n_files = 0;
    for _ in 0..n_files {
        let path_len = match read_u16(blob, off) {
            Some(v) => v as usize,
            None => return false,
        };
        off += 2;
        let hdr = path_len + 1 + 4 + if v3 { 1 + 4 } else { 0 };
        if off + hdr > blob.len() {
            return false;
        }
        let path = &blob[off..off + path_len];
        off += path_len;
        let kind = blob[off];
        off += 1;
        let (flags, raw_len) = if v3 {
            let flags = blob[off];
            off += 1;
            let raw_len = match read_u32(blob, off) {
                Some(v) => v,
                None => return false,
            };
            off += 4;
            (flags, raw_len)
        } else {
            (0u8, 0u32)
        };
        let data_len = match read_u32(blob, off) {
            Some(v) => v,
            None => return false,
        };
        off += 4;
        if off + data_len as usize > blob.len() {
            return false;
        }
        /* Product VFS: uncompressed only (zlib needs inflate host). */
        if (flags & FILE_FLAG_ZLIB) != 0 {
            off += data_len as usize;
            continue;
        }
        let i = m.n_files;
        let Some(pl) = copy_bytes(&mut m.files[i].path, path) else {
            return false;
        };
        m.files[i].path_len = pl;
        m.files[i].kind = kind;
        m.files[i].flags = flags;
        m.files[i].data = unsafe { blob.as_ptr().add(off) };
        m.files[i].data_len = data_len;
        m.files[i].raw_len = if v3 { raw_len } else { data_len };
        m.n_files += 1;
        off += data_len as usize;
    }
    m.artifact = blob.as_ptr();
    m.artifact_len = blob.len();
    m.used = true;
    true
}

fn path_str(e: &FileEnt) -> &str {
    core::str::from_utf8(&e.path[..e.path_len]).unwrap_or("")
}

fn find_file(m: &PackMount, rel: &str) -> Option<usize> {
    let want = norm_rel(rel);
    for i in 0..m.n_files {
        if path_str(&m.files[i]) == want {
            return Some(i);
        }
    }
    None
}

fn is_dir_prefix(m: &PackMount, rel: &str) -> bool {
    let want = norm_rel(rel);
    if want.is_empty() {
        return true;
    }
    let prefix = {
        let mut s = String::from(want);
        s.push('/');
        s
    };
    for i in 0..m.n_files {
        let p = path_str(&m.files[i]);
        if p.starts_with(prefix.as_str()) || p == want {
            if p == want {
                return false; /* exact file */
            }
            return true;
        }
    }
    false
}

unsafe fn ensure_ops() {
    if !*addr_of!(OPS_READY) {
        let _ = pm_metal_fs_ops_register(&WASMMOD_OPS);
        *addr_of_mut!(OPS_READY) = true;
    }
}

static WASMMOD_OPS: pm_metal_fs_ops_t = pm_metal_fs_ops_t {
    name: b"wasmmod\0".as_ptr(),
    open: Some(op_open),
    close: Some(op_close),
    fread: Some(op_fread),
    fwrite: None,
    fpread: Some(op_fpread),
    fpwrite: None,
    lseek: Some(op_lseek),
    stat: Some(op_stat),
    readdir: Some(op_readdir),
    mkdir: None,
    unlink: None,
    rename: None,
    fsync: Some(op_fsync),
    statfs: Some(op_statfs),
};

/// Mount raw MPWP bytes. If `target` is null, mounts at `/mods/<pack.name>`.
/// Returns 0 ok (including already mounted), -1 fail. Artifact must outlive the mount.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_wasmmod_mount_mpwp(
    target: *const u8,
    mpwp: *const u8,
    mpwp_len: usize,
) -> i32 {
    if mpwp.is_null() || mpwp_len == 0 {
        return -1;
    }
    ensure_ops();
    let blob = core::slice::from_raw_parts(mpwp, mpwp_len);
    /* Peek pack name without a MAX_FILES stack frame (firmware stack is small). */
    if blob.len() < 14 || &blob[0..4] != MPWP {
        return -1;
    }
    let version = match read_u16(blob, 4) {
        Some(v) if (1..=3).contains(&v) => v,
        _ => return -1,
    };
    let _ = version;
    let name_len = match read_u16(blob, 8) {
        Some(v) => v as usize,
        None => return -1,
    };
    if 10 + name_len > blob.len() || name_len >= NAME_MAX {
        return -1;
    }
    let mut mount_path = [0u8; NAME_MAX + 8];
    let tgt_ptr = if target.is_null() {
        let name = &blob[10..10 + name_len];
        mount_path[0] = b'/';
        mount_path[1] = b'm';
        mount_path[2] = b'o';
        mount_path[3] = b'd';
        mount_path[4] = b's';
        mount_path[5] = b'/';
        mount_path[6..6 + name_len].copy_from_slice(name);
        mount_path[6 + name_len] = 0;
        mount_path.as_ptr()
    } else {
        target
    };
    let tgt_s = cstr(tgt_ptr);
    let mounts = &mut *addr_of_mut!(MOUNTS);
    for i in 0..MAX_MOUNTS {
        if !mounts[i].used {
            continue;
        }
        if mounts[i].artifact == mpwp {
            return 0;
        }
        if target.is_null() {
            let n = core::str::from_utf8(&mounts[i].name[..mounts[i].name_len]).unwrap_or("");
            let mut existing = [0u8; NAME_MAX + 8];
            let prefix = b"/mods/";
            if 6 + n.len() < existing.len() {
                existing[..6].copy_from_slice(prefix);
                existing[6..6 + n.len()].copy_from_slice(n.as_bytes());
                let es =
                    core::str::from_utf8(&existing[..6 + n.len()]).unwrap_or("");
                if es == tgt_s {
                    return 0;
                }
            }
        }
    }
    let mut slot = None;
    for i in 0..MAX_MOUNTS {
        if !mounts[i].used {
            slot = Some(i);
            break;
        }
    }
    let Some(si) = slot else {
        return -1;
    };
    /* Mutate in place — never construct PackMount on the call stack. */
    mounts[si].used = false;
    mounts[si].name_len = 0;
    mounts[si].n_files = 0;
    mounts[si].artifact = core::ptr::null();
    mounts[si].artifact_len = 0;
    if !parse_mpwp(blob, &mut mounts[si]) {
        mounts[si].used = false;
        return -1;
    }
    let ctx = si as *mut c_void;
    if vfs::pm_metal_fs_vfs_mount(tgt_ptr, &WASMMOD_OPS as *const _ as *const c_void, ctx) == 0 {
        mounts[si].used = false;
        return -1;
    }
    0
}

unsafe extern "C" fn op_open(ctx: *mut c_void, path: *const u8, flags: u32) -> u32 {
    let mi = ctx as usize;
    let mounts = &*addr_of!(MOUNTS);
    let files = &mut *addr_of_mut!(FILES);
    if mi >= MAX_MOUNTS || !mounts[mi].used {
        return done(PM_METAL_FS_INVALID);
    }
    let m = &mounts[mi];
    let rel = norm_rel(cstr(path));
    let want_dir = (flags & PM_METAL_FS_O_DIRECTORY) != 0 || rel.is_empty();
    let mode = flags & 3;
    if mode == PM_METAL_FS_O_WRONLY
        || mode == PM_METAL_FS_O_RDWR
        || (flags & PM_METAL_FS_O_CREAT) != 0
    {
        return done(PM_METAL_FS_INVALID);
    }
    let (is_dir, file_i) = if want_dir {
        if !is_dir_prefix(m, rel) && find_file(m, rel).is_some() {
            return done(PM_METAL_FS_INVALID);
        }
        if !is_dir_prefix(m, rel) && !rel.is_empty() {
            return done(PM_METAL_FS_INVALID);
        }
        (true, MAX_FILES)
    } else {
        match find_file(m, rel) {
            Some(i) => (false, i),
            None => return done(PM_METAL_FS_INVALID),
        }
    };
    let mut slot = None;
    for i in 0..MAX_OPEN {
        if files[i].is_none() {
            slot = Some(i);
            break;
        }
    }
    let Some(fi) = slot else {
        return done(PM_METAL_FS_INVALID);
    };
    files[fi] = Some(OpenH {
        mount: mi,
        file: file_i,
        is_dir,
        pos: 0,
        dir_idx: 0,
    });
    pm_metal_fs_set_active_ops(&WASMMOD_OPS, ctx);
    done(fi as u32)
}

unsafe extern "C" fn op_close(_ctx: *mut c_void, h: u32) -> u32 {
    if (h as usize) < MAX_OPEN {
        (*addr_of_mut!(FILES))[h as usize] = None;
    }
    done(0)
}

unsafe extern "C" fn op_fread(ctx: *mut c_void, h: u32, dest: *mut u8, len: u32) -> u32 {
    let files = &*addr_of!(FILES);
    let pos = files
        .get(h as usize)
        .and_then(|f| f.as_ref())
        .map(|f| f.pos)
        .unwrap_or(0);
    op_fpread(ctx, h, pos, dest, len)
}

unsafe extern "C" fn op_fpread(
    _ctx: *mut c_void,
    h: u32,
    off: u32,
    dest: *mut u8,
    len: u32,
) -> u32 {
    let files = &mut *addr_of_mut!(FILES);
    let mounts = &*addr_of!(MOUNTS);
    let Some(f) = files.get_mut(h as usize).and_then(|x| x.as_mut()) else {
        return done(0);
    };
    if f.is_dir || dest.is_null() || f.file >= MAX_FILES {
        return done(0);
    }
    let m = &mounts[f.mount];
    let e = &m.files[f.file];
    let avail = (e.data_len as usize).saturating_sub(off as usize);
    let n = core::cmp::min(avail, len as usize);
    for i in 0..n {
        *dest.add(i) = *e.data.add(off as usize + i);
    }
    f.pos = off + n as u32;
    done(n as u32)
}

unsafe extern "C" fn op_lseek(_ctx: *mut c_void, h: u32, off: i32, whence: u32) -> i32 {
    let files = &mut *addr_of_mut!(FILES);
    let mounts = &*addr_of!(MOUNTS);
    let Some(f) = files.get_mut(h as usize).and_then(|x| x.as_mut()) else {
        return -1;
    };
    let end = if f.is_dir || f.file >= MAX_FILES {
        0i32
    } else {
        mounts[f.mount].files[f.file].data_len as i32
    };
    let cur = f.pos as i32;
    let np = match whence {
        PM_METAL_FS_SEEK_SET => off,
        PM_METAL_FS_SEEK_CUR => cur + off,
        PM_METAL_FS_SEEK_END => end + off,
        _ => return -1,
    };
    if np < 0 {
        return -1;
    }
    f.pos = np as u32;
    np
}

unsafe extern "C" fn op_stat(ctx: *mut c_void, path: *const u8, st_out: *mut u8) -> u32 {
    if st_out.is_null() {
        return done(PM_METAL_FS_INVALID);
    }
    let mi = ctx as usize;
    let mounts = &*addr_of!(MOUNTS);
    if mi >= MAX_MOUNTS || !mounts[mi].used {
        return done(PM_METAL_FS_INVALID);
    }
    let m = &mounts[mi];
    let rel = norm_rel(cstr(path));
    let st = st_out as *mut pm_metal_fs_stat_t;
    if let Some(i) = find_file(m, rel) {
        (*st).size = m.files[i].data_len;
        (*st).type_ = PM_METAL_FS_TYPE_FILE;
        return done(0);
    }
    if is_dir_prefix(m, rel) {
        (*st).size = 0;
        (*st).type_ = PM_METAL_FS_TYPE_DIR;
        return done(0);
    }
    done(PM_METAL_FS_INVALID)
}

unsafe extern "C" fn op_readdir(
    _ctx: *mut c_void,
    h: u32,
    name_out: *mut u8,
    name_cap: u32,
) -> u32 {
    if name_out.is_null() || name_cap == 0 {
        return done(PM_METAL_FS_INVALID);
    }
    let files = &mut *addr_of_mut!(FILES);
    let mounts = &*addr_of!(MOUNTS);
    let Some(f) = files.get_mut(h as usize).and_then(|x| x.as_mut()) else {
        return done(PM_METAL_FS_INVALID);
    };
    if !f.is_dir {
        return done(PM_METAL_FS_INVALID);
    }
    let m = &mounts[f.mount];
    /* Root listing: unique first path component. */
    let mut seen: Vec<String> = Vec::new();
    let mut idx = 0u32;
    for i in 0..m.n_files {
        let p = path_str(&m.files[i]);
        let comp = match p.find('/') {
            Some(j) => &p[..j],
            None => p,
        };
        if comp.is_empty() {
            continue;
        }
        if seen.iter().any(|s| s == comp) {
            continue;
        }
        seen.push(String::from(comp));
        if idx == f.dir_idx {
            let n = core::cmp::min(comp.len(), name_cap as usize - 1);
            for k in 0..n {
                *name_out.add(k) = comp.as_bytes()[k];
            }
            *name_out.add(n) = 0;
            f.dir_idx += 1;
            return done(0);
        }
        idx += 1;
    }
    done(PM_METAL_FS_INVALID)
}

unsafe extern "C" fn op_fsync(_ctx: *mut c_void, _h: u32) -> u32 {
    done(0)
}

unsafe extern "C" fn op_statfs(ctx: *mut c_void, out: *mut pm_metal_fs_statfs_t) -> i32 {
    if out.is_null() {
        return -1;
    }
    let mi = ctx as usize;
    let mounts = &*addr_of!(MOUNTS);
    if mi >= MAX_MOUNTS || !mounts[mi].used {
        return -1;
    }
    let mut used = 0u64;
    for i in 0..mounts[mi].n_files {
        used += mounts[mi].files[i].data_len as u64;
    }
    (*out).total = mounts[mi].artifact_len as u64;
    (*out).used = used;
    (*out).flags = PM_METAL_FS_ST_RDONLY;
    0
}


use pymergetic_metal_reg::{pm_metal_reg_mod_load, RegMod};

pymergetic_metal_reg::reg_mod! {
    mod wasmmod = "pymergetic.metal.fs.wasmmod";
    exports: [mount_mpwp];
}

extern "C" fn wasmmod_register_symbols(_ctx: *mut c_void) -> i32 {
    wasmmod::mount_mpwp.publish(pm_metal_fs_wasmmod_mount_mpwp as *const c_void);
    0
}

static WASMMOD_MOD: RegMod = RegMod::from_static(
    wasmmod::NAME,
    &wasmmod::STORAGE.exports,
    &wasmmod::STORAGE.imports,
    Some(wasmmod_register_symbols),
);

#[no_mangle]
pub extern "C" fn pm_metal_fs_wasmmod_reg_load() -> i32 {
    if pymergetic_metal_reg::find_mod(wasmmod::NAME).is_some() {
        return 0;
    }
    unsafe { pm_metal_reg_mod_load(&WASMMOD_MOD) }
}

#[inline]
pub fn reg_load() -> i32 {
    pm_metal_fs_wasmmod_reg_load()
}
