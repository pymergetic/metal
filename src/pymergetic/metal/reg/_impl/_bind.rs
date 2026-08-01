//! Ptr bind — hot-path resolve of a registered symbol.

use core::ffi::c_void;

use crate::table::{cstr_bytes, Table, FUNC_MAX, MODULE_MAX};

/// Resolve `(full_module, func)` to a function pointer, or null.
pub fn bind(table: &Table, module: *const u8, func: *const u8) -> *const c_void {
    let Some(m) = cstr_bytes(module, MODULE_MAX) else {
        return core::ptr::null();
    };
    let Some(f) = cstr_bytes(func, FUNC_MAX) else {
        return core::ptr::null();
    };
    table.lookup(m, f)
}
