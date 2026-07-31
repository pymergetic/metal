//! Shared boot module — Rust impl (harvest / orchestration).
//! Platform ops live in `boot/platform/` (own module faces) — do not re-export
//! them here (would diverge from generated `boot/__init__.h`).
//! Firmware image links this staticlib (pulls rt/mem/dt/console/serial + detectors).
#![no_std]

use pymergetic_metal_async as _;
use pymergetic_metal_bus_pci as _;
use pymergetic_metal_console as _;
use pymergetic_metal_dev_acpi as _;
use pymergetic_metal_dev_blk_ram as _;
use pymergetic_metal_dev_gfx as _;
use pymergetic_metal_dev_input as _;
use pymergetic_metal_dev_random as _;
use pymergetic_metal_dev_serial as _;
use pymergetic_metal_dev_time as _;
use pymergetic_metal_dt as _;
use pymergetic_metal_fs as _;
use pymergetic_metal_fs_fat as _;
use pymergetic_metal_fs_littlefs as _;
use pymergetic_metal_fs_mtar as _;
use pymergetic_metal_fs_tmpfs as _;
use pymergetic_metal_hwtree as _;
use pymergetic_metal_log as _;
use pymergetic_metal_mem as _;
use pymergetic_metal_net_dns as _;
use pymergetic_metal_net_ftp as _;
use pymergetic_metal_net_http as _;
use pymergetic_metal_net_ip as _;
use pymergetic_metal_net_ntp as _;
use pymergetic_metal_net_ip_icmp as _;
use pymergetic_metal_net_ip_tcp as _;
use pymergetic_metal_net_tftp as _;
use pymergetic_metal_net_ip_udp as _;
use pymergetic_metal_rt as _;
use pymergetic_metal_util_ascii as _;
use pymergetic_metal_util_lz4 as _;
use pymergetic_metal_util_size as _;
use pymergetic_metal_util_tar as _;
use pymergetic_metal_fs_vfs as _;

#[path = "banner.rs"]
mod banner;

#[path = "tree/__init__.rs"]
mod tree;

#[path = "rootfs/__init__.rs"]
mod rootfs;

#[path = "modload/__init__.rs"]
mod modload;

extern "C" {
    fn pm_metal_bus_virtio_detect() -> i32;
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
    /* Touch stems so sibling TUs stay in the staticlib under LTO. */
    let _banner = banner::pm_metal_boot_banner as unsafe extern "C" fn();
    let _tree = tree::pm_metal_boot_tree_print as unsafe extern "C" fn() -> i32;
    let _rootfs = rootfs::pm_metal_boot_rootfs_mount_all as unsafe extern "C" fn() -> i32;
    let _mod_load = modload::pm_metal_boot_mod_load
        as unsafe extern "C" fn(*const u8, *const u8, usize, *const u8, usize) -> i32;
    let _mod_unload = modload::pm_metal_boot_mod_unload as unsafe extern "C" fn(*const u8) -> i32;
    let _ = (_banner, _tree, _rootfs, _mod_load, _mod_unload);
    unsafe {
        let _ = pm_metal_bus_virtio_detect();
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
