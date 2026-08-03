//! Metal Microdot-shaped ASGI app runner — routes + leaf `handle`.
//!
//! Owns no sockets: mounts via `net.http.server` `register_c` / `mount`.
//! Handlers run on the active connection (`conn_method` / `send_simple`).
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::{c_char, c_void};
use core::ptr::{self, addr_of_mut};

use pymergetic_metal_rt as _;

#[path = "_pages.rs"]
mod pages;
#[path = "_download.rs"]
mod download;

const ROUTE_MAX: usize = 32;
const PATH_MAX: usize = 128;

pub type pm_metal_net_http_microdot_handler_t =
    Option<unsafe extern "C" fn(conn_id: u32) -> i32>;

#[derive(Copy, Clone)]
struct Route {
    used: bool,
    method: [u8; 8],
    path: [u8; PATH_MAX],
    handler: pm_metal_net_http_microdot_handler_t,
}

static mut ROUTES: [Route; ROUTE_MAX] = [Route {
    used: false,
    method: [0; 8],
    path: [0; PATH_MAX],
    handler: None,
}; ROUTE_MAX];
static mut APP_H: u32 = 0;
static mut ROOT_SET: bool = false;

extern "C" {
    fn pm_metal_net_http_register_c(
        func: Option<unsafe extern "C" fn(ctx: *mut c_void, conn_id: u32) -> i32>,
        ctx: *mut c_void,
    ) -> u32;
    fn pm_metal_net_http_conn_method() -> *const u8;
    fn pm_metal_net_http_conn_target() -> *const u8;
    fn pm_metal_net_http_send_simple(
        code: u32,
        reason: *const c_char,
        ctype: *const c_char,
        body: *const c_char,
    ) -> i32;
    fn pm_metal_reg_register(module: *const u8, name: *const u8, ptr: *const c_void) -> i32;
}

unsafe fn cstr_eq(a: *const u8, b: &[u8]) -> bool {
    if a.is_null() {
        return false;
    }
    let mut i = 0usize;
    while i < b.len() {
        let ca = *a.add(i);
        let cb = b[i];
        if ca == 0 || ca != cb {
            return false;
        }
        i += 1;
    }
    *a.add(i) == 0
}

unsafe fn copy_cstr(dst: &mut [u8], src: *const u8) -> bool {
    if src.is_null() || dst.is_empty() {
        return false;
    }
    let mut i = 0usize;
    while i + 1 < dst.len() {
        let c = *src.add(i);
        dst[i] = c;
        if c == 0 {
            return true;
        }
        i += 1;
    }
    dst[dst.len() - 1] = 0;
    false
}

unsafe fn path_match(route: &[u8], target: *const u8) -> bool {
    if target.is_null() {
        return false;
    }
    let rlen = route.iter().position(|&b| b == 0).unwrap_or(route.len());
    if rlen == 0 {
        return false;
    }
    let mut i = 0usize;
    while i < rlen {
        let t = *target.add(i);
        if t == 0 || t != route[i] {
            return false;
        }
        i += 1;
    }
    let next = *target.add(rlen);
    next == 0 || next == b'?'
}

unsafe extern "C" fn leaf_handle(_ctx: *mut c_void, conn_id: u32) -> i32 {
    let method = pm_metal_net_http_conn_method();
    let target = pm_metal_net_http_conn_target();
    if method.is_null() || target.is_null() {
        let _ = pm_metal_net_http_send_simple(
            500,
            b"Error\0".as_ptr() as *const c_char,
            b"text/plain\0".as_ptr() as *const c_char,
            b"no conn\n\0".as_ptr() as *const c_char,
        );
        return -1;
    }
    let routes = &*addr_of_mut!(ROUTES);
    for i in 0..ROUTE_MAX {
        let r = &routes[i];
        if !r.used {
            continue;
        }
        let mlen = r.method.iter().position(|&b| b == 0).unwrap_or(0);
        if mlen == 0 || !cstr_eq(method, &r.method[..mlen]) {
            continue;
        }
        if !path_match(&r.path, target) {
            continue;
        }
        let Some(h) = r.handler else {
            continue;
        };
        return h(conn_id);
    }
    let _ = pm_metal_net_http_send_simple(
        404,
        b"Not Found\0".as_ptr() as *const c_char,
        b"text/plain\0".as_ptr() as *const c_char,
        b"no microdot route\n\0".as_ptr() as *const c_char,
    );
    0
}

/// Ensure the Microdot C leaf is registered; returns app handle (0 fail).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_microdot_register() -> u32 {
    if APP_H != 0 {
        return APP_H;
    }
    APP_H = pm_metal_net_http_register_c(Some(leaf_handle), ptr::null_mut());
    if APP_H != 0 && !ROOT_SET {
        let _ = pm_metal_net_http_microdot_get(b"/\0".as_ptr(), Some(pages::home_handler));
        let _ = pm_metal_net_http_microdot_get(b"/symbols\0".as_ptr(), Some(pages::symbols_handler));
        let _ = pm_metal_net_http_microdot_get(b"/src\0".as_ptr(), Some(pages::src_handler));
        let _ = pm_metal_net_http_microdot_get(
            b"/download/kernel\0".as_ptr(),
            Some(download::kernel_handler),
        );
        let _ = pm_metal_net_http_microdot_get(
            b"/download/wasm\0".as_ptr(),
            Some(download::wasm_handler),
        );
        let _ = pm_metal_net_http_microdot_get(
            b"/pkg/tests.wasm_hello.wasm\0".as_ptr(),
            Some(download::pkg_hello_handler),
        );
        ROOT_SET = true;
    }
    APP_H
}

