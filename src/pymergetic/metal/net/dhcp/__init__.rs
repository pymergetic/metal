//! net.dhcp — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use pymergetic_metal_rt as _;

#[repr(C)]
pub struct pm_metal_net_dhcp_lease_t {
    _opaque: [u8; 0],
}

extern "C" {
    fn pm_metal_net_dhcp_run(lease_out: *mut pm_metal_net_dhcp_lease_t) -> i32;
}

#[inline] pub unsafe fn run(lease_out: *mut pm_metal_net_dhcp_lease_t) -> i32 { pm_metal_net_dhcp_run(lease_out) }
