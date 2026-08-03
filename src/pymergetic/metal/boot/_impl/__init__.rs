//! Shared boot module — Rust impl (harvest / orchestration / product wiring).
//! Platform ops live in `boot/platform/` (own module faces).
#![no_std]
#![allow(non_camel_case_types)]

extern crate alloc;

use pymergetic_metal_async as _;
use pymergetic_metal_bus_pci as _;
use pymergetic_metal_console as _;
use pymergetic_metal_dev_acpi as _;
use pymergetic_metal_dev_blk_ram as _;
use pymergetic_metal_dev_audio as _;
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
use pymergetic_metal_fs_vfs as _;
use pymergetic_metal_forge as _;
use pymergetic_metal_hwtree as _;
use pymergetic_metal_log as _;
use pymergetic_metal_shell as _;
use pymergetic_metal_mem as _;
use pymergetic_metal_net_dns as _;
use pymergetic_metal_net_ftp as _;
use pymergetic_metal_net_http as _;
use pymergetic_metal_net_http_microdot as _;
use pymergetic_metal_net_ip as _;
use pymergetic_metal_net_ip_icmp as _;
use pymergetic_metal_net_ip_tcp as _;
use pymergetic_metal_net_ip_udp as _;
use pymergetic_metal_net_ntp as _;
use pymergetic_metal_net_ssh as _;
use pymergetic_metal_net_tftp as _;
use pymergetic_metal_py as _;
use pymergetic_metal_reg as _;
use pymergetic_metal_rt as _;
use pymergetic_metal_util_ascii as _;
use pymergetic_metal_util_lz4 as _;
use pymergetic_metal_util_size as _;
use pymergetic_metal_util_tar as _;
use pymergetic_metal_wasm as _;
use pymergetic_metal as _;

#[path = "banner.rs"]
mod banner;
#[path = "_bootstrap.rs"]
mod bootstrap;

/* Nested boot product stems (no separate Cargo crate — live in this staticlib). */
#[path = "../tree/_impl/__init__.rs"]
mod tree;
#[path = "../externals/_impl/__init__.rs"]
mod externals;
#[path = "../rootfs/_impl/__init__.rs"]
mod rootfs;
#[path = "../modload/_impl/__init__.rs"]
mod modload;

/* Peer detectors: always-proxy faces (see docs/definitions/module.md). */
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

/// Harvest orchestration (C ABI). Returns 0.
#[no_mangle]
pub extern "C" fn pm_metal_boot_harvest() -> i32 {
    let _banner = banner::pm_metal_boot_banner as unsafe extern "C" fn();
    let _ = _banner;
    let _tree = tree::pm_metal_boot_tree_print as unsafe extern "C" fn() -> i32;
    let _ = _tree;
    let _ext = externals::pm_metal_boot_externals_mod_load as unsafe extern "C" fn() -> i32;
    let _ = _ext;
    let _root = rootfs::pm_metal_boot_rootfs_mount_all as unsafe extern "C" fn() -> i32;
    let _ = _root;
    let _src = rootfs::pm_metal_boot_rootfs_proof_src_browse as unsafe extern "C" fn() -> i32;
    let _ = _src;
    let _forge = pymergetic_metal_forge::pm_metal_forge_proof_render as unsafe extern "C" fn() -> i32;
    let _ = _forge;
    let _mod = modload::pm_metal_boot_mod_unload as unsafe extern "C" fn(*const u8) -> i32;
    let _ = _mod;
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

/// Bootstrap the registry for the whole floor + product modules.
///
/// # Safety
/// Must be called exactly once, before the rest of bring-up.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_boot_reg_bootstrap() -> i32 {
    bootstrap::reg_bootstrap()
}
