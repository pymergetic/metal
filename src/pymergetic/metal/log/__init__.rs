//! Log face — plain lines go to C `pm_metal_log`; styled lines add SGR.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use pymergetic_metal_rt as _;

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

extern "C" {
    /// Board/live C sink (`port/live/metal_log.c` / wasm HAL).
    pub fn pm_metal_log(line: *const u8);
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
        while n < 480 && *p.add(n) != 0 {
            n += 1;
        }
    }
    n
}

/// Append one styled ASCII line (NUL-terminated; newline via C sink).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_log_styled(style: pm_metal_log_style_t, line: *const u8) {
    let n = cstr_len(line);
    let mut buf = [0u8; 512];
    let mut pos = 0usize;
    if let Some(pre) = style_prefix(style) {
        buf[pos..pos + pre.len()].copy_from_slice(pre);
        pos += pre.len();
    }
    if n > 0 && pos + n < buf.len() - 4 {
        core::ptr::copy_nonoverlapping(line, buf.as_mut_ptr().add(pos), n);
        pos += n;
    }
    if style_prefix(style).is_some() {
        let reset = b"\x1b[0m";
        buf[pos..pos + reset.len()].copy_from_slice(reset);
        pos += reset.len();
    }
    buf[pos] = 0;
    pm_metal_log(buf.as_ptr());
}


use core::ffi::c_void;

use pymergetic_metal_reg::{pm_metal_reg_mod_load, RegMod};

pymergetic_metal_reg::reg_mod! {
    mod log = "pymergetic.metal.log";
    exports: [styled];
}

extern "C" fn log_register_symbols(_ctx: *mut c_void) -> i32 {
    log::styled.publish(pm_metal_log_styled as *const c_void);
    0
}

static LOG_MOD: RegMod = RegMod::from_static(
    log::NAME,
    &log::STORAGE.exports,
    &log::STORAGE.imports,
    Some(log_register_symbols),
);

#[no_mangle]
pub extern "C" fn pm_metal_log_reg_load() -> i32 {
    if pymergetic_metal_reg::find_mod(log::NAME).is_some() {
        return 0;
    }
    unsafe { pm_metal_reg_mod_load(&LOG_MOD) }
}

#[inline]
pub fn reg_load() -> i32 {
    pm_metal_log_reg_load()
}
