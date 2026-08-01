//! vfs — thin face over Metal `pm_metal_fs_*` (no upy FAT/LFS).

use crate::upy::py::obj::{self, MpObj};
use crate::upy::py::objects::{objint, objlist, objstr};

pub const INVALID: u32 = 0xffff_ffff;
pub const O_RDONLY: u32 = 1;
pub const O_WRONLY: u32 = 2;
pub const O_RDWR: u32 = 3;
pub const O_CREAT: u32 = 4;
pub const O_TRUNC: u32 = 8;
pub const O_DIRECTORY: u32 = 32;

extern "C" {
    fn pm_metal_fs_open_async(path: *const u8, flags: u32) -> u32;
    fn pm_metal_fs_close_async(h: u32) -> u32;
    fn pm_metal_fs_fread_async(h: u32, dest: *mut u8, len: u32) -> u32;
    fn pm_metal_fs_fwrite_async(h: u32, src: *const u8, len: u32) -> u32;
    fn pm_metal_fs_readdir_async(h: u32, name_dest: *mut u8, name_cap: u32) -> u32;
    fn pm_metal_fs_stat_async(path: *const u8, dest: *mut u8) -> u32;
    fn pm_metal_async_result_u32(h: u32) -> u32;
}

fn path_c(path: &[u8], buf: &mut [u8; 256]) -> Option<*const u8> {
    if path.len() >= buf.len() {
        return None;
    }
    buf[..path.len()].copy_from_slice(path);
    buf[path.len()] = 0;
    Some(buf.as_ptr())
}

pub unsafe fn open(path: &[u8], flags: u32) -> u32 {
    let mut buf = [0u8; 256];
    let Some(p) = path_c(path, &mut buf) else {
        return INVALID;
    };
    let h = pm_metal_fs_open_async(p, flags);
    pm_metal_async_result_u32(h)
}

pub unsafe fn close(fd: u32) -> i32 {
    if fd == INVALID {
        return -1;
    }
    let h = pm_metal_fs_close_async(fd);
    let rc = pm_metal_async_result_u32(h);
    if rc == INVALID {
        -1
    } else {
        0
    }
}

pub unsafe fn read(fd: u32, dest: &mut [u8]) -> isize {
    if fd == INVALID || dest.is_empty() {
        return -1;
    }
    let h = pm_metal_fs_fread_async(fd, dest.as_mut_ptr(), dest.len() as u32);
    let n = pm_metal_async_result_u32(h);
    if n == INVALID {
        -1
    } else {
        n as isize
    }
}

pub unsafe fn write(fd: u32, src: &[u8]) -> isize {
    if fd == INVALID {
        return -1;
    }
    let h = pm_metal_fs_fwrite_async(fd, src.as_ptr(), src.len() as u32);
    let n = pm_metal_async_result_u32(h);
    if n == INVALID {
        -1
    } else {
        n as isize
    }
}

#[repr(C)]
struct Stat {
    size: u32,
    type_: u32,
}

pub unsafe fn stat(path: &[u8]) -> Option<(u32, u32)> {
    let mut buf = [0u8; 256];
    let p = path_c(path, &mut buf)?;
    let mut st = Stat { size: 0, type_: 0 };
    let h = pm_metal_fs_stat_async(p, &mut st as *mut _ as *mut u8);
    let rc = pm_metal_async_result_u32(h);
    if rc == INVALID {
        None
    } else {
        Some((st.size, st.type_))
    }
}

/// List directory entries via Metal fs (open DIRECTORY + readdir).
pub unsafe fn listdir(path: &[u8]) -> Option<MpObj> {
    let fd = open(path, O_RDONLY | O_DIRECTORY);
    if fd == INVALID {
        return None;
    }
    let lst = objlist::new(0);
    if lst == obj::OBJ_NULL {
        let _ = close(fd);
        return None;
    }
    let mut name = [0u8; 128];
    loop {
        let h = pm_metal_fs_readdir_async(fd, name.as_mut_ptr(), name.len() as u32);
        let n = pm_metal_async_result_u32(h);
        if n == 0 || n == INVALID {
            break;
        }
        let len = core::cmp::min(n as usize, name.len());
        // trim trailing NULs
        let mut end = len;
        while end > 0 && name[end - 1] == 0 {
            end -= 1;
        }
        if end == 0 {
            break;
        }
        let s = objstr::new(&name[..end]);
        if !objlist::append(lst, s) {
            let _ = close(fd);
            return None;
        }
    }
    let _ = close(fd);
    Some(lst)
}

pub unsafe fn open_obj(path: MpObj, flags: u32) -> Option<MpObj> {
    let bytes = objstr::as_bytes(path)?;
    let fd = open(bytes, flags);
    if fd == INVALID {
        None
    } else {
        Some(objint::from_isize(fd as isize))
    }
}
