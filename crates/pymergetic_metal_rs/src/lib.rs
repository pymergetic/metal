//! Product freestanding Rust image — one staticlib, no duplicate rt/core.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]

pub use pymergetic_metal_rt as rt;

use pymergetic_metal_async as _;
use pymergetic_metal_fs as _;
use pymergetic_metal_fs_mtar as _;
use pymergetic_metal_fs_tmpfs as _;
use pymergetic_metal_fs_vfs as _;
use pymergetic_metal_fs_wasmmod as _;
use pymergetic_metal_util_lz4 as _;
use pymergetic_metal_util_size as _;
use pymergetic_metal_util_tar as _;

/// Thin FFI face over Metal lwIP if-mgmt + WireGuard (Py: pymergetic.metal.net.ip / .wg).
pub mod net {
    use core::ffi::c_char;

    extern "C" {
        pub fn pm_metal_net_ip_if_count() -> u32;
        pub fn pm_metal_net_ip_if_gen() -> u32;
        pub fn pm_metal_net_wg_ready() -> i32;
        pub fn pm_metal_net_wg_up(
            private_key_b64: *const c_char,
            listen_port: u16,
            tunnel_ip: *const c_char,
            tunnel_mask: *const c_char,
        ) -> i32;
        pub fn pm_metal_net_wg_down() -> i32;
    }

    #[inline]
    pub fn if_count() -> u32 {
        unsafe { pm_metal_net_ip_if_count() }
    }

    #[inline]
    pub fn wg_ready() -> bool {
        unsafe { pm_metal_net_wg_ready() != 0 }
    }
}