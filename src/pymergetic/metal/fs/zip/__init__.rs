//! RO ZIP (store / compression=0) fstype + pack builder.
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
    pm_metal_fs_statfs_t, PM_METAL_FS_INVALID, PM_METAL_FS_O_DIRECTORY, PM_METAL_FS_O_RDONLY,
    PM_METAL_FS_SEEK_CUR, PM_METAL_FS_SEEK_END, PM_METAL_FS_SEEK_SET, PM_METAL_FS_ST_RDONLY,
    PM_METAL_FS_TYPE_DIR, PM_METAL_FS_TYPE_FILE,
};
use pymergetic_metal_rt as _;
use pymergetic_metal_fs_vfs as vfs;

extern "C" {
    fn pm_metal_async_completed_u32(v: u32) -> u32;
}

const SIG_LOCAL: u32 = 0x0403_4b50;
const SIG_CENTRAL: u32 = 0x0201_4b50;
const SIG_EOCD: u32 = 0x0605_4b50;

const MAX_FILES: usize = 256;
const MAX_OPEN: usize = 32;
const MAX_ARCH: usize = 16;
const PATH_MAX: usize = 260;

#[derive(Clone)]
struct TocEntry {
    path: String,
    payload_off: u64,
    size: u32,
    is_dir: bool,
}

struct Arch {
    blob: *const u8,
    len: usize,
    toc: Vec<TocEntry>,
}

struct File {
    arch: usize,
    toc_i: usize,
    pos: u32,
}

static mut ARCHES: [Option<Arch>; MAX_ARCH] = [const { None }; MAX_ARCH];
static mut FILES: [Option<File>; MAX_OPEN] = [const { None }; MAX_OPEN];
static NEXT_ARCH: AtomicU32 = AtomicU32::new(1);

fn done(v: u32) -> u32 {
    unsafe { pm_metal_async_completed_u32(v) }
}

fn read_u16(blob: *const u8, off: usize) -> u16 {
    unsafe {
        u16::from_le_bytes([*blob.add(off), *blob.add(off + 1)])
    }
}

fn read_u32(blob: *const u8, off: usize) -> u32 {
    unsafe {
        u32::from_le_bytes([
            *blob.add(off),
            *blob.add(off + 1),
            *blob.add(off + 2),
            *blob.add(off + 3),
        ])
    }
}

fn write_u16(out: *mut u8, off: usize, v: u16) {
    unsafe {
        let b = v.to_le_bytes();
        *out.add(off) = b[0];
        *out.add(off + 1) = b[1];
    }
}

fn write_u32(out: *mut u8, off: usize, v: u32) {
    unsafe {
        let b = v.to_le_bytes();
        *out.add(off) = b[0];
        *out.add(off + 1) = b[1];
        *out.add(off + 2) = b[2];
        *out.add(off + 3) = b[3];
    }
}

fn crc32(data: &[u8]) -> u32 {
    let mut crc = 0xffff_ffffu32;
    for &b in data {
        crc ^= b as u32;
        for _ in 0..8 {
            if crc & 1 != 0 {
                crc = (crc >> 1) ^ 0xedb8_8320;
            } else {
                crc >>= 1;
            }
        }
    }
    !crc
}

fn find_eocd(blob: *const u8, len: usize) -> Option<usize> {
    if len < 22 {
        return None;
    }
    let min = len.saturating_sub(65557);
    let mut i = len - 22;
    loop {
        if read_u32(blob, i) == SIG_EOCD {
            return Some(i);
        }
        if i == min {
            break;
        }
        i -= 1;
    }
    None
}

fn ensure_dir(toc: &mut Vec<TocEntry>, path: &str) {
    if path.is_empty() {
        return;
    }
    if toc.iter().any(|e| e.path == path && e.is_dir) {
        return;
    }
    if toc.len() >= MAX_FILES {
        return;
    }
    toc.push(TocEntry {
        path: String::from(path),
        payload_off: 0,
        size: 0,
        is_dir: true,
    });
}

fn add_parent_dirs(toc: &mut Vec<TocEntry>, file_path: &str) {
    let parts: Vec<&str> = file_path.split('/').filter(|p| !p.is_empty()).collect();
    if parts.len() <= 1 {
        return;
    }
    let mut acc = String::new();
    for part in &parts[..parts.len() - 1] {
        if !acc.is_empty() {
            acc.push('/');
        }
        acc.push_str(part);
        ensure_dir(toc, &acc);
    }
}

