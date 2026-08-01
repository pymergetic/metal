//! Metal wasm host — load packs via WAMR into Metal memory; publish on `reg`.
//!
//! Host + freestanding: real WAMR (`external/wamr`) over one Metal pool.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(dead_code, non_camel_case_types)]

#[path = "_load.rs"]
mod load;

use pymergetic_metal_async as _;
use pymergetic_metal_log as _;
use pymergetic_metal_mem as _;
use pymergetic_metal_reg as _;
use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_wasm_port_ready() -> i32;
    fn pm_metal_wasm_port_init() -> i32;
    fn pm_metal_wasm_port_shutdown();
    fn pm_metal_wasm_port_load(
        full_module: *const u8,
        bytes: *const u8,
        len: u32,
    ) -> i32;
    fn pm_metal_wasm_port_unload(full_module: *const u8);
    fn pm_metal_wasm_port_publish_reg(full_module: *const u8) -> i32;
    fn pm_metal_wasm_port_call0(full_module: *const u8, func: *const u8) -> i32;
}

/// 1 if the wasm runtime is initialized.
#[no_mangle]
pub extern "C" fn pm_metal_wasm_ready() -> i32 {
    unsafe { pm_metal_wasm_port_ready() }
}

/// Init WAMR over a Metal pool (one memory). 0 ok, -1 fail.
#[no_mangle]
pub extern "C" fn pm_metal_wasm_init() -> i32 {
    unsafe { pm_metal_wasm_port_init() }
}

#[no_mangle]
pub extern "C" fn pm_metal_wasm_shutdown() {
    unsafe { pm_metal_wasm_port_shutdown() }
}

/// Load + instantiate wasm bytes under `full_module` (NUL C string).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_wasm_load(
    full_module: *const u8,
    bytes: *const u8,
    len: u32,
) -> i32 {
    load::load(full_module, bytes, len)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_wasm_unload(full_module: *const u8) {
    if !full_module.is_null() {
        pm_metal_wasm_port_unload(full_module);
    }
}

/// Publish `() -> i32` exports onto `reg`. Returns count or -1.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_wasm_publish_reg(full_module: *const u8) -> i32 {
    if full_module.is_null() {
        return -1;
    }
    pm_metal_wasm_port_publish_reg(full_module)
}

/// Call export without going through reg.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_wasm_call0(
    full_module: *const u8,
    func: *const u8,
) -> i32 {
    if full_module.is_null() || func.is_null() {
        return -1;
    }
    pm_metal_wasm_port_call0(full_module, func)
}

/// Load, publish to reg, return publish count (or -1).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_wasm_load_publish(
    full_module: *const u8,
    bytes: *const u8,
    len: u32,
) -> i32 {
    if load::load(full_module, bytes, len) != 0 {
        return -1;
    }
    pm_metal_wasm_port_publish_reg(full_module)
}

/// W6.3 proof: load forge-packed `tests.wasm_hello` + `_c`, call via host + `reg`.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_wasm_proof() -> i32 {
    extern "C" {
        fn pm_metal_reg_call0(full_module: *const u8, func: *const u8) -> i32;
    }

    /* Produced by `forge pack` (forge build runs pack all in prep). */
    static WASM_RS: &[u8] = include_bytes!("../../../../build/packs/tests.wasm_hello.wasm");
    static WASM_C: &[u8] = include_bytes!("../../../../build/packs/tests.wasm_hello_c.wasm");
    static MOD_RS: [u8; 17] = *b"tests.wasm_hello\0";
    static MOD_C: [u8; 19] = *b"tests.wasm_hello_c\0";

    if pm_metal_wasm_init() != 0 {
        return -1;
    }
    for (mod_name, bytes) in [
        (MOD_RS.as_slice(), WASM_RS),
        (MOD_C.as_slice(), WASM_C),
    ] {
        let n = pm_metal_wasm_load_publish(mod_name.as_ptr(), bytes.as_ptr(), bytes.len() as u32);
        if n < 1 {
            return -1;
        }
        if pm_metal_wasm_call0(mod_name.as_ptr(), b"ready\0".as_ptr()) != 0 {
            return -1;
        }
        if pm_metal_reg_call0(mod_name.as_ptr(), b"ready\0".as_ptr()) != 0 {
            return -1;
        }
    }
    0
}
