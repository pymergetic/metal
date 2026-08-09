//! net.dhcp — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use pymergetic_metal_rt as _;

#[repr(C)]
pub struct pm_metal_net_dhcp_lease_t {
    pub yiaddr: u32,
    pub mask: u32,
    pub gw: u32,
    pub dns: u32,
    pub server: u32,
}

extern "C" {
    fn pm_metal_net_dhcp_start() -> u32;
    fn pm_metal_net_dhcp_poll();
    fn pm_metal_net_dhcp_lease() -> *const pm_metal_net_dhcp_lease_t;
    fn pm_metal_net_dhcp_run(lease_out: *mut pm_metal_net_dhcp_lease_t) -> i32;
}

#[inline] pub fn start() -> u32 { unsafe { pm_metal_net_dhcp_start() } }
#[inline] pub fn poll() { unsafe { pm_metal_net_dhcp_poll() } }
#[inline] pub unsafe fn lease() -> *const pm_metal_net_dhcp_lease_t { pm_metal_net_dhcp_lease() }
#[inline] pub unsafe fn run(lease_out: *mut pm_metal_net_dhcp_lease_t) -> i32 { pm_metal_net_dhcp_run(lease_out) }