/// Add GET `path` -> `handler`. Returns 0 ok, -1 fail.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_microdot_get(
    path: *const u8,
    handler: pm_metal_net_http_microdot_handler_t,
) -> i32 {
    pm_metal_net_http_microdot_route(b"GET\0".as_ptr(), path, handler)
}

/// Add `method` + `path` -> `handler`. Returns 0 ok, -1 fail.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_microdot_route(
    method: *const u8,
    path: *const u8,
    handler: pm_metal_net_http_microdot_handler_t,
) -> i32 {
    if method.is_null() || path.is_null() || handler.is_none() {
        return -1;
    }
    if *path != b'/' {
        return -1;
    }
    let routes = &mut *addr_of_mut!(ROUTES);
    for i in 0..ROUTE_MAX {
        let r = &mut routes[i];
        if r.used {
            continue;
        }
        r.method = [0; 8];
        r.path = [0; PATH_MAX];
        if !copy_cstr(&mut r.method, method) || !copy_cstr(&mut r.path, path) {
            return -1;
        }
        r.handler = handler;
        r.used = true;
        return 0;
    }
    -1
}

/// Leaf for C mounts (same as register_c body). Prefer [`pm_metal_net_http_microdot_register`].
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_microdot_handle(conn_id: u32) -> i32 {
    leaf_handle(ptr::null_mut(), conn_id)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_microdot_bind_reg() -> i32 {
    let mod_name = b"pymergetic.metal.net.http.microdot\0";
    let rows: [(&[u8], *const c_void); 4] = [
        (b"register\0", pm_metal_net_http_microdot_register as *const c_void),
        (b"get\0", pm_metal_net_http_microdot_get as *const c_void),
        (b"route\0", pm_metal_net_http_microdot_route as *const c_void),
        (b"handle\0", pm_metal_net_http_microdot_handle as *const c_void),
    ];
    for (name, ptr) in rows {
        if pm_metal_reg_register(mod_name.as_ptr(), name.as_ptr(), ptr) != 0 {
            return -1;
        }
    }
    0
}

/// Loopback GET /symbols + /src after httpd autoload. Logs `doc browse ok`.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_net_http_microdot_proof_browse() -> i32 {
    extern "C" {
        fn pm_metal_net_http_get(url: *const c_char, body: *mut c_void, cap: u32) -> u32;
        fn pm_metal_net_http_status(h: u32) -> u32;
        fn pm_metal_net_http_body_len(h: u32) -> u32;
        fn pm_metal_async_status(h: u32) -> u32;
        fn pm_metal_async_create_task(h: u32) -> u32;
        fn pm_metal_async_run_poll_all() -> i32;
        fn pm_metal_async_coro_close(h: u32);
        fn pm_metal_net_ip_poll();
        fn pm_metal_time_mono_us() -> u64;
        fn pm_metal_log(line: *const u8);
    }
    const ASYNC_DONE: u32 = 2;
    const ASYNC_ERROR: u32 = 4;
    const ASYNC_CANCELLED: u32 = 3;

    unsafe fn once(url: &[u8], needle: &[u8]) -> i32 {
        let mut body = [0u8; 2048];
        let h = pm_metal_net_http_get(
            url.as_ptr() as *const c_char,
            body.as_mut_ptr() as *mut c_void,
            body.len() as u32,
        );
        if h == 0 {
            return -1;
        }
        if pm_metal_async_create_task(h) == 0 {
            pm_metal_async_coro_close(h);
            return -1;
        }
        let deadline = pm_metal_time_mono_us() + 10_000_000;
        let mut rc = -1i32;
        loop {
            let _ = pm_metal_async_run_poll_all();
            pm_metal_net_ip_poll();
            match pm_metal_async_status(h) {
                ASYNC_DONE => {
                    rc = 0;
                    break;
                }
                ASYNC_ERROR | ASYNC_CANCELLED => break,
                _ => {
                    if pm_metal_time_mono_us() >= deadline {
                        break;
                    }
                }
            }
        }
        let st = pm_metal_net_http_status(0);
        let blen = pm_metal_net_http_body_len(0) as usize;
        pm_metal_async_coro_close(h);
        if rc != 0 || st != 200 || blen < needle.len() {
            return -1;
        }
        let hay = &body[..blen.min(body.len())];
        let mut i = 0usize;
        while i + needle.len() <= hay.len() {
            if &hay[i..i + needle.len()] == needle {
                return 0;
            }
            i += 1;
        }
        -1
    }

    if once(b"http://127.0.0.1/symbols\0", b"reg symbols") != 0 {
        pm_metal_log(b"doc browse: symbols\0".as_ptr());
        return -1;
    }
    if once(b"http://127.0.0.1/src\0", b"src /") != 0 {
        pm_metal_log(b"doc browse: src\0".as_ptr());
        return -1;
    }
    if once(
        b"http://127.0.0.1/src?path=reg/_impl/__init__.rs\0",
        b"Cross-lang registry",
    ) != 0
    {
        pm_metal_log(b"doc browse: cat\0".as_ptr());
        return -1;
    }
    /* Loaded image head: METL (.bootinfo bios), MZ (PE efi), or ELF if mapped. */
    if once(b"http://127.0.0.1/download/kernel\0", b"METL") != 0
        && once(b"http://127.0.0.1/download/kernel\0", b"MZ") != 0
        && once(b"http://127.0.0.1/download/kernel\0", b"\x7fELF") != 0
    {
        pm_metal_log(b"doc browse: kernel\0".as_ptr());
        return -1;
    }
    /* Loaded by wasm_proof before this proof runs. Magic is \\0asm. */
    if once(b"http://127.0.0.1/download/wasm?name=tests.wasm_hello\0", b"asm") != 0 {
        pm_metal_log(b"doc browse: wasm\0".as_ptr());
        return -1;
    }
    pm_metal_log(b"doc browse ok\0".as_ptr());
    0
}
