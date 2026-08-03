//! W14.2 — HTTP fetch-on-miss + trust gate before WAMR load.
//! Freestanding only (final link has http/trust); host smoke omits this module.

use core::ffi::{c_char, c_void};

use crate::{pm_metal_wasm_call0, pm_metal_wasm_load_verified, pm_metal_wasm_unload};

extern "C" {
    fn pm_metal_mem_alloc(size: usize) -> *mut u8;
    fn pm_metal_mem_free(p: *mut u8);
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
    fn pm_metal_trust_mods_pubkey_set(pk: *const u8, pk_len: u32) -> i32;
}

const ASYNC_DONE: u32 = 2;
const ASYNC_ERROR: u32 = 4;
const ASYNC_CANCELLED: u32 = 3;
/* Tiny packs only (hello ~100B); larger guests need a real async seed coro. */
const FETCH_CAP: usize = 4096;

/// GET `url` into a heap buffer, `trust_accept` (sig optional), then
/// `load_register`. Cooperative poll (yields via `run_poll_all`). Returns
/// published export count, or `-1`.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_wasm_fetch_register(
    full_module: *const u8,
    url: *const c_char,
    sig: *const u8,
    sig_len: u32,
) -> i32 {
    if full_module.is_null() || url.is_null() {
        return -1;
    }
    let buf = pm_metal_mem_alloc(FETCH_CAP);
    if buf.is_null() {
        return -1;
    }
    let h = pm_metal_net_http_get(url, buf as *mut c_void, FETCH_CAP as u32);
    if h == 0 {
        pm_metal_mem_free(buf);
        return -1;
    }
    if pm_metal_async_create_task(h) == 0 {
        pm_metal_async_coro_close(h);
        pm_metal_mem_free(buf);
        return -1;
    }
    let deadline = pm_metal_time_mono_us() + 15_000_000;
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
    let st = pm_metal_net_http_status(h);
    let blen = pm_metal_net_http_body_len(h);
    pm_metal_async_coro_close(h);
    if rc != 0 || st != 200 || blen == 0 {
        pm_metal_mem_free(buf);
        return -1;
    }
    let n = pm_metal_wasm_load_verified(full_module, buf, blen, sig, sig_len);
    pm_metal_mem_free(buf);
    n
}

/// Unload hello if present, verify+load with baked sig, unload, HTTP fetch
/// from loopback `/pkg/`, call `ready`. Logs `wasm fetch ok`.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_wasm_proof_fetch() -> i32 {
    static WASM: &[u8] = include_bytes!("../../../../build/packs/tests.wasm_hello.wasm");
    static MOD: [u8; 17] = *b"tests.wasm_hello\0";
    /* Same Mods test key as trust_proof (seed 01..20). */
    static PK: [u8; 32] = [
        0xd4, 0xf8, 0xe6, 0xf2, 0x67, 0x27, 0x11, 0x77, 0xc1, 0x1d, 0x17, 0xd3, 0x98, 0x10, 0xd7,
        0x47, 0x16, 0x65, 0x72, 0xa1, 0xb6, 0xdb, 0x8e, 0x35, 0x23, 0x63, 0xd9, 0x78, 0x6e, 0xb0,
        0x79, 0x83,
    ];
    static SIG: [u8; 64] = [
        0x55, 0x74, 0x18, 0xa7, 0x0e, 0xa1, 0xb7, 0x76, 0xb0, 0x49, 0xc4, 0x89, 0xd7, 0x68, 0xd0,
        0x05, 0xe7, 0x6b, 0x68, 0x7a, 0xfe, 0x61, 0x84, 0x25, 0x82, 0xf4, 0x99, 0xe7, 0xe6, 0x48,
        0x02, 0xdf, 0x96, 0x39, 0x18, 0x36, 0xeb, 0xd4, 0x66, 0x6c, 0x3d, 0x91, 0x00, 0x5f, 0x9d,
        0xda, 0x57, 0x04, 0xd8, 0x25, 0x6a, 0x71, 0x75, 0x49, 0x4a, 0x47, 0x37, 0xd9, 0x66, 0x47,
        0xfd, 0xfc, 0x6c, 0x0a,
    ];
    static URL: &[u8] = b"http://127.0.0.1/pkg/tests.wasm_hello.wasm\0";

    let _ = pm_metal_wasm_unload(MOD.as_ptr());
    if pm_metal_trust_mods_pubkey_set(PK.as_ptr(), PK.len() as u32) != 0 {
        return -1;
    }
    if pm_metal_wasm_load_verified(
        MOD.as_ptr(),
        WASM.as_ptr(),
        WASM.len() as u32,
        SIG.as_ptr(),
        SIG.len() as u32,
    ) < 1
    {
        pm_metal_log(b"wasm fetch: verified\0".as_ptr());
        return -1;
    }
    if pm_metal_wasm_call0(MOD.as_ptr(), b"ready\0".as_ptr()) != 0 {
        return -1;
    }
    let _ = pm_metal_wasm_unload(MOD.as_ptr());
    /* Soft policy: unsigned HTTP body accepted; still exercises fetch->load. */
    if pm_metal_wasm_fetch_register(MOD.as_ptr(), URL.as_ptr() as *const c_char, core::ptr::null(), 0)
        < 1
    {
        pm_metal_log(b"wasm fetch: http\0".as_ptr());
        return -1;
    }
    if pm_metal_wasm_call0(MOD.as_ptr(), b"ready\0".as_ptr()) != 0 {
        return -1;
    }
    pm_metal_log(b"wasm fetch ok\0".as_ptr());
    0
}