unsafe fn parse_zip(blob: *const u8, len: usize) -> Option<Vec<TocEntry>> {
    let eocd = find_eocd(blob, len)?;
    let cd_entries = read_u16(blob, eocd + 10) as usize;
    let cd_size = read_u32(blob, eocd + 12) as usize;
    let cd_off = read_u32(blob, eocd + 16) as usize;
    if cd_off + cd_size > len {
        return None;
    }

    let mut toc = Vec::new();
    let mut pos = cd_off;
    for _ in 0..cd_entries {
        if pos + 46 > len || read_u32(blob, pos) != SIG_CENTRAL {
            return None;
        }
        let method = read_u16(blob, pos + 10);
        let comp_size = read_u32(blob, pos + 20);
        let uncomp_size = read_u32(blob, pos + 24);
        let name_len = read_u16(blob, pos + 28) as usize;
        let extra_len = read_u16(blob, pos + 30) as usize;
        let comment_len = read_u16(blob, pos + 32) as usize;
        let local_off = read_u32(blob, pos + 42) as usize;
        let entry_end = pos + 46 + name_len + extra_len + comment_len;
        if entry_end > len || name_len == 0 || name_len > PATH_MAX {
            return None;
        }

        let name_bytes =
            core::slice::from_raw_parts(blob.add(pos + 46), name_len);
        let name = core::str::from_utf8_unchecked(name_bytes);
        let mut path = String::from(name);
        let is_dir = path.ends_with('/');
        if is_dir {
            path.pop();
        }

        if method != 0 {
            pos = entry_end;
            continue;
        }
        if comp_size != uncomp_size {
            pos = entry_end;
            continue;
        }

        let mut payload_off = 0u64;
        let size = uncomp_size;
        if !is_dir {
            if local_off + 30 > len || read_u32(blob, local_off) != SIG_LOCAL {
                pos = entry_end;
                continue;
            }
            let l_name_len = read_u16(blob, local_off + 26) as usize;
            let l_extra_len = read_u16(blob, local_off + 28) as usize;
            let data_off = local_off + 30 + l_name_len + l_extra_len;
            if data_off + size as usize > len {
                pos = entry_end;
                continue;
            }
            payload_off = data_off as u64;
        }

        if !is_dir {
            add_parent_dirs(&mut toc, &path);
        }
        if toc.len() < MAX_FILES {
            if is_dir {
                ensure_dir(&mut toc, &path);
            } else {
                toc.push(TocEntry {
                    path,
                    payload_off,
                    size,
                    is_dir: false,
                });
            }
        }
        pos = entry_end;
    }
    Some(toc)
}

/// Open a ZIP blob in place. Returns arch ctx id (nonzero) or 0.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_zip_open_blob(blob: *const u8, len: usize) -> u32 {
    if blob.is_null() || len < 22 {
        return 0;
    }
    let Some(toc) = parse_zip(blob, len) else {
        return 0;
    };
    /* Empty zip (EOCD only) is valid — toc may be empty. */
    let arches = &mut *addr_of_mut!(ARCHES);
    let mut id = 1usize;
    while id < MAX_ARCH {
        if arches[id].is_none() {
            break;
        }
        id += 1;
    }
    if id >= MAX_ARCH {
        return 0;
    }
    let _ = NEXT_ARCH.fetch_add(1, Ordering::Relaxed);
    arches[id] = Some(Arch { blob, len, toc });
    id as u32
}

/// Mount blob at `target` (registers zip ops). Returns 0 ok.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_zip_mount(
    target: *const u8,
    blob: *const u8,
    len: usize,
) -> i32 {
    let id = pm_metal_fs_zip_open_blob(blob, len);
    if id == 0 {
        return -1;
    }
    ensure_ops_registered();
    let ctx = id as usize as *mut c_void;
    if vfs::pm_metal_fs_vfs_mount(target, &ZIP_OPS as *const _ as *const c_void, ctx) == 0 {
        return -1;
    }
    0
}

static mut OPS_READY: bool = false;
static ZIP_NAME: &[u8] = b"zip\0";

static ZIP_OPS: pm_metal_fs_ops_t = pm_metal_fs_ops_t {
    name: ZIP_NAME.as_ptr(),
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
    statfs: Some(op_statfs),
};

