//! net.ip — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_net_ip_init(addr_be: u32, mask_be: u32, gw_be: u32) -> i32;
    fn pm_metal_net_ip_ready() -> i32;
    fn pm_metal_net_ip_set_addrs(addr: u32, mask: u32, gw: u32) -> i32;
    fn pm_metal_net_ip_set_dns(dns: u32) -> i32;
    fn pm_metal_net_ip_addr() -> u32;
    fn pm_metal_net_ip_gw() -> u32;
    fn pm_metal_net_ip_mask() -> u32;
    fn pm_metal_net_ip_dns() -> u32;
    fn pm_metal_net_ip_arp_resolve(ip_host: u32) -> i32;
    fn pm_metal_net_ip_announce() -> i32;
    fn pm_metal_net_ip_poll();
    fn pm_metal_net_ip_ping(dst_ip: u32, id: u16, seq: u16) -> i32;
    fn pm_metal_net_ip_ping_replies() -> u32;
}

#[inline] pub fn init(addr_be: u32, mask_be: u32, gw_be: u32) -> i32 { unsafe { pm_metal_net_ip_init(addr_be, mask_be, gw_be) } }
#[inline] pub fn ready() -> i32 { unsafe { pm_metal_net_ip_ready() } }
#[inline] pub fn set_addrs(addr: u32, mask: u32, gw: u32) -> i32 { unsafe { pm_metal_net_ip_set_addrs(addr, mask, gw) } }
#[inline] pub fn set_dns(dns: u32) -> i32 { unsafe { pm_metal_net_ip_set_dns(dns) } }
#[inline] pub fn addr() -> u32 { unsafe { pm_metal_net_ip_addr() } }
#[inline] pub fn gw() -> u32 { unsafe { pm_metal_net_ip_gw() } }
#[inline] pub fn mask() -> u32 { unsafe { pm_metal_net_ip_mask() } }
#[inline] pub fn dns() -> u32 { unsafe { pm_metal_net_ip_dns() } }
#[inline] pub fn arp_resolve(ip_host: u32) -> i32 { unsafe { pm_metal_net_ip_arp_resolve(ip_host) } }
#[inline] pub fn announce() -> i32 { unsafe { pm_metal_net_ip_announce() } }
#[inline] pub fn poll() { unsafe { pm_metal_net_ip_poll() } }
#[inline] pub fn ping(dst_ip: u32, id: u16, seq: u16) -> i32 { unsafe { pm_metal_net_ip_ping(dst_ip, id, seq) } }
#[inline] pub fn ping_replies() -> u32 { unsafe { pm_metal_net_ip_ping_replies() } }
