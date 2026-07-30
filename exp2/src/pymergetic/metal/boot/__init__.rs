//! Shared boot module — Rust impl (harvest / orchestration).
//! Platform ops live in `boot/platform/` (own module faces) — do not re-export
//! them here (would diverge from generated `boot/__init__.h`).
//! Firmware image links this staticlib (pulls rt/mem/dt/console/serial + detectors).
#![no_std]

use pymergetic_metal_async as _;
use pymergetic_metal_bus_pci as _;
use pymergetic_metal_console as _;
use pymergetic_metal_dev_acpi as _;
use pymergetic_metal_dev_blk as _;
use pymergetic_metal_dev_gfx as _;
use pymergetic_metal_dev_input as _;
use pymergetic_metal_dev_random as _;
use pymergetic_metal_dev_serial as _;
use pymergetic_metal_dev_time as _;
use pymergetic_metal_dt as _;
use pymergetic_metal_hwtree as _;
use pymergetic_metal_log as _;
use pymergetic_metal_mem as _;
use pymergetic_metal_rt as _;
use pymergetic_metal_util_ascii as _;
use pymergetic_metal_util_eightcc as _;
use pymergetic_metal_util_fourcc as _;
use pymergetic_metal_util_lz4 as _;
use pymergetic_metal_util_tar as _;

#[path = "banner.rs"]
mod banner;

extern "C" {
    fn pm_metal_bus_pci_detect() -> i32;
    fn pm_metal_dev_time_detect() -> i32;
    fn pm_metal_dev_acpi_detect() -> i32;
    fn pm_metal_dev_random_detect() -> i32;
    fn pm_metal_dev_input_detect() -> i32;
    fn pm_metal_dev_blk_detect() -> i32;
    fn pm_metal_dev_gfx_detect() -> i32;
}

/// Harvest orchestration (C ABI). Calls linked detectors; each skips
/// already-listed / CAP_BOUND identities. Returns 0.
#[no_mangle]
pub extern "C" fn pm_metal_boot_harvest() -> i32 {
    /* Touch banner so the sibling TU stays in the staticlib under LTO. */
    let _banner = banner::pm_metal_boot_banner as unsafe extern "C" fn();
    let _ = _banner;
    unsafe {
        let _ = pm_metal_bus_pci_detect();
        let _ = pm_metal_dev_time_detect();
        let _ = pm_metal_dev_acpi_detect();
        let _ = pm_metal_dev_random_detect();
        let _ = pm_metal_dev_input_detect();
        let _ = pm_metal_dev_blk_detect();
        let _ = pm_metal_dev_gfx_detect();
    }
    0
}
