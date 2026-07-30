//! Shared boot module — Rust impl (harvest / orchestration).
//! Platform ops live in `boot/platform/` (own module faces) — do not re-export
//! them here (would diverge from generated `boot/__init__.h`).
//! Firmware image links this staticlib (pulls rt/mem/dt/console/serial).
#![no_std]

use pymergetic_metal_console as _;
use pymergetic_metal_dev_serial as _;
use pymergetic_metal_dt as _;
use pymergetic_metal_mem as _;
use pymergetic_metal_rt as _;

/// Harvest orchestration (C ABI). Detectors fill `dt`; must skip devices
/// already marked `PM_METAL_DT_CAP_BOUND` (floor UART via `seed_bound_uart`).
#[no_mangle]
pub extern "C" fn pm_metal_boot_harvest() -> i32 {
    /* No detectors linked in the hello image yet. */
    0
}
