//! In-memory RW directory tree (`tmpfs`).
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
    PM_METAL_FS_O_RDONLY, PM_METAL_FS_O_RDWR, PM_METAL_FS_O_TRUNC, PM_METAL_FS_O_WRONLY,
    PM_METAL_FS_SEEK_CUR, PM_METAL_FS_SEEK_END, PM_METAL_FS_SEEK_SET, PM_METAL_FS_TYPE_DIR,
    PM_METAL_FS_TYPE_FILE,
};
use pymergetic_metal_rt as _;
use pymergetic_metal_fs_vfs as vfs;

extern "C" {
    fn pm_metal_async_completed_u32(v: u32) -> u32;
}

const MAX_FS: usize = 8;
const MAX_OPEN: usize = 64;
const MAX_NODES: usize = 512;

#[derive(Clone)]
struct Node {
    path: String,
    is_dir: bool,
    data: Vec<u8>,
}

struct Fs {
    nodes: Vec<Node>,
}

struct OpenH {
    fs: usize,
    node: usize,
    is_dir: bool,
    pos: u32,
    dir_idx: u32,
}

static mut FSS: [Option<Fs>; MAX_FS] = [const { None }; MAX_FS];
static mut FILES: [Option<OpenH>; MAX_OPEN] = [const { None }; MAX_OPEN];
static mut OPS_READY: bool = false;
static TMPFS_NAME: &[u8] = b"tmpfs\0";

static TMPFS_OPS: pm_metal_fs_ops_t = pm_metal_fs_ops_t {
    name: TMPFS_NAME.as_ptr(),
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
    statfs: Some(op_statfs),
};

fn done(v: u32) -> u32 {
    unsafe { pm_metal_async_completed_u32(v) }
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

fn norm_path(path: &str) -> String {
    let mut s = String::from(path.trim().trim_start_matches('/'));
    while s.ends_with('/') {
        s.pop();
    }
    s
}

fn parent_of(path: &str) -> &str {
    match path.rfind('/') {
        Some(i) => &path[..i],
        None => "",
    }
}

unsafe fn ensure_ops() {
    if !*addr_of!(OPS_READY) {
        let _ = pm_metal_fs_ops_register(&TMPFS_OPS);
        *addr_of_mut!(OPS_READY) = true;
    }
}

fn find_node(fs: &Fs, path: &str) -> Option<usize> {
    fs.nodes.iter().position(|n| n.path == path)
}

fn ensure_dir(fs: &mut Fs, path: &str) -> Option<usize> {
    if path.is_empty() {
        return Some(usize::MAX); /* virtual root */
    }
    if let Some(i) = find_node(fs, path) {
        return if fs.nodes[i].is_dir { Some(i) } else { None };
    }
    let parent = parent_of(path);
    if !parent.is_empty() {
        ensure_dir(fs, parent)?;
    }
    if fs.nodes.len() >= MAX_NODES {
        return None;
    }
    fs.nodes.push(Node {
        path: String::from(path),
        is_dir: true,
        data: Vec::new(),
    });
    Some(fs.nodes.len() - 1)
}

/// Mount empty tmpfs at `target`. Returns 0 ok.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_fs_tmpfs_mount(target: *const u8) -> i32 {
    if target.is_null() {
        return -1;
    }
    ensure_ops();
    let fss = &mut *addr_of_mut!(FSS);
    let mut id = 1usize;
    while id < MAX_FS {
        if fss[id].is_none() {
            break;
        }
        id += 1;
    }
    if id >= MAX_FS {
        return -1;
    }
    fss[id] = Some(Fs { nodes: Vec::new() });
    let ctx = id as *mut c_void;
    if vfs::pm_metal_fs_vfs_mount(target, &TMPFS_OPS as *const _ as *const c_void, ctx) == 0 {
        fss[id] = None;
        return -1;
    }
    0
}

