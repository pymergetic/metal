//! Bulk registration helpers — register many `(func, ptr)` rows under one
//! full module name onto `reg` in a single call.

use core::ffi::c_void;

use crate::pm_metal_reg_register;

/// Register many `(func, ptr)` rows under one full module C string.
/// `module` and each `func` must be NUL-terminated. Returns 0 or -1.
pub unsafe fn register_rows(module: *const u8, rows: &[(*const u8, *const c_void)]) -> i32 {
    for &(func, ptr) in rows {
        if pm_metal_reg_register(module, func, ptr) != 0 {
            return -1;
        }
    }
    0
}

/// Same as [`register_rows`] with Rust byte slices (must include trailing NUL).
pub unsafe fn register_rows_bytes(module: &[u8], rows: &[(&[u8], *const c_void)]) -> i32 {
    for &(func, ptr) in rows {
        if pm_metal_reg_register(module.as_ptr(), func.as_ptr(), ptr) != 0 {
            return -1;
        }
    }
    0
}
