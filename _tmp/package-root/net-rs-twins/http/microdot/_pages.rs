//! Doc-browser pages for Microdot (W13.3): home, /symbols, /src.
//!
//! Text/plain for now — HTML polish is optional later. Reads live `reg`
//! reflection and `/src/<kernel-id>` VFS (no iface tar).

use core::ffi::c_char;
use core::ptr::{self, addr_of_mut};

extern "C" {
    fn pm_metal_net_http_send_simple(
        code: u32,
        reason: *const c_char,
        ctype: *const c_char,
        body: *const c_char,
    ) -> i32;
    fn pm_metal_net_http_conn_target() -> *const u8;
    fn pm_metal_reg_mod_count() -> u32;
    fn pm_metal_reg_mod_at(index: u32, name_out: *mut u8, name_cap: u32) -> i32;
    fn pm_metal_reg_mod_entry_count(full_module: *const u8) -> u32;
    fn pm_metal_reg_mod_entry_at(
        full_module: *const u8,
        index: u32,
        name_out: *mut u8,
        name_cap: u32,
        out_ptr: *mut *const core::ffi::c_void,
    ) -> i32;
    fn pm_metal_fs_open_async(path: *const u8, flags: u32) -> u32;
    fn pm_metal_fs_readdir_async(h: u32, name_dest: *mut u8, name_cap: u32) -> u32;
    fn pm_metal_fs_fread_async(h: u32, dest: *mut u8, len: u32) -> u32;
    fn pm_metal_fs_close_async(h: u32) -> u32;
    fn pm_metal_fs_result(h: u32) -> u32;
}

const O_RDONLY: u32 = 1;
const O_DIRECTORY: u32 = 32;
const FS_INVALID: u32 = 0xffff_ffff;
const BODY_CAP: usize = 8192;
const SRC_ROOT: &[u8] = b"/src/pymergetic.metal";

static mut BODY: [u8; BODY_CAP] = [0; BODY_CAP];

unsafe fn body_clear() -> (&'static mut [u8], usize) {
    let b = &mut *addr_of_mut!(BODY);
    b.fill(0);
    (b, 0)
}

unsafe fn body_push(buf: &mut [u8], len: &mut usize, bytes: &[u8]) -> bool {
    if *len + bytes.len() >= buf.len() {
        return false;
    }
    buf[*len..*len + bytes.len()].copy_from_slice(bytes);
    *len += bytes.len();
    true
}

unsafe fn send_text(code: u32, reason: &[u8], body: &[u8]) -> i32 {
    pm_metal_net_http_send_simple(
        code,
        reason.as_ptr() as *const c_char,
        b"text/plain\0".as_ptr() as *const c_char,
        body.as_ptr() as *const c_char,
    )
}

pub unsafe extern "C" fn home_handler(_conn_id: u32) -> i32 {
    send_text(
        200,
        b"OK\0",
        b"metal doc-browser\n/\n/symbols\n/src\n/src?path=\n/download/kernel\n/download/wasm?name=\n/pkg/tests.wasm_hello.wasm\n\0",
    )
}

pub unsafe extern "C" fn symbols_handler(_conn_id: u32) -> i32 {
    let (buf, mut len) = body_clear();
    let _ = body_push(buf, &mut len, b"reg symbols\n");
    let nmod = pm_metal_reg_mod_count();
    let mut i = 0u32;
    while i < nmod {
        let mut mod_name = [0u8; 128];
        if pm_metal_reg_mod_at(i, mod_name.as_mut_ptr(), 128) != 0 {
            break;
        }
        let ml = mod_name.iter().position(|&b| b == 0).unwrap_or(0);
        let _ = body_push(buf, &mut len, b"\n");
        let _ = body_push(buf, &mut len, &mod_name[..ml]);
        let _ = body_push(buf, &mut len, b"\n");
        let nent = pm_metal_reg_mod_entry_count(mod_name.as_ptr());
        let mut j = 0u32;
        while j < nent {
            let mut en = [0u8; 64];
            let mut ptr: *const core::ffi::c_void = ptr::null();
            if pm_metal_reg_mod_entry_at(mod_name.as_ptr(), j, en.as_mut_ptr(), 64, &mut ptr) != 0 {
                break;
            }
            let el = en.iter().position(|&b| b == 0).unwrap_or(0);
            let _ = body_push(buf, &mut len, b"  ");
            let _ = body_push(buf, &mut len, &en[..el]);
            if ptr.is_null() {
                let _ = body_push(buf, &mut len, b" (null)\n");
            } else {
                let _ = body_push(buf, &mut len, b"\n");
            }
            j += 1;
        }
        i += 1;
        if len + 64 >= buf.len() {
            break;
        }
    }
    let _ = body_push(buf, &mut len, b"\0");
    send_text(200, b"OK\0", &buf[..len])
}

