//! Gfx probe — Multiboot LFB not exposed on this floor yet.
//! PCI display devices are added by `pm_metal_bus_pci_detect` (class 0x03).
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]

use pymergetic_metal_dt as _;
use pymergetic_metal_rt as _;

/// Probe early framebuffer. Returns 0 (nothing found / deferred).
#[no_mangle]
pub extern "C" fn pm_metal_dev_gfx_detect() -> i32 {
    0
}
