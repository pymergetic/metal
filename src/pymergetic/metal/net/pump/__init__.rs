//! net.pump — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_net_pump_once();
    fn pm_metal_net_pump_bind_async();
    fn pm_metal_net_await_tcp_established_h(sock: u32) -> u32;
    fn pm_metal_net_await_tcp_rx_h(sock: u32, min_bytes: u32) -> u32;
    fn pm_metal_net_await_tcp_established(sock: u32, max_iters: u32) -> i32;
    fn pm_metal_net_await_tcp_rx(sock: u32, min_bytes: u32, max_iters: u32) -> i32;
    fn pm_metal_net_pump_wake_tcp();
}

#[inline] pub fn pump_once() { unsafe { pm_metal_net_pump_once() } }
#[inline] pub fn pump_bind_async() { unsafe { pm_metal_net_pump_bind_async() } }
#[inline] pub fn await_tcp_established_h(sock: u32) -> u32 { unsafe { pm_metal_net_await_tcp_established_h(sock) } }
#[inline] pub fn await_tcp_rx_h(sock: u32, min_bytes: u32) -> u32 { unsafe { pm_metal_net_await_tcp_rx_h(sock, min_bytes) } }
#[inline] pub fn await_tcp_established(sock: u32, max_iters: u32) -> i32 { unsafe { pm_metal_net_await_tcp_established(sock, max_iters) } }
#[inline] pub fn await_tcp_rx(sock: u32, min_bytes: u32, max_iters: u32) -> i32 { unsafe { pm_metal_net_await_tcp_rx(sock, min_bytes, max_iters) } }
#[inline] pub fn pump_wake_tcp() { unsafe { pm_metal_net_pump_wake_tcp() } }
