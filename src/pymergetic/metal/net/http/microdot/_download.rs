//! Self-serve downloads (W13.4) + static pkg bytes for fetch-on-miss (W14.2).

use core::ffi::c_char;
use core::ptr;

extern "C" {
    fn pm_metal_net_http_send_bytes(
        code: u32,
        reason: *const c_char,
        ctype: *const c_char,
        body: *const u8,
        body_len: u32,
    ) -> i32;
    fn pm_metal_net_http_send_simple(
        code: u32,
        reason: *const c_char,
        ctype: *const c_char,
        body: *const c_char,
    ) -> i32;
    fn pm_metal_net_http_conn_target() -> *const u8;
    fn pm_metal_wasm_image(
        full_module: *const u8,
        out_bytes: *mut *const u8,
        out_len: *mut u32,
    ) -> i32;
    fn pm_metal_boot_mem_map_ops() -> *const MemMapOps;
}

#[repr(C)]
struct MemMapOps {
    get: Option<unsafe extern "C" fn(*mut u8, u32, *mut u32) -> i32>,
    image_base: Option<unsafe extern "C" fn() -> usize>,
    image_end: Option<unsafe extern "C" fn() -> usize>,
    claim_arena: Option<unsafe extern "C" fn(*mut *mut u8, *mut usize) -> i32>,
}

const OCTET: &[u8] = b"application/octet-stream\0";
/* Listen-handler body cap — full multi-MB kernel stream is a later async job. */
const KERNEL_HEAD: usize = 64;

unsafe fn send_err(code: u32, reason: &[u8], msg: &[u8]) -> i32 {
    pm_metal_net_http_send_simple(
        code,
        reason.as_ptr() as *const c_char,
        b"text/plain\0".as_ptr() as *const c_char,
        msg.as_ptr() as *const c_char,
    )
}

/// GET /download/kernel — first [`KERNEL_HEAD`] bytes of the loaded image.
/// BIOS image base is `.bootinfo` (`METL`), not the on-disk ELF header.
pub unsafe extern "C" fn kernel_handler(_conn_id: u32) -> i32 {
    let ops = pm_metal_boot_mem_map_ops();
    if ops.is_null() {
        return send_err(503, b"Unavailable\0", b"no mem map\n\0");
    }
    let Some(base_fn) = (*ops).image_base else {
        return send_err(503, b"Unavailable\0", b"no image\n\0");
    };
    let Some(end_fn) = (*ops).image_end else {
        return send_err(503, b"Unavailable\0", b"no image\n\0");
    };
    let base = base_fn();
    let end = end_fn();
    if base == 0 || end <= base {
        return send_err(503, b"Unavailable\0", b"empty image\n\0");
    }
    let n = (end - base).min(KERNEL_HEAD);
    pm_metal_net_http_send_bytes(
        200,
        b"OK\0".as_ptr() as *const c_char,
        OCTET.as_ptr() as *const c_char,
        base as *const u8,
        n as u32,
    )
}

unsafe fn query_name(target: *const u8) -> Option<&'static [u8]> {
    if target.is_null() {
        return None;
    }
    let mut i = 0usize;
    while *target.add(i) != 0 {
        if *target.add(i) == b'?' {
            let q = target.add(i + 1);
            if *q == b'n'
                && *q.add(1) == b'a'
                && *q.add(2) == b'm'
                && *q.add(3) == b'e'
                && *q.add(4) == b'='
            {
                let start = q.add(5);
                let mut n = 0usize;
                while *start.add(n) != 0 && *start.add(n) != b'&' {
                    n += 1;
                    if n > 120 {
                        break;
                    }
                }
                if n == 0 {
                    return None;
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

/// GET /download/wasm?name=<full.module.id>
pub unsafe extern "C" fn wasm_handler(_conn_id: u32) -> i32 {
    let target = pm_metal_net_http_conn_target();
    let Some(name) = query_name(target) else {
        return send_err(400, b"Bad Request\0", b"need ?name=\n\0");
    };
    if name.windows(2).any(|w| w == b"..") || name.contains(&b'/') {
        return send_err(400, b"Bad Request\0", b"bad name\n\0");
    }
    let mut namez = [0u8; 128];
    if name.len() + 1 >= namez.len() {
        return send_err(414, b"URI Too Long\0", b"name too long\n\0");
    }
    namez[..name.len()].copy_from_slice(name);
    namez[name.len()] = 0;
    let mut bytes: *const u8 = ptr::null();
    let mut len = 0u32;
    if pm_metal_wasm_image(namez.as_ptr(), &mut bytes, &mut len) != 0 || bytes.is_null() || len == 0
    {
        return send_err(404, b"Not Found\0", b"wasm not loaded\n\0");
    }
    /* Tiny packs only for sync send (hello ~100B); reject oversized. */
    if len > 4096 {
        return send_err(413, b"Payload Too Large\0", b"too large for sync\n\0");
    }
    pm_metal_net_http_send_bytes(
        200,
        b"OK\0".as_ptr() as *const c_char,
        OCTET.as_ptr() as *const c_char,
        bytes,
        len,
    )
}

/// GET /pkg/tests.wasm_hello.wasm — forge-packed bytes always available
/// (does not require the module to be loaded; used by fetch-on-miss proof).
pub unsafe extern "C" fn pkg_hello_handler(_conn_id: u32) -> i32 {
    static WASM: &[u8] = include_bytes!("../../../../../../build/packs/tests.wasm_hello.wasm");
    pm_metal_net_http_send_bytes(
        200,
        b"OK\0".as_ptr() as *const c_char,
        OCTET.as_ptr() as *const c_char,
        WASM.as_ptr(),
        WASM.len() as u32,
    )
}