unsafe fn ensure_ops_registered() {
    if !*addr_of!(OPS_READY) {
        let _ = pm_metal_fs_ops_register(&ZIP_OPS);
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
        if e.path == path {
            found = Some(i);
            break;
        }
    }
    /* Virtual root for empty path (readdir /). */
    if found.is_none() && path.is_empty() {
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
        /* toc_i = usize::MAX marks virtual root */
        files[fi] = Some(File {
            arch: arch_id,
            toc_i: usize::MAX,
            pos: 0,
        });
        pm_metal_fs_set_active_ops(&ZIP_OPS, ctx);
        return done(fi as u32);
    }
    let Some(ti) = found else {
        return done(PM_METAL_FS_INVALID);
    };
    let e = &arch.toc[ti];
    if e.is_dir && (flags & PM_METAL_FS_O_DIRECTORY) == 0 && flags != PM_METAL_FS_O_RDONLY {
        /* allow dir open for readdir */
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
    files[fi] = Some(File {
        arch: arch_id,
        toc_i: ti,
        pos: 0,
    });
    pm_metal_fs_set_active_ops(&ZIP_OPS, ctx);
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
    let arches = &*addr_of!(ARCHES);
    let Some(f) = files.get_mut(h as usize).and_then(|x| x.as_mut()) else {
        return done(0);
    };
    if dest.is_null() {
        return done(0);
    }
    let Some(arch) = arches.get(f.arch).and_then(|a| a.as_ref()) else {
        return done(0);
    };
    if f.toc_i == usize::MAX {
        return done(0);
    }
    let e = &arch.toc[f.toc_i];
    if e.is_dir {
        return done(0);
    }
    let start = off as usize;
    if start >= e.size as usize {
        return done(0);
    }
    let avail = (e.size as usize) - start;
    let n = core::cmp::min(avail, len as usize);
    let src = arch.blob.add(e.payload_off as usize + start);
    for i in 0..n {
        *dest.add(i) = *src.add(i);
    }
    f.pos = off + n as u32;
    done(n as u32)
}

unsafe extern "C" fn op_lseek(_ctx: *mut c_void, h: u32, off: i32, whence: u32) -> i32 {
    let files = &mut *addr_of_mut!(FILES);
    let arches = &*addr_of!(ARCHES);
    let Some(f) = files.get_mut(h as usize).and_then(|x| x.as_mut()) else {
        return -1;
    };
    let Some(arch) = arches.get(f.arch).and_then(|a| a.as_ref()) else {
        return -1;
    };
    if f.toc_i == usize::MAX {
        f.pos = if whence == PM_METAL_FS_SEEK_SET { off.max(0) as u32 } else { 0 };
        return f.pos as i32;
    }
    let e = &arch.toc[f.toc_i];
    let end = if e.is_dir { 0 } else { e.size as i32 };
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
                (*st).size = e.size;
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

unsafe extern "C" fn op_statfs(ctx: *mut c_void, out: *mut pm_metal_fs_statfs_t) -> i32 {
    if out.is_null() {
        return -1;
    }
    let arches = &*addr_of!(ARCHES);
    let Some(arch) = arches.get(ctx as usize).and_then(|a| a.as_ref()) else {
        return -1;
    };
    (*out).total = arch.len as u64;
    (*out).used = arch.len as u64;
    (*out).flags = PM_METAL_FS_ST_RDONLY;
    0
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
    let dir = if f.toc_i == usize::MAX {
        ""
    } else {
        arch.toc[f.toc_i].path.as_str()
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

struct PackEntry {
    name: String,
    data_off: usize,
    size: u32,
    crc: u32,
    local_off: u32,
}

unsafe fn write_local_header(
    out: *mut u8,
    out_cap: usize,
    w: usize,
    name: &[u8],
    size: u32,
    crc: u32,
) -> i32 {
    let hdr_len = 30 + name.len();
    if w + hdr_len > out_cap {
        return -1;
    }
    write_u32(out, w, SIG_LOCAL);
    write_u16(out, w + 4, 20);
    write_u16(out, w + 6, 0);
    write_u16(out, w + 8, 0);
    write_u16(out, w + 10, 0);
    write_u16(out, w + 12, 0);
    write_u32(out, w + 14, crc);
    write_u32(out, w + 18, size);
    write_u32(out, w + 22, size);
    write_u16(out, w + 26, name.len() as u16);
    write_u16(out, w + 28, 0);
    for (i, b) in name.iter().enumerate() {
        *out.add(w + 30 + i) = *b;
    }
    0
}

unsafe fn write_central_header(
    out: *mut u8,
    out_cap: usize,
    w: usize,
    name: &[u8],
    size: u32,
    crc: u32,
    local_off: u32,
) -> i32 {
    let hdr_len = 46 + name.len();
    if w + hdr_len > out_cap {
        return -1;
    }
    write_u32(out, w, SIG_CENTRAL);
    write_u16(out, w + 4, 20);
    write_u16(out, w + 6, 20);
    write_u16(out, w + 8, 0);
    write_u16(out, w + 10, 0);
    write_u16(out, w + 12, 0);
    write_u16(out, w + 14, 0);
    write_u32(out, w + 16, crc);
    write_u32(out, w + 20, size);
    write_u32(out, w + 24, size);
    write_u16(out, w + 28, name.len() as u16);
    write_u16(out, w + 30, 0);
    write_u16(out, w + 32, 0);
    write_u16(out, w + 34, 0);
    write_u16(out, w + 36, 0);
    write_u32(out, w + 38, 0);
    write_u32(out, w + 42, local_off);
    for (i, b) in name.iter().enumerate() {
        *out.add(w + 46 + i) = *b;
    }
    0
}

/// Pack files into a store-method ZIP (single buffer).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_zip_pack_simple(
    names: *const *const u8,
    datas: *const *const u8,
    lens: *const u32,
    count: u32,
    out: *mut u8,
    out_cap: usize,
    out_len: *mut usize,
) -> i32 {
    if names.is_null()
        || datas.is_null()
        || lens.is_null()
        || out.is_null()
        || out_len.is_null()
    {
        return -1;
    }
    if count as usize > MAX_FILES {
        return -1;
    }

    let mut entries: Vec<PackEntry> = Vec::new();
    let mut w = 0usize;

    for i in 0..count as usize {
        let name = cstr(*names.add(i));
        if name.is_empty() || name.len() > PATH_MAX {
            return -1;
        }
        let src = *datas.add(i);
        let slen = *lens.add(i) as usize;
        if src.is_null() {
            return -1;
        }
        let data = core::slice::from_raw_parts(src, slen);
        let crc = crc32(data);
        let nb = name.as_bytes();
        let local_off = w as u32;
        if write_local_header(out, out_cap, w, nb, slen as u32, crc) < 0 {
            return -1;
        }
        w += 30 + nb.len();
        if w + slen > out_cap {
            return -1;
        }
        for (j, b) in data.iter().enumerate() {
            *out.add(w + j) = *b;
        }
        entries.push(PackEntry {
            name: String::from(name),
            data_off: w,
            size: slen as u32,
            crc,
            local_off,
        });
        w += slen;
    }

    let cd_off = w;
    for e in &entries {
        let nb = e.name.as_bytes();
        if write_central_header(out, out_cap, w, nb, e.size, e.crc, e.local_off) < 0 {
            return -1;
        }
        w += 46 + nb.len();
    }
    let cd_size = w - cd_off;

    if w + 22 > out_cap {
        return -1;
    }
    write_u32(out, w, SIG_EOCD);
    write_u16(out, w + 4, 0);
    write_u16(out, w + 6, 0);
    write_u16(out, w + 8, entries.len() as u16);
    write_u16(out, w + 10, entries.len() as u16);
    write_u32(out, w + 12, cd_size as u32);
    write_u32(out, w + 16, cd_off as u32);
    write_u16(out, w + 20, 0);
    w += 22;

    *out_len = w;
    0
}

/// Write an empty ZIP (EOCD only, zero entries).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_zip_empty(
    out: *mut u8,
    out_cap: usize,
    out_len: *mut usize,
) -> i32 {
    if out.is_null() || out_len.is_null() || out_cap < 22 {
        return -1;
    }
    write_u32(out, 0, SIG_EOCD);
    write_u16(out, 4, 0);
    write_u16(out, 6, 0);
    write_u16(out, 8, 0);
    write_u16(out, 10, 0);
    write_u32(out, 12, 0);
    write_u32(out, 16, 0);
    write_u16(out, 20, 0);
    *out_len = 22;
    0
}
