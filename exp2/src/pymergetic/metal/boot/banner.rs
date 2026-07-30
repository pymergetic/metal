//! Welcome banner — FIGlet "METAL" via util/ascii + log ACCENT.

use pymergetic_metal_log::{pm_metal_log_style_t, pm_metal_log_styled};

extern "C" {
    fn pm_metal_util_ascii_render(
        text: *const u8,
        cb: Option<unsafe extern "C" fn(ctx: *mut u8, s: *const u8, n: usize) -> i32>,
        ctx: *mut u8,
    ) -> i32;
}

unsafe extern "C" fn log_row(ctx: *mut u8, s: *const u8, n: usize) -> i32 {
    let _ = ctx;
    if s.is_null() || n == 0 {
        return 0;
    }
    /* Copy to a small NUL-terminated buffer for log_styled. */
    let mut buf = [0u8; 192];
    let take = if n + 1 < buf.len() { n } else { buf.len() - 1 };
    for i in 0..take {
        let b = *s.add(i);
        buf[i] = if b < 0x80 { b } else { b'?' };
    }
    buf[take] = 0;
    pm_metal_log_styled(
        pm_metal_log_style_t::PM_METAL_LOG_STYLE_ACCENT,
        buf.as_ptr(),
    );
    0
}

/// Print the boot welcome banner onto the log/console path.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_boot_banner() {
    if pm_metal_util_ascii_render(b"METAL\0".as_ptr(), Some(log_row), core::ptr::null_mut()) != 0 {
        pm_metal_log_styled(
            pm_metal_log_style_t::PM_METAL_LOG_STYLE_ACCENT,
            b"METAL\0".as_ptr(),
        );
    }
}
