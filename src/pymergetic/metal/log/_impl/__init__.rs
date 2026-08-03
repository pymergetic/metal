//! Sync log facade -> console #0 (default). Styled lines use ASCII SGR.
//! No ring of its own — console owns history / viewports.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use pymergetic_metal_rt as _;

// `console` via always-proxy face (see docs/definitions/module.md
// "Two face shapes"). Final link unit (`boot`) Cargo-depends on console.
#[path = "../../../../../include/pymergetic/metal/console/__init__.rs"]
mod console_face;

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

unsafe fn emit(bytes: &[u8]) {
    if bytes.is_empty() {
        return;
    }
    if console_face::pm_metal_console_ready() != 0 {
        console_face::pm_metal_console_write(0, bytes.as_ptr(), bytes.len());
    }
}

/// 1 if console #0 is ready to accept log lines.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_log_ready() -> i32 {
    console_face::pm_metal_console_ready()
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
    if style_prefix(style).is_some() {
        emit(b"\x1b[0m");
    }
    let needs_nl = n == 0 || *line.add(n - 1) != b'\n';
    if needs_nl {
        emit(b"\n");
    }
}

/* Floor RegMod: publish exports for always-proxy faces (W10.1). */
use core::cell::Cell;
use core::ffi::c_void;
use pymergetic_metal_reg::{
    pm_metal_reg_mod_load, publish_entries, RegEntry, RegMod, RegModStatic,
};

static FLOOR_ENTRIES: RegModStatic<3, 0> = RegModStatic::new(
    [
        RegEntry::new("pm_metal_log_ready"),
        RegEntry::new("pm_metal_log"),
        RegEntry::new("pm_metal_log_styled"),
    ],
    [],
);

extern "C" fn floor_register_symbols(_ctx: *mut c_void) -> i32 {
    publish_entries(
        &FLOOR_ENTRIES.entries,
        &[
            pm_metal_log_ready as *const c_void,
            pm_metal_log as *const c_void,
            pm_metal_log_styled as *const c_void,
        ],
    )
}

static FLOOR_MOD: RegMod = RegMod {
    name: "pymergetic.metal.log",
    unloadable: false,
    parent: None,
    ctx: core::ptr::null_mut(),
    on_load: None,
    register_symbols: Some(floor_register_symbols),
    connect_symbols: None,
    on_registrations_updated: None,
    deregister_symbols: None,
    on_unload: None,
    entries: &FLOOR_ENTRIES.entries,
    imports: &[],
    raw_next: Cell::new(core::ptr::null()),
    raw_prev: Cell::new(core::ptr::null()),
};

#[no_mangle]
pub unsafe extern "C" fn pm_metal_log_mod_load() -> i32 {
    pm_metal_reg_mod_load(&FLOOR_MOD)
}
