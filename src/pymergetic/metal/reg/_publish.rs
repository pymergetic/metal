//! Kernel border publish helpers — full module names onto `reg`.

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

/// Publish finished kernel module borders onto `reg` (firmware bring-up).
/// Host builds return -1 (py/console/log are not linked into this crate).
#[cfg(any(target_os = "none", target_os = "uefi"))]
pub unsafe fn publish_kernel() -> i32 {
    /* Linked by boot — each module exports pm_metal_*_bind_reg. */
    extern "C" {
        fn pm_metal_py_bind_reg() -> i32;
        fn pm_metal_console_bind_reg() -> i32;
        fn pm_metal_log_bind_reg() -> i32;
        fn pm_metal_net_ssh_bind_reg() -> i32;
        fn pm_metal_net_http_server_bind_reg() -> i32;
        fn pm_metal_net_http_microdot_bind_reg() -> i32;
    }
    if pm_metal_py_bind_reg() != 0 {
        return -1;
    }
    if pm_metal_console_bind_reg() != 0 {
        return -1;
    }
    if pm_metal_log_bind_reg() != 0 {
        return -1;
    }
    if pm_metal_net_ssh_bind_reg() != 0 {
        return -1;
    }
    if pm_metal_net_http_server_bind_reg() != 0 {
        return -1;
    }
    if pm_metal_net_http_microdot_bind_reg() != 0 {
        return -1;
    }
    0
}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
pub unsafe fn publish_kernel() -> i32 {
    -1
}
