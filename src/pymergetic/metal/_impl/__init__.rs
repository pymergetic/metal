//! `pymergetic.metal` — the kernel namespace root, and (per
//! docs/definitions/module.md "Kernel is a module too") the first module
//! loaded in the same register/connect/lifecycle shape as every other
//! module, not a bare marker with zero ABI.
//!
//! It has no exports of its own (no peer calls `pymergetic.metal.<fn>`
//! through the registry) and no imports (the spine, `mem`/`reg`, is a
//! direct Cargo dependency for every consumer, not resolved through this
//! module's own import slots). Its only real job is being module #0 in
//! the kernel table (`pymergetic_metal_reg`'s `RegMod` list) so a
//! genuinely `unloadable` provider loaded later (wasm, Python) has a
//! real, permanent root to key its own `parent` on for cascading unload
//! — see `docs/definitions/module.md` "Module lifecycle".
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(dead_code, non_camel_case_types)]

use core::cell::Cell;

use pymergetic_metal_reg::RegMod;

static KERNEL_MOD: RegMod = RegMod {
    name: "pymergetic.metal",
    unloadable: false,
    parent: None,
    ctx: core::ptr::null_mut(),
    on_load: None,
    register_symbols: None,
    connect_symbols: None,
    on_registrations_updated: None,
    deregister_symbols: None,
    on_unload: None,
    entries: &[],
    imports: &[],
    raw_next: Cell::new(core::ptr::null()),
    raw_prev: Cell::new(core::ptr::null()),
};

/// Load the kernel module: first call in boot order, before any other
/// module's own bring-up (see `boot/_impl/_bootstrap.rs`). `0` on
/// success, `-1` if already loaded (a repeat/duplicate boot call, which
/// is itself a bug in the caller — kernel is architecturally first and
/// loaded exactly once).
///
/// Named `pm_metal_kernel_*` rather than bare `pm_metal_*`: the C ABI
/// naming rule prefixes every export with its full path segment list
/// after `pymergetic/metal/`, which is empty for this module itself —
/// `kernel` is the one deliberate exception, spelling out what would
/// otherwise be an unreadably bare `pm_metal_load`.
///
/// # Safety
/// Must be called at most once, before any other module's own
/// `pm_metal_reg_mod_load` call in the same boot.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_kernel_load() -> i32 {
    pymergetic_metal_reg::pm_metal_reg_mod_load(&KERNEL_MOD)
}
