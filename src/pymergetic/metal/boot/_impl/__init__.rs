//! Shared boot module — Rust impl (harvest / orchestration).
//! Platform ops live in `boot/platform/` (own module faces) — do not re-export
//! them here (would diverge from generated `boot/__init__.h`).
//! Firmware image links this staticlib (pulls rt/mem/dt/console/serial + detectors).
//!
//! Empty-boot skeleton (`registration_rethink` plan, Phase A): `tree` /
//! `rootfs` / `modload` and their product crates (fs/net/py/wasm/reg/hwtree/
//! util_size/util_tar/util_lz4) are disabled here, not deleted from the
//! tree — revive module by module in later phases. `async` stays linked
//! for `async::time` (TSC calibrate); the run loop / runners are not
//! started from the floor.
#![no_std]
#![allow(non_camel_case_types)]

use pymergetic_metal_async as _;
use pymergetic_metal_console as _;
/* Not called via Rust `use` (see `pm_metal_boot_harvest`, which calls
 * through its generated fast-path face instead) — linked in so its
 * `#[no_mangle]` detector symbol resolves at the final link step. */
use pymergetic_metal_bus_pci as _;
use pymergetic_metal_dev_acpi as _;
use pymergetic_metal_dev_gfx as _;
use pymergetic_metal_dev_input as _;
use pymergetic_metal_dev_random as _;
use pymergetic_metal_dev_serial as _;
use pymergetic_metal_dev_time as _;
use pymergetic_metal_dt as _;
use pymergetic_metal_log as _;
use pymergetic_metal_mem as _;
/* Not called directly (see `boot/.pm/Cargo.toml`) — linked in so `rt`'s
 * raw `extern "C"` call to `pm_metal_reg_register` resolves. */
use pymergetic_metal_reg as _;
use pymergetic_metal_rt as _;
use pymergetic_metal_util_ascii as _;
/* Kernel namespace root: not called via Rust `use` (see `_bootstrap.rs`,
 * which calls through the generated fast-path face instead) — linked in
 * so `pm_metal_kernel_load` resolves at the final link step. */
use pymergetic_metal as _;

#[path = "banner.rs"]
mod banner;
#[path = "_bootstrap.rs"]
mod bootstrap;

/* Peer Rust module detectors are `unloadable = false` (permanently
 * linked): consume their generated fast-path faces, never a direct Cargo
 * call (see docs/definitions/module.md "Consume foreign modules"). Each
 * peer is still Cargo-depended-on above purely so its object code links
 * in. `bus/virtio`/`dev/blk` are the two C-only detector units (no Rust
 * face) — stay raw `extern "C"`. */
#[path = "../../../../../include/pymergetic/metal/bus/pci/__init__.rs"]
mod bus_pci_face;
#[path = "../../../../../include/pymergetic/metal/dev/time/__init__.rs"]
mod dev_time_face;
#[path = "../../../../../include/pymergetic/metal/dev/acpi/__init__.rs"]
mod dev_acpi_face;
#[path = "../../../../../include/pymergetic/metal/dev/random/__init__.rs"]
mod dev_random_face;
#[path = "../../../../../include/pymergetic/metal/dev/input/__init__.rs"]
mod dev_input_face;
#[path = "../../../../../include/pymergetic/metal/dev/gfx/__init__.rs"]
mod dev_gfx_face;

extern "C" {
    fn pm_metal_bus_virtio_detect() -> i32;
    fn pm_metal_dev_blk_detect() -> i32;
}

/// Harvest orchestration (C ABI). Calls linked detectors; each skips
/// already-listed / CAP_BOUND identities. Returns 0.
#[no_mangle]
pub extern "C" fn pm_metal_boot_harvest() -> i32 {
    /* Touch stem so the sibling TU stays in the staticlib under LTO. */
    let _banner = banner::pm_metal_boot_banner as unsafe extern "C" fn();
    let _ = _banner;
    unsafe {
        let _ = pm_metal_bus_virtio_detect();
        let _ = bus_pci_face::pm_metal_bus_pci_detect();
        let _ = dev_time_face::pm_metal_dev_time_detect();
        let _ = dev_acpi_face::pm_metal_dev_acpi_detect();
        let _ = dev_random_face::pm_metal_dev_random_detect();
        let _ = dev_input_face::pm_metal_dev_input_detect();
        let _ = pm_metal_dev_blk_detect();
        let _ = dev_gfx_face::pm_metal_dev_gfx_detect();
    }
    0
}

/// Bootstrap the registry for the whole floor: loads the kernel module
/// first, then `rt`'s dynamic-table register/connect step (see
/// `_bootstrap.rs`). Call once from the loader before the rest of
/// bring-up. Returns 0 or -1.
///
/// # Safety
/// Must be called exactly once, before any other bring-up step (see
/// `_bootstrap.rs::reg_bootstrap`).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_boot_reg_bootstrap() -> i32 {
    bootstrap::reg_bootstrap()
}
