//! `.mtar` (lz4-in-ustar) fstype — RO borrow or RW owned blob + pack builder.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(dead_code, non_camel_case_types)]

extern crate alloc;

use alloc::string::String;
use alloc::vec::Vec;
use core::ffi::c_void;
use core::ptr::{addr_of, addr_of_mut};
use core::sync::atomic::{AtomicU32, Ordering};

use pymergetic_metal_fs::{
    pm_metal_fs_ops_register, pm_metal_fs_ops_t, pm_metal_fs_set_active_ops, pm_metal_fs_stat_t,
    pm_metal_fs_statfs_t, PM_METAL_FS_INVALID, PM_METAL_FS_O_CREAT, PM_METAL_FS_O_DIRECTORY,
    PM_METAL_FS_O_RDONLY, PM_METAL_FS_O_RDWR, PM_METAL_FS_O_TRUNC, PM_METAL_FS_O_WRONLY,
    PM_METAL_FS_SEEK_CUR, PM_METAL_FS_SEEK_END, PM_METAL_FS_SEEK_SET, PM_METAL_FS_ST_RDONLY,
    PM_METAL_FS_TYPE_DIR, PM_METAL_FS_TYPE_FILE,
};
use pymergetic_metal_rt as _;
use pymergetic_metal_util_lz4 as _;
use pymergetic_metal_util_tar as _;
use pymergetic_metal_fs_vfs as vfs;

extern "C" {
    fn pm_metal_async_completed_u32(v: u32) -> u32;
    fn pm_metal_util_lz4_compress(
        src: *const u8,
        src_len: usize,
        dst: *mut u8,
        dst_cap: usize,
    ) -> i32;
    fn pm_metal_util_lz4_compress_bound(src_len: usize) -> usize;
    fn pm_metal_util_lz4_decompress_safe(
        src: *const u8,
        src_len: usize,
        dst: *mut u8,
        dst_cap: usize,
        out_len: *mut usize,
    ) -> i32;
    fn pm_metal_util_tar_foreach_ex(
        archive: *const u8,
        len: usize,
        cb: Option<
            unsafe extern "C" fn(
                *mut u8,
                *const u8,
                u64,
                i32,
                u64,
                u64,
                *const u8,
                usize,
            ) -> i32,
        >,
        ctx: *mut u8,
    ) -> i32;
    fn pm_metal_util_tar_write_header(
        out: *mut u8,
        out_cap: usize,
        name: *const u8,
        size: u64,
        typeflag: u8,
    ) -> i32;
    fn pm_metal_util_tar_pad_len(size: u64) -> usize;
    fn pm_metal_util_tar_write_end(out: *mut u8, out_cap: usize) -> i32;
}

const TOC_NAME: &[u8] = b"__metal_toc__\0";
const MAX_FILES: usize = 256;
const MAX_OPEN: usize = 32;
const NAME_MAX: usize = 100;

#[derive(Clone)]
struct TocEntry {
    path: String,
    payload_off: u64,
    comp_len: u32,
    uncomp_len: u32,
    is_dir: bool,
}

struct Arch {
    /// Borrowed RO blob, or pointer into `owned` when RW.
    blob: *const u8,
    len: usize,
    toc: Vec<TocEntry>,
    /// When set, mount is RW; blob/len track the live packed image.
    owned: Option<Vec<u8>>,
    rw: bool,
}

struct File {
    used: bool,
    arch: usize,
    toc_i: usize,
    pos: u32,
    cache: Vec<u8>,
    writable: bool,
    dirty: bool,
    path: String,
}

static mut ARCHES: [Option<Arch>; 16] = [const { None }; 16];
static mut FILES: [Option<File>; MAX_OPEN] = [const { None }; MAX_OPEN];
static NEXT_ARCH: AtomicU32 = AtomicU32::new(1);

fn done(v: u32) -> u32 {
    unsafe { pm_metal_async_completed_u32(v) }
}

/// Open a `.mtar` blob in place. Returns arch ctx id (nonzero) or 0.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_mtar_open_blob(blob: *const u8, len: usize) -> u32 {
    if blob.is_null() || len == 0 {
        return 0;
    }
    let mut toc = Vec::new();
    let mut ctx = ScanCtx {
        toc: &mut toc,
        blob,
        len,
    };
    let rc = pm_metal_util_tar_foreach_ex(
        blob,
        len,
        Some(scan_cb),
        &mut ctx as *mut ScanCtx as *mut u8,
    );
    if rc < 0 || toc.is_empty() {
        /* No TOC member — build from all entries (uncomp unknown = comp). */
        toc.clear();
        let mut ctx2 = ScanCtx {
            toc: &mut toc,
            blob,
            len,
        };
        let _ = pm_metal_util_tar_foreach_ex(
            blob,
            len,
            Some(scan_all_cb),
            &mut ctx2 as *mut ScanCtx as *mut u8,
        );
    }
    let arches = &mut *addr_of_mut!(ARCHES);
    let mut id = 1usize;
    while id < 16 {
        if arches[id].is_none() {
            break;
        }
        id += 1;
    }
    if id >= 16 {
        return 0;
    }
    let _ = NEXT_ARCH.fetch_add(1, Ordering::Relaxed);
    arches[id] = Some(Arch {
        blob,
        len,
        toc,
        owned: None,
        rw: false,
    });
    id as u32
}

