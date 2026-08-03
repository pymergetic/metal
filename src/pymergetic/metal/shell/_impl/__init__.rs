//! Thin shell surface — guest/host log line (maps to kernel log today).
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]

use core::cell::Cell;
use core::ffi::c_void;

use pymergetic_metal_reg::{
    pm_metal_reg_mod_load, publish_entries, RegEntry, RegMod, RegModStatic,
};
use pymergetic_metal_rt as _;

#[path = "../../../../../include/pymergetic/metal/log/__init__.rs"]
mod log_face;

/// Append one ASCII line (NUL-terminated) — same path as `pm_metal_log`.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_shell_log(line: *const u8) {
    log_face::pm_metal_log(line);
}

static FLOOR_ENTRIES: RegModStatic<1, 0> = RegModStatic::new(
    [RegEntry::new("pm_metal_shell_log")],
    [],
);

extern "C" fn floor_register_symbols(_ctx: *mut c_void) -> i32 {
    publish_entries(
        &FLOOR_ENTRIES.entries,
        &[pm_metal_shell_log as *const c_void],
    )
}

static FLOOR_MOD: RegMod = RegMod {
    name: "pymergetic.metal.shell",
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
pub unsafe extern "C" fn pm_metal_shell_mod_load() -> i32 {
    pm_metal_reg_mod_load(&FLOOR_MOD)
}