unsafe fn query_path(target: *const u8) -> Option<&'static [u8]> {
    if target.is_null() {
        return None;
    }
    let mut i = 0usize;
    while *target.add(i) != 0 {
        if *target.add(i) == b'?' {
            let q = target.add(i + 1);
            /* path= */
            if *q == b'p'
                && *q.add(1) == b'a'
                && *q.add(2) == b't'
                && *q.add(3) == b'h'
                && *q.add(4) == b'='
            {
                let start = q.add(5);
                let mut n = 0usize;
                while *start.add(n) != 0 && *start.add(n) != b'&' {
                    n += 1;
                    if n > 200 {
                        break;
                    }
                }
                return Some(core::slice::from_raw_parts(start, n));
            }
            return None;
        }
        i += 1;
        if i > 256 {
            break;
        }
    }
    None
}

pub unsafe extern "C" fn src_handler(_conn_id: u32) -> i32 {
    let target = pm_metal_net_http_conn_target();
    if let Some(rel) = query_path(target) {
        return src_cat(rel);
    }
    src_list()
}

unsafe fn src_list() -> i32 {
    let (buf, mut len) = body_clear();
    let _ = body_push(buf, &mut len, b"src /\n");
    let mut path = [0u8; 160];
    path[..SRC_ROOT.len()].copy_from_slice(SRC_ROOT);
    path[SRC_ROOT.len()] = b'/';
    path[SRC_ROOT.len() + 1] = 0;
    let oh = pm_metal_fs_open_async(path.as_ptr(), O_RDONLY | O_DIRECTORY);
    let fd = pm_metal_fs_result(oh);
    if fd == FS_INVALID {
        let _ = body_push(buf, &mut len, b"(unmounted)\n\0");
        return send_text(503, b"Unavailable\0", &buf[..len]);
    }
    let mut name = [0u8; 96];
    loop {
        name.fill(0);
        let rh = pm_metal_fs_readdir_async(fd, name.as_mut_ptr(), name.len() as u32);
        let n = pm_metal_fs_result(rh);
        if n == 0 || n == FS_INVALID {
            break;
        }
        let nl = name.iter().position(|&b| b == 0).unwrap_or(0);
        if nl == 0 {
            continue;
        }
        let _ = body_push(buf, &mut len, &name[..nl]);
        let _ = body_push(buf, &mut len, b"\n");
        if len + 128 >= buf.len() {
            break;
        }
    }
    let _ = pm_metal_fs_result(pm_metal_fs_close_async(fd));
    let _ = body_push(buf, &mut len, b"\0");
    send_text(200, b"OK\0", &buf[..len])
}

unsafe fn src_cat(rel: &[u8]) -> i32 {
    /* Reject `..` and absolute escapes. */
    if rel.is_empty() || rel[0] == b'/' || rel.windows(2).any(|w| w == b"..") {
        return send_text(400, b"Bad Request\0", b"bad path\n\0");
    }
    let mut path = [0u8; 256];
    if SRC_ROOT.len() + 1 + rel.len() + 1 >= path.len() {
        return send_text(414, b"URI Too Long\0", b"path too long\n\0");
    }
    path[..SRC_ROOT.len()].copy_from_slice(SRC_ROOT);
    path[SRC_ROOT.len()] = b'/';
    path[SRC_ROOT.len() + 1..SRC_ROOT.len() + 1 + rel.len()].copy_from_slice(rel);
    path[SRC_ROOT.len() + 1 + rel.len()] = 0;

    let oh = pm_metal_fs_open_async(path.as_ptr(), O_RDONLY);
    let fd = pm_metal_fs_result(oh);
    if fd == FS_INVALID {
        return send_text(404, b"Not Found\0", b"not found\n\0");
    }
    /* Cap response so the listen handler does not stall the runner while
     * blasting a multi-KB body over loopback (doc browser can page later). */
    const CAT_MAX: usize = 1024;
    let (buf, mut len) = body_clear();
    let max = CAT_MAX.min(buf.len() - 1);
    loop {
        if len >= max {
            break;
        }
        let rh = pm_metal_fs_fread_async(fd, buf.as_mut_ptr().add(len), (max - len) as u32);
        let n = pm_metal_fs_result(rh) as usize;
        if n == 0 || n == FS_INVALID as usize {
            break;
        }
        len += n;
    }
    let _ = pm_metal_fs_result(pm_metal_fs_close_async(fd));
    buf[len] = 0;
    len += 1;
    send_text(200, b"OK\0", &buf[..len])
}
