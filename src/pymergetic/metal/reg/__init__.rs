//! Cross-lang registry bus — full module names + func → pointer.
//!
//! Keys always look like `pymergetic.metal.fs.open` (Locked #2). Bind returns
//! the raw fn pointer for the hot path; lookup copies it out for C callers.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(dead_code, non_camel_case_types)]

#[path = "_bind.rs"]
mod bind;
#[path = "_publish.rs"]
mod publish;
#[path = "_table.rs"]
mod table;

use core::ffi::c_void;

use pymergetic_metal_rt as _;

use bind::bind as bind_key;
use table::{cstr_bytes, Table, FUNC_MAX, MODULE_MAX};

pub use publish::{register_rows, register_rows_bytes};

static TABLE: Table = Table::new();

/// Register or replace `(full_module, func)` → `ptr`. Returns 0 or -1.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_register(
    full_module: *const u8,
    func: *const u8,
    ptr: *const c_void,
) -> i32 {
    let Some(m) = cstr_bytes(full_module, MODULE_MAX) else {
        return -1;
    };
    let Some(f) = cstr_bytes(func, FUNC_MAX) else {
        return -1;
    };
    TABLE.register(m, f, ptr)
}

/// Lookup: write pointer to `*out_ptr` on success. Returns 0 or -1.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_lookup(
    full_module: *const u8,
    func: *const u8,
    out_ptr: *mut *const c_void,
) -> i32 {
    if out_ptr.is_null() {
        return -1;
    }
    let p = bind_key(&TABLE, full_module, func);
    if p.is_null() {
        *out_ptr = core::ptr::null();
        return -1;
    }
    *out_ptr = p;
    0
}

/// Hot path: bound function pointer, or null if missing.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_bind(
    full_module: *const u8,
    func: *const u8,
) -> *const c_void {
    bind_key(&TABLE, full_module, func)
}

/// Convenience: call a registered `extern "C" fn() -> i32`. Returns the
/// callee result, or -1 if missing / null args.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_call0(full_module: *const u8, func: *const u8) -> i32 {
    let p = bind_key(&TABLE, full_module, func);
    if p.is_null() {
        return -1;
    }
    let f: extern "C" fn() -> i32 = core::mem::transmute(p);
    f()
}

/// How many symbols are currently registered.
#[no_mangle]
pub extern "C" fn pm_metal_reg_count() -> u32 {
    TABLE.count() as u32
}

/// Publish kernel module borders (py / console / log). Returns 0 or -1.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_reg_publish_kernel() -> i32 {
    publish::publish_kernel()
}
