//! net.wg — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use core::ffi::c_char;

use pymergetic_metal_rt as _;

#[repr(C)]
pub struct pm_metal_net_wg_status_t {
    _opaque: [u8; 0],
}

extern "C" {
    fn pm_metal_net_wg_up_named(ifname: *const c_char, private_key_b64: *const c_char, listen_port: u16, tunnel_ip: *const c_char, tunnel_mask: *const c_char) -> i32;
    fn pm_metal_net_wg_down_named(ifname: *const c_char) -> i32;
    fn pm_metal_net_wg_peer_add_named(ifname: *const c_char, public_key_b64: *const c_char, endpoint_ip: *const c_char, endpoint_port: u16, allowed_ip: *const c_char, allowed_mask: *const c_char) -> i32;
    fn pm_metal_net_wg_peer_del_named(ifname: *const c_char, public_key_b64: *const c_char) -> i32;
    fn pm_metal_net_wg_status_named(ifname: *const c_char, out: *mut pm_metal_net_wg_status_t) -> i32;
    fn pm_metal_net_wg_up(private_key_b64: *const c_char, listen_port: u16, tunnel_ip: *const c_char, tunnel_mask: *const c_char) -> i32;
    fn pm_metal_net_wg_down() -> i32;
    fn pm_metal_net_wg_peer_add(public_key_b64: *const c_char, endpoint_ip: *const c_char, endpoint_port: u16, allowed_ip: *const c_char, allowed_mask: *const c_char) -> i32;
    fn pm_metal_net_wg_peer_del(public_key_b64: *const c_char) -> i32;
    fn pm_metal_net_wg_status(out: *mut pm_metal_net_wg_status_t) -> i32;
    fn pm_metal_net_wg_ready() -> i32;
    fn pm_metal_net_wg_handshake_smoke() -> i32;
}

#[inline] pub unsafe fn up_named(ifname: *const c_char, private_key_b64: *const c_char, listen_port: u16, tunnel_ip: *const c_char, tunnel_mask: *const c_char) -> i32 { pm_metal_net_wg_up_named(ifname, private_key_b64, listen_port, tunnel_ip, tunnel_mask) }
#[inline] pub unsafe fn down_named(ifname: *const c_char) -> i32 { pm_metal_net_wg_down_named(ifname) }
#[inline] pub unsafe fn peer_add_named(ifname: *const c_char, public_key_b64: *const c_char, endpoint_ip: *const c_char, endpoint_port: u16, allowed_ip: *const c_char, allowed_mask: *const c_char) -> i32 { pm_metal_net_wg_peer_add_named(ifname, public_key_b64, endpoint_ip, endpoint_port, allowed_ip, allowed_mask) }
#[inline] pub unsafe fn peer_del_named(ifname: *const c_char, public_key_b64: *const c_char) -> i32 { pm_metal_net_wg_peer_del_named(ifname, public_key_b64) }
#[inline] pub unsafe fn status_named(ifname: *const c_char, out: *mut pm_metal_net_wg_status_t) -> i32 { pm_metal_net_wg_status_named(ifname, out) }
#[inline] pub unsafe fn up(private_key_b64: *const c_char, listen_port: u16, tunnel_ip: *const c_char, tunnel_mask: *const c_char) -> i32 { pm_metal_net_wg_up(private_key_b64, listen_port, tunnel_ip, tunnel_mask) }
#[inline] pub fn down() -> i32 { unsafe { pm_metal_net_wg_down() } }
#[inline] pub unsafe fn peer_add(public_key_b64: *const c_char, endpoint_ip: *const c_char, endpoint_port: u16, allowed_ip: *const c_char, allowed_mask: *const c_char) -> i32 { pm_metal_net_wg_peer_add(public_key_b64, endpoint_ip, endpoint_port, allowed_ip, allowed_mask) }
#[inline] pub unsafe fn peer_del(public_key_b64: *const c_char) -> i32 { pm_metal_net_wg_peer_del(public_key_b64) }
#[inline] pub unsafe fn status(out: *mut pm_metal_net_wg_status_t) -> i32 { pm_metal_net_wg_status(out) }
#[inline] pub fn ready() -> i32 { unsafe { pm_metal_net_wg_ready() } }
#[inline] pub fn handshake_smoke() -> i32 { unsafe { pm_metal_net_wg_handshake_smoke() } }
