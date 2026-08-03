//! Serial hardware — unified access over `boot/platform` uart lower half.
//! Not a console. Becoming a viewport requires a manual `console.attach`.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]

use pymergetic_metal_rt as _;

#[repr(C)]
struct UartOps {
    write: Option<unsafe extern "C" fn(*const u8, usize)>,
    /// Non-blocking single-byte read: -1 if none available, else 0..255.
    /// Prefix-only mirror of `pm_metal_boot_uart_ops_t` -- the real C
    /// struct also carries `floor_iobase`/`floor_compat` after this, but
    /// nothing here reads past `try_getc` so the extra tail fields never
    /// need a Rust-side field to line up.
    try_getc: Option<unsafe extern "C" fn() -> i32>,
}

#[cfg(any(target_os = "none", target_os = "uefi"))]
extern "C" {
    fn pm_metal_boot_uart_ops() -> *const UartOps;
}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
fn pm_metal_boot_uart_ops() -> *const UartOps {
    core::ptr::null()
}

static mut READY: bool = false;

/// Bring up serial via the platform uart lower half.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_serial_init() -> i32 {
    let ops = pm_metal_boot_uart_ops();
    if ops.is_null() || (*ops).write.is_none() {
        return -1;
    }
    READY = true;
    0
}

/// Write ASCII bytes to the serial device (hardware path).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_serial_write(s: *const u8, n: usize) {
    if !READY || s.is_null() || n == 0 {
        return;
    }
    let ops = pm_metal_boot_uart_ops();
    if ops.is_null() {
        return;
    }
    if let Some(w) = (*ops).write {
        w(s, n);
    }
}

/// 1 if serial init succeeded.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_serial_ready() -> i32 {
    if READY {
        1
    } else {
        0
    }
}

/// Drain up to `cap` already-available bytes from the serial device
/// (hardware path) into `buf`, non-blocking. Returns the number of bytes
/// actually read (0 if none were waiting), or `-1` if not ready / bad
/// args / no `try_getc` op.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_serial_try_read(buf: *mut u8, cap: usize) -> i32 {
    if !READY || buf.is_null() || cap == 0 {
        return -1;
    }
    let ops = pm_metal_boot_uart_ops();
    if ops.is_null() {
        return -1;
    }
    let Some(try_getc) = (*ops).try_getc else {
        return -1;
    };
    let mut n = 0usize;
    while n < cap {
        let c = try_getc();
        if c < 0 {
            break;
        }
        *buf.add(n) = c as u8;
        n += 1;
    }
    n as i32
}