unsafe extern "C" fn op_open(ctx: *mut c_void, path: *const u8, flags: u32) -> u32 {
    let fs_id = ctx as usize;
    let fss = &mut *addr_of_mut!(FSS);
    let files = &mut *addr_of_mut!(FILES);
    let Some(fs) = fss.get_mut(fs_id).and_then(|f| f.as_mut()) else {
        return done(PM_METAL_FS_INVALID);
    };
    let norm = norm_path(cstr(path));
    let want_dir = (flags & PM_METAL_FS_O_DIRECTORY) != 0 || norm.is_empty();
    let creat = (flags & PM_METAL_FS_O_CREAT) != 0;
    let trunc = (flags & PM_METAL_FS_O_TRUNC) != 0;
    let write =
        (flags & PM_METAL_FS_O_WRONLY) != 0 || (flags & 3) == PM_METAL_FS_O_RDWR || creat || trunc;

    let node_i = if norm.is_empty() {
        if !want_dir && (flags & PM_METAL_FS_O_RDONLY) == 0 {
            return done(PM_METAL_FS_INVALID);
        }
        usize::MAX
    } else if let Some(i) = find_node(fs, &norm) {
        if fs.nodes[i].is_dir != want_dir && want_dir {
            return done(PM_METAL_FS_INVALID);
        }
        if fs.nodes[i].is_dir && !want_dir && write {
            return done(PM_METAL_FS_INVALID);
        }
        if !fs.nodes[i].is_dir && trunc {
            fs.nodes[i].data.clear();
        }
        i
    } else if want_dir {
        return done(PM_METAL_FS_INVALID);
    } else if creat {
        let parent = parent_of(&norm);
        if !parent.is_empty() {
            if ensure_dir(fs, parent).is_none() {
                return done(PM_METAL_FS_INVALID);
            }
        }
        if fs.nodes.len() >= MAX_NODES {
            return done(PM_METAL_FS_INVALID);
        }
        fs.nodes.push(Node {
            path: norm.clone(),
            is_dir: false,
            data: Vec::new(),
        });
        fs.nodes.len() - 1
    } else {
        return done(PM_METAL_FS_INVALID);
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
        fs: fs_id,
        node: node_i,
        is_dir: want_dir || node_i == usize::MAX,
        pos: 0,
        dir_idx: 0,
    });
    pm_metal_fs_set_active_ops(&TMPFS_OPS, ctx);
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
    let fss = &*addr_of!(FSS);
    let Some(f) = files.get_mut(h as usize).and_then(|x| x.as_mut()) else {
        return done(0);
    };
    if f.is_dir || dest.is_null() || f.node == usize::MAX {
        return done(0);
    }
    let Some(fs) = fss.get(f.fs).and_then(|x| x.as_ref()) else {
        return done(0);
    };
    let data = &fs.nodes[f.node].data;
    let avail = data.len().saturating_sub(off as usize);
    let n = core::cmp::min(avail, len as usize);
    for i in 0..n {
        *dest.add(i) = data[off as usize + i];
    }
    f.pos = off + n as u32;
    done(n as u32)
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
    let fss = &mut *addr_of_mut!(FSS);
    let Some(f) = files.get_mut(h as usize).and_then(|x| x.as_mut()) else {
        return done(0);
    };
    if f.is_dir || src.is_null() || f.node == usize::MAX {
        return done(0);
    }
    let Some(fs) = fss.get_mut(f.fs).and_then(|x| x.as_mut()) else {
        return done(0);
    };
    let data = &mut fs.nodes[f.node].data;
    let end = off as usize + len as usize;
    if data.len() < end {
        data.resize(end, 0);
    }
    for i in 0..len as usize {
        data[off as usize + i] = *src.add(i);
    }
    f.pos = off + len;
    done(len)
}