/// Open a growable owned copy of `blob` for RW mounts. Returns arch id or 0.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_mtar_open_owned(blob: *const u8, len: usize, cap: usize) -> u32 {
    if blob.is_null() || len == 0 {
        return 0;
    }
    let id = pm_metal_fs_mtar_open_blob(blob, len);
    if id == 0 {
        return 0;
    }
    let arches = &mut *addr_of_mut!(ARCHES);
    let Some(arch) = arches[id as usize].as_mut() else {
        return 0;
    };
    let mut owned = Vec::with_capacity(core::cmp::max(cap, len));
    owned.extend_from_slice(core::slice::from_raw_parts(blob, len));
    arch.len = owned.len();
    arch.owned = Some(owned);
    arch.blob = arch.owned.as_ref().unwrap().as_ptr();
    arch.rw = true;
    id
}

struct ScanCtx<'a> {
    toc: &'a mut Vec<TocEntry>,
    blob: *const u8,
    len: usize,
}

unsafe extern "C" fn scan_cb(
    ctx: *mut u8,
    name: *const u8,
    size: u64,
    is_dir: i32,
    _header_off: u64,
    payload_off: u64,
    data: *const u8,
    data_len: usize,
) -> i32 {
    let c = &mut *(ctx as *mut ScanCtx);
    let n = cstr(name);
    if n == "__metal_toc__" && is_dir == 0 && !data.is_null() {
        parse_toc(c.toc, data, data_len);
        return 0;
    }
    let _ = (size, payload_off, c.blob, c.len);
    0
}

unsafe extern "C" fn scan_all_cb(
    ctx: *mut u8,
    name: *const u8,
    size: u64,
    is_dir: i32,
    _header_off: u64,
    payload_off: u64,
    _data: *const u8,
    _data_len: usize,
) -> i32 {
    let c = &mut *(ctx as *mut ScanCtx);
    let n = cstr(name);
    if n == "__metal_toc__" {
        return 0;
    }
    if c.toc.len() >= MAX_FILES {
        return -1;
    }
    c.toc.push(TocEntry {
        path: String::from(n.trim_end_matches('/')),
        payload_off,
        comp_len: size as u32,
        uncomp_len: size as u32,
        is_dir: is_dir != 0,
    });
    0
}

fn parse_toc(toc: &mut Vec<TocEntry>, data: *const u8, len: usize) {
    /* rows: u16 nlen | path | u64 off | u32 comp | u32 uncomp | u8 is_dir */
    let mut i = 0usize;
    unsafe {
        while i + 2 <= len {
            let nlen = u16::from_le_bytes([*data.add(i), *data.add(i + 1)]) as usize;
            i += 2;
            if i + nlen + 8 + 4 + 4 + 1 > len {
                break;
            }
            let path = core::str::from_utf8_unchecked(core::slice::from_raw_parts(data.add(i), nlen));
            i += nlen;
            let mut offb = [0u8; 8];
            for k in 0..8 {
                offb[k] = *data.add(i + k);
            }
            let payload_off = u64::from_le_bytes(offb);
            i += 8;
            let comp = u32::from_le_bytes([
                *data.add(i),
                *data.add(i + 1),
                *data.add(i + 2),
                *data.add(i + 3),
            ]);
            i += 4;
            let uncomp = u32::from_le_bytes([
                *data.add(i),
                *data.add(i + 1),
                *data.add(i + 2),
                *data.add(i + 3),
            ]);
            i += 4;
            let is_dir = *data.add(i) != 0;
            i += 1;
            if toc.len() < MAX_FILES {
                toc.push(TocEntry {
                    path: String::from(path),
                    payload_off,
                    comp_len: comp,
                    uncomp_len: uncomp,
                    is_dir,
                });
            }
        }
    }
}

/// Mount RO blob at `target` (registers mtar ops). Returns 0 ok.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_mtar_mount(target: *const u8, blob: *const u8, len: usize) -> i32 {
    let id = pm_metal_fs_mtar_open_blob(blob, len);
    if id == 0 {
        return -1;
    }
    ensure_ops_registered();
    let ctx = id as usize as *mut c_void;
    if vfs::pm_metal_fs_vfs_mount(target, &MTAR_OPS as *const _ as *const c_void, ctx) == 0 {
        return -1;
    }
    0
}

/// Mount RW owned copy. `blob_mut`/`len` seed; `cap` is initial Vec capacity hint.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_mtar_mount_rw(
    target: *const u8,
    blob_mut: *mut u8,
    len: usize,
    cap: usize,
) -> i32 {
    let id = pm_metal_fs_mtar_open_owned(blob_mut, len, cap);
    if id == 0 {
        return -1;
    }
    ensure_ops_registered();
    let ctx = id as usize as *mut c_void;
    if vfs::pm_metal_fs_vfs_mount(target, &MTAR_RW_OPS as *const _ as *const c_void, ctx) == 0 {
        (*addr_of_mut!(ARCHES))[id as usize] = None;
        return -1;
    }
    0
}

