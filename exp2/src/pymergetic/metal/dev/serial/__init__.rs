//! Serial hardware — unified access over `boot/platform` uart lower half.
//! Not a console. Becoming a viewport requires a manual `console.attach`.
#![cfg_attr(target_os = "none", no_std)]

use pymergetic_metal_rt as _;

#[repr(C)]
struct UartOps {
    write: Option<unsafe extern "C" fn(*const u8, usize)>,
}

#[cfg(target_os = "none")]
extern "C" {
    fn pm_metal_boot_uart_ops() -> *const UartOps;
}

#[cfg(not(target_os = "none"))]
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
