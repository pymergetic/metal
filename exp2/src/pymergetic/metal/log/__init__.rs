//! Sync log facade -> console #0 (default). Styled lines use ASCII SGR.
//! No ring of its own — console owns history / viewports.
#![cfg_attr(target_os = "none", no_std)]
#![allow(non_camel_case_types)]

use pymergetic_metal_rt as _;

#[cfg(target_os = "none")]
extern "C" {
    fn pm_metal_console_ready() -> i32;
    fn pm_metal_console_write(id: u32, s: *const u8, n: usize);
}

#[cfg(not(target_os = "none"))]
fn pm_metal_console_ready() -> i32 {
    0
}

#[cfg(not(target_os = "none"))]
fn pm_metal_console_write(_id: u32, _s: *const u8, _n: usize) {}

/// Semantic log styles (ANSI SGR when a viewport understands them).
#[repr(u32)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum pm_metal_log_style_t {
    PM_METAL_LOG_STYLE_DEFAULT = 0,
    PM_METAL_LOG_STYLE_DIM = 1,
    PM_METAL_LOG_STYLE_OK = 2,
    PM_METAL_LOG_STYLE_WARN = 3,
    PM_METAL_LOG_STYLE_FAIL = 4,
    PM_METAL_LOG_STYLE_ACCENT = 5,
}

fn style_prefix(style: pm_metal_log_style_t) -> Option<&'static [u8]> {
    match style {
        pm_metal_log_style_t::PM_METAL_LOG_STYLE_DIM => Some(b"\x1b[2m"),
        pm_metal_log_style_t::PM_METAL_LOG_STYLE_OK => Some(b"\x1b[32m"),
        pm_metal_log_style_t::PM_METAL_LOG_STYLE_WARN => Some(b"\x1b[33m"),
        pm_metal_log_style_t::PM_METAL_LOG_STYLE_FAIL => Some(b"\x1b[31m"),
        pm_metal_log_style_t::PM_METAL_LOG_STYLE_ACCENT => Some(b"\x1b[36m"),
        pm_metal_log_style_t::PM_METAL_LOG_STYLE_DEFAULT => None,
    }
}

fn cstr_len(p: *const u8) -> usize {
    if p.is_null() {
        return 0;
    }
    let mut n = 0usize;
    unsafe {
        while n < 512 && *p.add(n) != 0 {
            n += 1;
        }
    }
    n
}

fn emit(bytes: &[u8]) {
    if bytes.is_empty() {
        return;
    }
    unsafe {
        if pm_metal_console_ready() != 0 {
            pm_metal_console_write(0, bytes.as_ptr(), bytes.len());
        }
    }
}

/// 1 if console #0 is ready to accept log lines.
#[no_mangle]
pub extern "C" fn pm_metal_log_ready() -> i32 {
    unsafe { pm_metal_console_ready() }
}

/// Append one ASCII line (NUL-terminated; newline added if missing).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_log(line: *const u8) {
    pm_metal_log_styled(pm_metal_log_style_t::PM_METAL_LOG_STYLE_DEFAULT, line);
}

/// Append one styled ASCII line (NUL-terminated; newline added if missing).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_log_styled(style: pm_metal_log_style_t, line: *const u8) {
    let n = cstr_len(line);
    if let Some(pre) = style_prefix(style) {
        emit(pre);
    }
    if n > 0 {
        emit(core::slice::from_raw_parts(line, n));
    }
    if let Some(_) = style_prefix(style) {
        emit(b"\x1b[0m");
    }
    let needs_nl = n == 0 || unsafe { *line.add(n - 1) } != b'\n';
    if needs_nl {
        emit(b"\n");
    }
}
