//! bus.pci — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_bus_pci_read32(bus: u8, dev: u8, func: u8, offset: u8) -> u32;
    fn pm_metal_bus_pci_read16(bus: u8, dev: u8, func: u8, offset: u8) -> u16;
    fn pm_metal_bus_pci_read8(bus: u8, dev: u8, func: u8, offset: u8) -> u8;
    fn pm_metal_bus_pci_write16(bus: u8, dev: u8, func: u8, offset: u8, val: u16);
    fn pm_metal_bus_pci_write32(bus: u8, dev: u8, func: u8, offset: u8, val: u32);
    fn pm_metal_bus_pci_enable_mem_bm(bus: u8, dev: u8, func: u8);
    fn pm_metal_bus_pci_bar_mmio(bus: u8, dev: u8, func: u8, bar_index: u8, bars_consumed: *mut u8) -> u64;
    fn pm_metal_bus_pci_find(vendor: u16, device: u16, bus_out: *mut u8, dev_out: *mut u8, func_out: *mut u8) -> i32;
}

#[inline] pub fn read32(bus: u8, dev: u8, func: u8, offset: u8) -> u32 { unsafe { pm_metal_bus_pci_read32(bus, dev, func, offset) } }
#[inline] pub fn read16(bus: u8, dev: u8, func: u8, offset: u8) -> u16 { unsafe { pm_metal_bus_pci_read16(bus, dev, func, offset) } }
#[inline] pub fn read8(bus: u8, dev: u8, func: u8, offset: u8) -> u8 { unsafe { pm_metal_bus_pci_read8(bus, dev, func, offset) } }
#[inline] pub fn write16(bus: u8, dev: u8, func: u8, offset: u8, val: u16) { unsafe { pm_metal_bus_pci_write16(bus, dev, func, offset, val) } }
#[inline] pub fn write32(bus: u8, dev: u8, func: u8, offset: u8, val: u32) { unsafe { pm_metal_bus_pci_write32(bus, dev, func, offset, val) } }
#[inline] pub fn enable_mem_bm(bus: u8, dev: u8, func: u8) { unsafe { pm_metal_bus_pci_enable_mem_bm(bus, dev, func) } }
#[inline] pub unsafe fn bar_mmio(bus: u8, dev: u8, func: u8, bar_index: u8, bars_consumed: *mut u8) -> u64 { pm_metal_bus_pci_bar_mmio(bus, dev, func, bar_index, bars_consumed) }
#[inline] pub unsafe fn find(vendor: u16, device: u16, bus_out: *mut u8, dev_out: *mut u8, func_out: *mut u8) -> i32 { pm_metal_bus_pci_find(vendor, device, bus_out, dev_out, func_out) }
