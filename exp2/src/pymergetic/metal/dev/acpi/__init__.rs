//! ACPI RSDP discovery (x86 BIOS area / EBDA).
//! No DT class fits ACPI yet — store a static pointer for hwtree; log the find.
#![cfg_attr(target_os = "none", no_std)]

use core::sync::atomic::{AtomicUsize, Ordering};

use pymergetic_metal_log as _;
use pymergetic_metal_rt as _;

static RSDP: AtomicUsize = AtomicUsize::new(0);

#[cfg(target_os = "none")]
extern "C" {
    fn pm_metal_log(line: *const u8);
}

#[cfg(not(target_os = "none"))]
fn pm_metal_log(_line: *const u8) {}

fn sig_ok(p: *const u8) -> bool {
    const SIG: &[u8] = b"RSD PTR ";
    unsafe {
        for i in 0..8 {
            if *p.add(i) != SIG[i] {
                return false;
            }
        }
    }
    true
}

fn checksum_ok(p: *const u8, len: usize) -> bool {
    let mut sum: u8 = 0;
    unsafe {
        for i in 0..len {
            sum = sum.wrapping_add(*p.add(i));
        }
    }
    sum == 0
}

unsafe fn scan_range(start: usize, len: usize) -> Option<usize> {
    let end = start.saturating_add(len);
    let mut addr = start & !0xFusize;
    while addr + 20 <= end {
        let p = addr as *const u8;
        if sig_ok(p) && checksum_ok(p, 20) {
            return Some(addr);
        }
        addr += 16;
    }
    None
}

/// Find RSDP; log and stash pointer. Returns 1 if found, 0 otherwise.
/// (No DT add — no ACPI class in the IO enum yet.)
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_acpi_detect() -> i32 {
    #[cfg(not(target_arch = "x86_64"))]
    {
        return 0;
    }
    #[cfg(target_arch = "x86_64")]
    {
        if RSDP.load(Ordering::Relaxed) != 0 {
            return 1;
        }
        /* EBDA segment pointer at 0x40E (real-mode BDA). */
        let ebda_seg = *((0x40Eu64) as *const u16) as usize;
        let ebda = ebda_seg << 4;
        if ebda != 0 {
            if let Some(a) = scan_range(ebda, 1024) {
                RSDP.store(a, Ordering::Relaxed);
                pm_metal_log(b"acpi: rsdp found (ebda)\0".as_ptr());
                return 1;
            }
        }
        if let Some(a) = scan_range(0xE0000, 0x20000) {
            RSDP.store(a, Ordering::Relaxed);
            pm_metal_log(b"acpi: rsdp found (bios)\0".as_ptr());
            return 1;
        }
        0
    }
}

/// Physical address of RSDP, or 0 if not found.
#[no_mangle]
pub extern "C" fn pm_metal_dev_acpi_rsdp() -> u64 {
    RSDP.load(Ordering::Relaxed) as u64
}