unsafe extern "C" fn op_lseek(_ctx: *mut c_void, h: u32, off: i32, whence: u32) -> i32 {
    let files = &mut *addr_of_mut!(FILES);
    let fss = &*addr_of!(FSS);
    let Some(f) = files.get_mut(h as usize).and_then(|x| x.as_mut()) else {
        return -1;
    };
    let end = if f.is_dir || f.node == usize::MAX {
        0i32
    } else {
        fss.get(f.fs)
            .and_then(|x| x.as_ref())
            .map(|fs| fs.nodes[f.node].data.len() as i32)
            .unwrap_or(0)
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
    let fs_id = ctx as usize;
    let fss = &*addr_of!(FSS);
    let Some(fs) = fss.get(fs_id).and_then(|f| f.as_ref()) else {
        return done(PM_METAL_FS_INVALID);
    };
    let norm = norm_path(cstr(path));
    if norm.is_empty() {
        if !st_out.is_null() {
            let st = st_out as *mut pm_metal_fs_stat_t;
            (*st).size = 0;
            (*st).type_ = PM_METAL_FS_TYPE_DIR;
        }
        return done(0);
    }
    let Some(i) = find_node(fs, &norm) else {
        return done(PM_METAL_FS_INVALID);
    };
    if !st_out.is_null() {
        let st = st_out as *mut pm_metal_fs_stat_t;
        (*st).size = fs.nodes[i].data.len() as u32;
        (*st).type_ = if fs.nodes[i].is_dir {
            PM_METAL_FS_TYPE_DIR
        } else {
            PM_METAL_FS_TYPE_FILE
        };
    }
    done(0)
}

unsafe extern "C" fn op_statfs(ctx: *mut c_void, out: *mut pm_metal_fs_statfs_t) -> i32 {
    if out.is_null() {
        return -1;
    }
    let fss = &*addr_of!(FSS);
    let Some(fs) = fss.get(ctx as usize).and_then(|f| f.as_ref()) else {
        return -1;
    };
    let used = fs.nodes.iter().map(|n| n.data.len() as u64).sum::<u64>();
    /* Unbounded heap-backed; report resident data bytes. */
    (*out).total = used;
    (*out).used = used;
    (*out).flags = 0;
    0
}

unsafe extern "C" fn op_readdir(
    _ctx: *mut c_void,
    h: u32,
    name_out: *mut u8,
    name_cap: u32,
) -> u32 {
    let files = &mut *addr_of_mut!(FILES);
    let fss = &*addr_of!(FSS);
    let Some(f) = files.get_mut(h as usize).and_then(|x| x.as_mut()) else {
        return done(0);
    };
    if !f.is_dir || name_out.is_null() || name_cap == 0 {
        return done(0);
    }
    let Some(fs) = fss.get(f.fs).and_then(|x| x.as_ref()) else {
        return done(0);
    };
    let dir = if f.node == usize::MAX {
        ""
    } else {
        fs.nodes[f.node].path.as_str()
    };
    let mut n = 0u32;
    for node in &fs.nodes {
        let parent = parent_of(&node.path);
        if parent != dir {
            continue;
        }
        if n == f.dir_idx {
            let base = node.path.rsplit('/').next().unwrap_or(&node.path);
            let b = base.as_bytes();
            let copy = core::cmp::min(b.len(), name_cap as usize - 1);
            for i in 0..copy {
                *name_out.add(i) = b[i];
            }
            *name_out.add(copy) = 0;
            f.dir_idx = n + 1;
            return done(1);
        }
        n += 1;
    }
    done(0)
}

unsafe extern "C" fn op_mkdir(ctx: *mut c_void, path: *const u8) -> u32 {
    let fs_id = ctx as usize;
    let fss = &mut *addr_of_mut!(FSS);
    let Some(fs) = fss.get_mut(fs_id).and_then(|f| f.as_mut()) else {
        return done(PM_METAL_FS_INVALID);
    };
    let norm = norm_path(cstr(path));
    if norm.is_empty() {
        return done(0);
    }
    match ensure_dir(fs, &norm) {
        Some(_) => done(0),
        None => done(PM_METAL_FS_INVALID),
    }
}

unsafe extern "C" fn op_unlink(ctx: *mut c_void, path: *const u8) -> u32 {
    let fs_id = ctx as usize;
    let fss = &mut *addr_of_mut!(FSS);
    let Some(fs) = fss.get_mut(fs_id).and_then(|f| f.as_mut()) else {
        return done(PM_METAL_FS_INVALID);
    };
    let norm = norm_path(cstr(path));
    let Some(i) = find_node(fs, &norm) else {
        return done(PM_METAL_FS_INVALID);
    };
    if fs.nodes[i].is_dir {
        let prefix = if norm.is_empty() {
            String::new()
        } else {
            let mut p = norm.clone();
            p.push('/');
            p
        };
        if fs.nodes.iter().any(|n| n.path.starts_with(&prefix) && n.path != norm) {
            return done(PM_METAL_FS_INVALID);
        }
    }
    fs.nodes.remove(i);
    done(0)
}