static mut OPS_READY: bool = false;
static MTAR_NAME: &[u8] = b"mtar\0";
static MTAR_RW_NAME: &[u8] = b"mtar-rw\0";

static MTAR_OPS: pm_metal_fs_ops_t = pm_metal_fs_ops_t {
    name: MTAR_NAME.as_ptr(),
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
    fsync: None,
    statfs: Some(op_statfs_ro),
};

static MTAR_RW_OPS: pm_metal_fs_ops_t = pm_metal_fs_ops_t {
    name: MTAR_RW_NAME.as_ptr(),
    open: Some(op_open_rw),
    close: Some(op_close_rw),
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
    statfs: Some(op_statfs_rw),
};

unsafe fn ensure_ops_registered() {
    if !*addr_of!(OPS_READY) {
        let _ = pm_metal_fs_ops_register(&MTAR_OPS);
        let _ = pm_metal_fs_ops_register(&MTAR_RW_OPS);
        *addr_of_mut!(OPS_READY) = true;
    }
}

unsafe extern "C" fn op_open(ctx: *mut c_void, path: *const u8, flags: u32) -> u32 {
    let arch_id = ctx as usize;
    let arches = &*addr_of!(ARCHES);
    let files = &mut *addr_of_mut!(FILES);
    if arches.get(arch_id).and_then(|a| a.as_ref()).is_none() {
        return done(PM_METAL_FS_INVALID);
    }
    let path = cstr(path);
    let arch = arches[arch_id].as_ref().unwrap();
    let mut found = None;
    for (i, e) in arch.toc.iter().enumerate() {
        if e.path == path || (path.is_empty() && e.is_dir) {
            found = Some(i);
            break;
        }
    }
    let Some(ti) = found else {
        return done(PM_METAL_FS_INVALID);
    };
    let e = &arch.toc[ti];
    if e.is_dir && (flags & PM_METAL_FS_O_DIRECTORY) == 0 && flags != PM_METAL_FS_O_RDONLY {
        /* allow dir open for readdir with O_RDONLY|O_DIRECTORY or plain */
    }
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
    let mut cache = Vec::new();
    if !e.is_dir && e.uncomp_len > 0 {
        cache.resize(e.uncomp_len as usize, 0);
        let src = arch.blob.add(e.payload_off as usize);
        let mut out_len = 0usize;
        let rc = pm_metal_util_lz4_decompress_safe(
            src,
            e.comp_len as usize,
            cache.as_mut_ptr(),
            cache.len(),
            &mut out_len,
        );
        if rc != 0 {
            if (e.comp_len as usize) <= cache.len() {
                for i in 0..(e.comp_len as usize) {
                    cache[i] = *src.add(i);
                }
                cache.truncate(e.comp_len as usize);
            } else {
                return done(PM_METAL_FS_INVALID);
            }
        } else {
            cache.truncate(out_len);
        }
    }
    files[fi] = Some(File {
        used: true,
        arch: arch_id,
        toc_i: ti,
        pos: 0,
        cache,
        writable: false,
        dirty: false,
        path: e.path.clone(),
    });
    pm_metal_fs_set_active_ops(&MTAR_OPS, ctx);
    done(fi as u32)
}

