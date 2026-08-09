//! dev.acpi — Rust face over the C impl.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use pymergetic_metal_rt as _;

extern "C" {
    fn pm_metal_dev_acpi_set_rsdp(addr: u64);
    fn pm_metal_dev_acpi_rsdp() -> u64;
    fn pm_metal_dev_acpi_init() -> i32;
    fn pm_metal_dev_acpi_cpu_count() -> u32;
    fn pm_metal_dev_acpi_apic_id(cpu_index: u32) -> u32;
    fn pm_metal_dev_acpi_lapic_base() -> u64;
}

#[inline] pub fn set_rsdp(addr: u64) { unsafe { pm_metal_dev_acpi_set_rsdp(addr) } }
#[inline] pub fn rsdp() -> u64 { unsafe { pm_metal_dev_acpi_rsdp() } }
#[inline] pub fn init() -> i32 { unsafe { pm_metal_dev_acpi_init() } }
#[inline] pub fn cpu_count() -> u32 { unsafe { pm_metal_dev_acpi_cpu_count() } }
#[inline] pub fn apic_id(cpu_index: u32) -> u32 { unsafe { pm_metal_dev_acpi_apic_id(cpu_index) } }
#[inline] pub fn lapic_base() -> u64 { unsafe { pm_metal_dev_acpi_lapic_base() } }