unsafe extern "C" fn op_open_rw(ctx: *mut c_void, path: *const u8, flags: u32) -> u32 {
    let arch_id = ctx as usize;
    let arches = &*addr_of!(ARCHES);
    let files = &mut *addr_of_mut!(FILES);
    let Some(arch) = arches.get(arch_id).and_then(|a| a.as_ref()) else {
        return done(PM_METAL_FS_INVALID);
    };
    if !arch.rw {
        return done(PM_METAL_FS_INVALID);
    }
    let path_s = cstr(path);
    let want_dir = (flags & PM_METAL_FS_O_DIRECTORY) != 0;
    let creat = (flags & PM_METAL_FS_O_CREAT) != 0;
    let trunc = (flags & PM_METAL_FS_O_TRUNC) != 0;
    let write = (flags & PM_METAL_FS_O_WRONLY) != 0
        || (flags & 3) == PM_METAL_FS_O_RDWR
        || creat
        || trunc;

    let mut found = None;
    for (i, e) in arch.toc.iter().enumerate() {
        if e.path == path_s || (path_s.is_empty() && e.is_dir) {
            found = Some(i);
            break;
        }
    }
    /* Root dir synthetic */
    if path_s.is_empty() {
        found = found.or(Some(usize::MAX));
    }

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

    if let Some(ti) = found {
        if ti == usize::MAX {
            files[fi] = Some(File {
                used: true,
                arch: arch_id,
                toc_i: 0,
                pos: 0,
                cache: Vec::new(),
                writable: false,
                dirty: false,
                path: String::new(),
            });
            /* mark as dir via empty path + want_dir */
            pm_metal_fs_set_active_ops(&MTAR_RW_OPS, ctx);
            return done(fi as u32);
        }
        let e = &arch.toc[ti];
        if e.is_dir && write && !want_dir {
            return done(PM_METAL_FS_INVALID);
        }
        let mut cache = Vec::new();
        if !e.is_dir && e.uncomp_len > 0 {
            cache.resize(e.uncomp_len as usize, 0);
            let src = arch.blob.add(e.payload_off as usize);
            let mut out_len = 0usize;
            let rc = pm_metal_util_lz4_decompress_safe(
                src,
                e.comp_len as usize,
                cache.as_mut_ptr(),
                cache.len(),
                &mut out_len,
            );
            if rc != 0 {
                if (e.comp_len as usize) <= cache.len() {
                    for i in 0..(e.comp_len as usize) {
                        cache[i] = *src.add(i);
                    }
                    cache.truncate(e.comp_len as usize);
                } else {
                    return done(PM_METAL_FS_INVALID);
                }
            } else {
                cache.truncate(out_len);
            }
        }
        if trunc && !e.is_dir {
            cache.clear();
        }
        files[fi] = Some(File {
            used: true,
            arch: arch_id,
            toc_i: ti,
            pos: 0,
            cache,
            writable: write && !e.is_dir,
            dirty: trunc && !e.is_dir,
            path: e.path.clone(),
        });
        pm_metal_fs_set_active_ops(&MTAR_RW_OPS, ctx);
        return done(fi as u32);
    }

    if !creat || want_dir {
        return done(PM_METAL_FS_INVALID);
    }
    /* Create empty file — append toc placeholder; rebuild on close/fsync. */
    files[fi] = Some(File {
        used: true,
        arch: arch_id,
        toc_i: usize::MAX,
        pos: 0,
        cache: Vec::new(),
        writable: true,
        dirty: true,
        path: String::from(path_s),
    });
    pm_metal_fs_set_active_ops(&MTAR_RW_OPS, ctx);
    done(fi as u32)
}

unsafe extern "C" fn op_close(_ctx: *mut c_void, h: u32) -> u32 {
    if (h as usize) < MAX_OPEN {
        (*addr_of_mut!(FILES))[h as usize] = None;
    }
    done(0)
}

unsafe extern "C" fn op_close_rw(ctx: *mut c_void, h: u32) -> u32 {
    let files = &mut *addr_of_mut!(FILES);
    if let Some(f) = files.get_mut(h as usize).and_then(|x| x.as_mut()) {
        if f.dirty && f.writable {
            let _ = commit_file(ctx as usize, f);
        }
    }
    if (h as usize) < MAX_OPEN {
        files[h as usize] = None;
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
    let Some(f) = files.get_mut(h as usize).and_then(|x| x.as_mut()) else {
        return done(0);
    };
    if dest.is_null() {
        return done(0);
    }
    let avail = f.cache.len().saturating_sub(off as usize);
    let n = core::cmp::min(avail, len as usize);
    for i in 0..n {
        *dest.add(i) = f.cache[off as usize + i];
    }
    f.pos = off + n as u32;
    done(n as u32)
}

unsafe extern "C" fn op_lseek(_ctx: *mut c_void, h: u32, off: i32, whence: u32) -> i32 {
    let files = &mut *addr_of_mut!(FILES);
    let Some(f) = files.get_mut(h as usize).and_then(|x| x.as_mut()) else {
        return -1;
    };
    let end = f.cache.len() as i32;
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
    let arch_id = ctx as usize;
    let arches = &*addr_of!(ARCHES);
    let Some(arch) = arches.get(arch_id).and_then(|a| a.as_ref()) else {
        return done(PM_METAL_FS_INVALID);
    };
    let path = cstr(path);
    for e in &arch.toc {
        if e.path == path {
            if !st_out.is_null() {
                let st = st_out as *mut pm_metal_fs_stat_t;
                (*st).size = e.uncomp_len;
                (*st).type_ = if e.is_dir {
                    PM_METAL_FS_TYPE_DIR
                } else {
                    PM_METAL_FS_TYPE_FILE
                };
            }
            return done(0);
        }
    }
    done(PM_METAL_FS_INVALID)
}

unsafe fn fill_statfs(ctx: *mut c_void, out: *mut pm_metal_fs_statfs_t, flags: u32) -> i32 {
    if out.is_null() {
        return -1;
    }
    let arches = &*addr_of!(ARCHES);
    let Some(arch) = arches.get(ctx as usize).and_then(|a| a.as_ref()) else {
        return -1;
    };
    let total = match &arch.owned {
        Some(v) => v.capacity() as u64,
        None => arch.len as u64,
    };
    (*out).total = total;
    (*out).used = arch.len as u64;
    (*out).flags = flags;
    0
}

unsafe extern "C" fn op_statfs_ro(ctx: *mut c_void, out: *mut pm_metal_fs_statfs_t) -> i32 {
    fill_statfs(ctx, out, PM_METAL_FS_ST_RDONLY)
}

unsafe extern "C" fn op_statfs_rw(ctx: *mut c_void, out: *mut pm_metal_fs_statfs_t) -> i32 {
    fill_statfs(ctx, out, 0)
}

unsafe extern "C" fn op_readdir(
    ctx: *mut c_void,
    h: u32,
    name_out: *mut u8,
    name_cap: u32,
) -> u32 {
    let files = &mut *addr_of_mut!(FILES);
    let Some(f) = files.get(h as usize).and_then(|x| x.as_ref()) else {
        return done(0);
    };
    if name_out.is_null() || name_cap == 0 {
        return done(0);
    }
    let arch_id = ctx as usize;
    let arches = &*addr_of!(ARCHES);
    let Some(arch) = arches.get(arch_id).and_then(|a| a.as_ref()) else {
        return done(0);
    };
    let dir = if !f.path.is_empty() || f.toc_i == usize::MAX || arch.toc.is_empty() {
        f.path.as_str()
    } else if f.toc_i < arch.toc.len() {
        arch.toc[f.toc_i].path.as_str()
    } else {
        ""
    };
    let idx = f.pos as usize;
    let mut n = 0usize;
    for e in &arch.toc {
        let parent = parent_of(&e.path);
        if parent != dir && !(dir.is_empty() && parent.is_empty()) {
            continue;
        }
        if e.path == dir {
            continue;
        }
        if n == idx {
            let base = e.path.rsplit('/').next().unwrap_or(&e.path);
            let b = base.as_bytes();
            let copy = core::cmp::min(b.len(), name_cap as usize - 1);
            for i in 0..copy {
                *name_out.add(i) = b[i];
            }
            *name_out.add(copy) = 0;
            if let Some(fm) = files[h as usize].as_mut() {
                fm.pos = idx as u32 + 1;
            }
            return done(1);
        }
        n += 1;
    }
    done(0)
}

unsafe extern "C" fn op_fwrite(ctx: *mut c_void, h: u32, src: *const u8, len: u32) -> u32 {
    let files = &*addr_of!(FILES);
    let pos = files
        .get(h as usize)
        .and_then(|f| f.as_ref())
        .map(|f| f.pos)
        .unwrap_or(0);
    op_fpwrite(ctx, h, pos, src, len)
}

unsafe extern "C" fn op_fpwrite(
    _ctx: *mut c_void,
    h: u32,
    off: u32,
    src: *const u8,
    len: u32,
) -> u32 {
    let files = &mut *addr_of_mut!(FILES);
    let Some(f) = files.get_mut(h as usize).and_then(|x| x.as_mut()) else {
        return done(0);
    };
    if !f.writable || src.is_null() {
        return done(0);
    }
    let end = off as usize + len as usize;
    if f.cache.len() < end {
        f.cache.resize(end, 0);
    }
    for i in 0..len as usize {
        f.cache[off as usize + i] = *src.add(i);
    }
    f.pos = off + len;
    f.dirty = true;
    done(len)
}

unsafe extern "C" fn op_fsync(ctx: *mut c_void, h: u32) -> u32 {
    let files = &mut *addr_of_mut!(FILES);
    let Some(f) = files.get_mut(h as usize).and_then(|x| x.as_mut()) else {
        return done(PM_METAL_FS_INVALID);
    };
    if f.dirty && f.writable {
        if commit_file(ctx as usize, f).is_err() {
            return done(PM_METAL_FS_INVALID);
        }
    }
    done(0)
}

unsafe extern "C" fn op_mkdir(ctx: *mut c_void, path: *const u8) -> u32 {
    let arch_id = ctx as usize;
    let path_s = cstr(path);
    if path_s.is_empty() {
        return done(0);
    }
    match rebuild_with(arch_id, |members| {
        if members.iter().any(|(p, _, _)| p == path_s) {
            return Ok(());
        }
        /* ensure parent dirs */
        let mut cur = String::new();
        for part in path_s.split('/') {
            if part.is_empty() {
                continue;
            }
            if !cur.is_empty() {
                cur.push('/');
            }
            cur.push_str(part);
            if !members.iter().any(|(p, _, _)| p == &cur) {
                members.push((cur.clone(), Vec::new(), true));
            }
        }
        Ok(())
    }) {
        Ok(()) => done(0),
        Err(()) => done(PM_METAL_FS_INVALID),
    }
}

unsafe extern "C" fn op_unlink(ctx: *mut c_void, path: *const u8) -> u32 {
    let arch_id = ctx as usize;
    let path_s = cstr(path);
    match rebuild_with(arch_id, |members| {
        let before = members.len();
        members.retain(|(p, _, is_dir)| {
            if p == path_s {
                return false;
            }
            if *is_dir {
                return true;
            }
            /* also drop children of removed dir */
            let mut pref = String::from(path_s);
            pref.push('/');
            !p.starts_with(&pref)
        });
        if members.len() == before {
            return Err(());
        }
        Ok(())
    }) {
        Ok(()) => done(0),
        Err(()) => done(PM_METAL_FS_INVALID),
    }
}

unsafe fn commit_file(arch_id: usize, f: &mut File) -> Result<(), ()> {
    let path = f.path.clone();
    let data = f.cache.clone();
    rebuild_with(arch_id, |members| {
        if let Some((_, d, is_dir)) = members.iter_mut().find(|(p, _, _)| p == &path) {
            if *is_dir {
                return Err(());
            }
            *d = data;
        } else {
            members.push((path, data, false));
        }
        Ok(())
    })?;
    f.dirty = false;
    /* refresh toc_i after rebuild */
    let arches = &*addr_of!(ARCHES);
    if let Some(arch) = arches.get(arch_id).and_then(|a| a.as_ref()) {
        f.toc_i = arch
            .toc
            .iter()
            .position(|e| e.path == f.path)
            .unwrap_or(usize::MAX);
    }
    Ok(())
}

unsafe fn collect_members(arch: &Arch) -> Result<Vec<(String, Vec<u8>, bool)>, ()> {
    let mut members = Vec::new();
    for e in &arch.toc {
        if e.is_dir {
            members.push((e.path.clone(), Vec::new(), true));
            continue;
        }
        let mut cache = Vec::new();
        if e.uncomp_len > 0 {
            cache.resize(e.uncomp_len as usize, 0);
            let src = arch.blob.add(e.payload_off as usize);
            let mut out_len = 0usize;
            let rc = pm_metal_util_lz4_decompress_safe(
                src,
                e.comp_len as usize,
                cache.as_mut_ptr(),
                cache.len(),
                &mut out_len,
            );
            if rc != 0 {
                if (e.comp_len as usize) <= cache.len() {
                    for i in 0..(e.comp_len as usize) {
                        cache[i] = *src.add(i);
                    }
                    cache.truncate(e.comp_len as usize);
                } else {
                    return Err(());
                }
            } else {
                cache.truncate(out_len);
            }
        }
        members.push((e.path.clone(), cache, false));
    }
    Ok(members)
}

unsafe fn rebuild_with(
    arch_id: usize,
    edit: impl FnOnce(&mut Vec<(String, Vec<u8>, bool)>) -> Result<(), ()>,
) -> Result<(), ()> {
    let arches = &mut *addr_of_mut!(ARCHES);
    let Some(arch) = arches.get_mut(arch_id).and_then(|a| a.as_mut()) else {
        return Err(());
    };
    if !arch.rw {
        return Err(());
    }
    let mut members = collect_members(arch)?;
    edit(&mut members)?;
    let packed = pack_members(&members)?;
    arch.owned = Some(packed);
    arch.len = arch.owned.as_ref().unwrap().len();
    arch.blob = arch.owned.as_ref().unwrap().as_ptr();
    /* re-parse toc from owned */
    let blob = arch.blob;
    let len = arch.len;
    let mut toc = Vec::new();
    let mut ctx = ScanCtx {
        toc: &mut toc,
        blob,
        len,
    };
    let rc = pm_metal_util_tar_foreach_ex(
        blob,
        len,
        Some(scan_cb),
        &mut ctx as *mut ScanCtx as *mut u8,
    );
    if rc < 0 || toc.is_empty() {
        toc.clear();
        let mut ctx2 = ScanCtx {
            toc: &mut toc,
            blob,
            len,
        };
        let _ = pm_metal_util_tar_foreach_ex(
            blob,
            len,
            Some(scan_all_cb),
            &mut ctx2 as *mut ScanCtx as *mut u8,
        );
    }
    arch.toc = toc;
    Ok(())
}

fn pack_members(members: &[(String, Vec<u8>, bool)]) -> Result<Vec<u8>, ()> {
    let mut names: Vec<*const u8> = Vec::new();
    let mut datas: Vec<*const u8> = Vec::new();
    let mut lens: Vec<u32> = Vec::new();
    let mut name_bufs: Vec<Vec<u8>> = Vec::new();
    let mut file_datas: Vec<&[u8]> = Vec::new();

    for (name, data, is_dir) in members {
        if *is_dir {
            continue; /* dirs recorded in TOC via file parents; pack files only */
        }
        let mut nb = Vec::with_capacity(name.len() + 1);
        nb.extend_from_slice(name.as_bytes());
        nb.push(0);
        name_bufs.push(nb);
        file_datas.push(data.as_slice());
        lens.push(data.len() as u32);
    }
    for nb in &name_bufs {
        names.push(nb.as_ptr());
    }
    for d in &file_datas {
        datas.push(d.as_ptr());
    }

    /* Also ensure dir-only paths appear: write empty placeholder files? Prefer
     * TOC rows for dirs — extend pack to emit dir toc entries. */
    let mut cap = 64 * 1024usize;
    for (_, data, _) in members {
        cap = cap.saturating_add(data.len()).saturating_add(1024);
    }
    for _ in 0..8 {
        let mut out = alloc::vec![0u8; cap];
        let mut out_len = 0usize;
        let rc = unsafe {
            pack_simple_with_dirs(
                names.as_ptr(),
                datas.as_ptr(),
                lens.as_ptr(),
                names.len() as u32,
                members,
                out.as_mut_ptr(),
                out.len(),
                &mut out_len,
            )
        };
        if rc == 0 {
            out.truncate(out_len);
            return Ok(out);
        }
        cap = cap.saturating_mul(2);
    }
    Err(())
}

unsafe fn pack_simple_with_dirs(
    names: *const *const u8,
    datas: *const *const u8,
    lens: *const u32,
    count: u32,
    all: &[(String, Vec<u8>, bool)],
    out: *mut u8,
    out_cap: usize,
    out_len: *mut usize,
) -> i32 {
    /* Build TOC including dirs, then file members like pack_simple. */
    let mut members: Vec<(String, Vec<u8>, u32, bool)> = Vec::new();
    for (name, _d, is_dir) in all {
        if *is_dir {
            members.push((name.clone(), Vec::new(), 0, true));
        }
    }
    for i in 0..count as usize {
        let name = cstr(*names.add(i));
        let src = *datas.add(i);
        let slen = *lens.add(i) as usize;
        if src.is_null() {
            return -1;
        }
        let bound = pm_metal_util_lz4_compress_bound(slen);
        let mut comp = alloc::vec![0u8; bound];
        let n = if slen == 0 {
            0
        } else {
            pm_metal_util_lz4_compress(src, slen, comp.as_mut_ptr(), comp.len())
        };
        if n < 0 {
            return -1;
        }
        comp.truncate(n as usize);
        members.push((String::from(name), comp, slen as u32, false));
    }

    let mut toc_body: Vec<u8> = Vec::new();
    for (name, comp, uncomp, is_dir) in &members {
        let nlen = name.len();
        toc_body.extend_from_slice(&(nlen as u16).to_le_bytes());
        toc_body.extend_from_slice(name.as_bytes());
        toc_body.extend_from_slice(&0u64.to_le_bytes());
        toc_body.extend_from_slice(&(comp.len() as u32).to_le_bytes());
        toc_body.extend_from_slice(&uncomp.to_le_bytes());
        toc_body.push(if *is_dir { 1 } else { 0 });
    }
    let toc_pad = pm_metal_util_tar_pad_len(toc_body.len() as u64);
    let mut file_off = 512 + toc_body.len() + toc_pad;
    let mut ti = 0usize;
    for (name, comp, _uncomp, is_dir) in &members {
        let nlen = name.len();
        let payload = if *is_dir {
            0u64
        } else {
            (file_off + 512) as u64
        };
        let pos = ti + 2 + nlen;
        toc_body[pos..pos + 8].copy_from_slice(&payload.to_le_bytes());
        ti += 2 + nlen + 8 + 4 + 4 + 1;
        if !*is_dir {
            file_off += 512 + comp.len() + pm_metal_util_tar_pad_len(comp.len() as u64);
        }
    }

    let mut w = 0usize;
    let mut hdr = [0u8; 512];
    let hl = pm_metal_util_tar_write_header(
        hdr.as_mut_ptr(),
        512,
        TOC_NAME.as_ptr(),
        toc_body.len() as u64,
        b'0',
    );
    if hl < 0 || w + 512 > out_cap {
        return -1;
    }
    for i in 0..512 {
        *out.add(w + i) = hdr[i];
    }
    w += 512;
    if w + toc_body.len() > out_cap {
        return -1;
    }
    for (i, b) in toc_body.iter().enumerate() {
        *out.add(w + i) = *b;
    }
    w += toc_body.len();
    for _ in 0..toc_pad {
        if w >= out_cap {
            return -1;
        }
        *out.add(w) = 0;
        w += 1;
    }

    for (name, comp, _, is_dir) in &members {
        if *is_dir {
            continue;
        }
        let mut nb = [0u8; NAME_MAX + 1];
        let nb_len = core::cmp::min(name.len(), NAME_MAX - 1);
        nb[..nb_len].copy_from_slice(&name.as_bytes()[..nb_len]);
        let hl = pm_metal_util_tar_write_header(
            hdr.as_mut_ptr(),
            512,
            nb.as_ptr(),
            comp.len() as u64,
            b'0',
        );
        if hl < 0 || w + 512 > out_cap {
            return -1;
        }
        for i in 0..512 {
            *out.add(w + i) = hdr[i];
        }
        w += 512;
        if w + comp.len() > out_cap {
            return -1;
        }
        for (i, b) in comp.iter().enumerate() {
            *out.add(w + i) = *b;
        }
        w += comp.len();
        let pad = pm_metal_util_tar_pad_len(comp.len() as u64);
        for _ in 0..pad {
            if w >= out_cap {
                return -1;
            }
            *out.add(w) = 0;
            w += 1;
        }
    }
    let el = pm_metal_util_tar_write_end(out.add(w), out_cap - w);
    if el < 0 {
        return -1;
    }
    w += el as usize;
    *out_len = w;
    0
}

fn parent_of(path: &str) -> &str {
    match path.rfind('/') {
        Some(i) => &path[..i],
        None => "",
    }
}

fn cstr<'a>(p: *const u8) -> &'a str {
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

/* ---- pack builder --------------------------------------------------- */

/// Empty `.mtar` (TOC member with zero rows + tar end). Returns 0 ok.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_mtar_empty(
    out: *mut u8,
    out_cap: usize,
    out_len: *mut usize,
) -> i32 {
    pm_metal_fs_mtar_pack_simple(
        core::ptr::null(),
        core::ptr::null(),
        core::ptr::null(),
        0,
        out,
        out_cap,
        out_len,
    )
}

pub type pm_metal_fs_mtar_write_fn = Option<
    unsafe extern "C" fn(ctx: *mut c_void, data: *const u8, len: usize) -> i32,
>;

/// Pack one file (already compressed payload optional). Host builds TOC then members.
/// Simpler API: pack from memory file list via [`pm_metal_fs_mtar_pack_begin`] family — see
/// [`pm_metal_fs_mtar_pack_simple`] for a single-shot buffer packer used by forge-cli.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_mtar_pack_simple(
    names: *const *const u8,
    datas: *const *const u8,
    lens: *const u32,
    count: u32,
    out: *mut u8,
    out_cap: usize,
    out_len: *mut usize,
) -> i32 {
    if out.is_null() || out_len.is_null() {
        return -1;
    }
    if count > 0 && (names.is_null() || datas.is_null() || lens.is_null()) {
        return -1;
    }
    let mut members: Vec<(String, Vec<u8>, u32)> = Vec::new();
    for i in 0..count as usize {
        let name = cstr(*names.add(i));
        let src = *datas.add(i);
        let slen = *lens.add(i) as usize;
        if src.is_null() {
            return -1;
        }
        let bound = pm_metal_util_lz4_compress_bound(slen);
        let mut comp = alloc::vec![0u8; bound];
        let n = pm_metal_util_lz4_compress(src, slen, comp.as_mut_ptr(), comp.len());
        if n < 0 {
            return -1;
        }
        comp.truncate(n as usize);
        members.push((String::from(name), comp, slen as u32));
    }

    /* Two-pass: build toc body, patch payload offsets, then assemble. */
    let mut toc_body: Vec<u8> = Vec::new();
    /* Placeholder: after TOC member, files follow. Estimate TOC body size first. */
    for (name, comp, uncomp) in &members {
        let nlen = name.len();
        toc_body.extend_from_slice(&(nlen as u16).to_le_bytes());
        toc_body.extend_from_slice(name.as_bytes());
        toc_body.extend_from_slice(&0u64.to_le_bytes()); /* patch later */
        toc_body.extend_from_slice(&(comp.len() as u32).to_le_bytes());
        toc_body.extend_from_slice(&uncomp.to_le_bytes());
        toc_body.push(0);
    }
    let toc_pad = pm_metal_util_tar_pad_len(toc_body.len() as u64);
    let mut file_off = 512 + toc_body.len() + toc_pad;
    /* Patch offsets in toc_body */
    let mut ti = 0usize;
    for (name, comp, _uncomp) in &members {
        let nlen = name.len();
        let payload = file_off + 512; /* after file header */
        let pos = ti + 2 + nlen;
        let bytes = (payload as u64).to_le_bytes();
        toc_body[pos..pos + 8].copy_from_slice(&bytes);
        ti += 2 + nlen + 8 + 4 + 4 + 1;
        file_off += 512 + comp.len() + pm_metal_util_tar_pad_len(comp.len() as u64);
    }

    let mut w = 0usize;
    let mut hdr = [0u8; 512];
    let hl = pm_metal_util_tar_write_header(
        hdr.as_mut_ptr(),
        512,
        TOC_NAME.as_ptr(),
        toc_body.len() as u64,
        b'0',
    );
    if hl < 0 || w + 512 > out_cap {
        return -1;
    }
    for i in 0..512 {
        *out.add(w + i) = hdr[i];
    }
    w += 512;
    if w + toc_body.len() > out_cap {
        return -1;
    }
    for (i, b) in toc_body.iter().enumerate() {
        *out.add(w + i) = *b;
    }
    w += toc_body.len();
    for _ in 0..toc_pad {
        if w >= out_cap {
            return -1;
        }
        *out.add(w) = 0;
        w += 1;
    }

    for (name, comp, _) in &members {
        let mut nb = [0u8; NAME_MAX + 1];
        let nb_len = core::cmp::min(name.len(), NAME_MAX - 1);
        nb[..nb_len].copy_from_slice(&name.as_bytes()[..nb_len]);
        let hl = pm_metal_util_tar_write_header(
            hdr.as_mut_ptr(),
            512,
            nb.as_ptr(),
            comp.len() as u64,
            b'0',
        );
        if hl < 0 || w + 512 > out_cap {
            return -1;
        }
        for i in 0..512 {
            *out.add(w + i) = hdr[i];
        }
        w += 512;
        if w + comp.len() > out_cap {
            return -1;
        }
        for (i, b) in comp.iter().enumerate() {
            *out.add(w + i) = *b;
        }
        w += comp.len();
        let pad = pm_metal_util_tar_pad_len(comp.len() as u64);
        for _ in 0..pad {
            if w >= out_cap {
                return -1;
            }
            *out.add(w) = 0;
            w += 1;
        }
    }
    let el = pm_metal_util_tar_write_end(out.add(w), out_cap - w);
    if el < 0 {
        return -1;
    }
    w += el as usize;
    *out_len = w;
    0
}
