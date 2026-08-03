//! ACPI RSDP discovery + MADT CPU count.
//! No DT class fits ACPI yet — store a static pointer for hwtree.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]

use core::sync::atomic::{AtomicUsize, Ordering};

use pymergetic_metal_rt as _;

static RSDP: AtomicUsize = AtomicUsize::new(0);

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

fn read_u32(p: *const u8, off: usize) -> u32 {
    unsafe {
        u32::from_le_bytes([
            *p.add(off),
            *p.add(off + 1),
            *p.add(off + 2),
            *p.add(off + 3),
        ])
    }
}

fn read_u64(p: *const u8, off: usize) -> u64 {
    unsafe {
        u64::from_le_bytes([
            *p.add(off),
            *p.add(off + 1),
            *p.add(off + 2),
            *p.add(off + 3),
            *p.add(off + 4),
            *p.add(off + 5),
            *p.add(off + 6),
            *p.add(off + 7),
        ])
    }
}

fn sdt_sig_is(p: *const u8, sig: &[u8; 4]) -> bool {
    unsafe {
        *p.add(0) == sig[0]
            && *p.add(1) == sig[1]
            && *p.add(2) == sig[2]
            && *p.add(3) == sig[3]
    }
}

unsafe fn find_table(rsdp: usize, want: &[u8; 4]) -> Option<*const u8> {
    if rsdp == 0 {
        return None;
    }
    let rp = rsdp as *const u8;
    if !sig_ok(rp) || !checksum_ok(rp, 20) {
        return None;
    }
    let rev = *rp.add(15);
    /* Prefer XSDT when RSDP rev >= 2. */
    if rev >= 2 {
        let len = read_u32(rp, 20) as usize;
        if len >= 36 && checksum_ok(rp, len) {
            let xsdt = read_u64(rp, 24) as usize;
            if xsdt != 0 {
                if let Some(t) = walk_xsdt(xsdt, want) {
                    return Some(t);
                }
            }
        }
    }
    let rsdt = read_u32(rp, 16) as usize;
    if rsdt != 0 {
        return walk_rsdt(rsdt, want);
    }
    None
}

unsafe fn walk_rsdt(rsdt: usize, want: &[u8; 4]) -> Option<*const u8> {
    let p = rsdt as *const u8;
    let len = read_u32(p, 4) as usize;
    if len < 36 || !checksum_ok(p, len) || !sdt_sig_is(p, b"RSDT") {
        return None;
    }
    let mut off = 36usize;
    while off + 4 <= len {
        let entry = read_u32(p, off) as usize;
        off += 4;
        if entry == 0 {
            continue;
        }
        let t = entry as *const u8;
        let tlen = read_u32(t, 4) as usize;
        if tlen < 36 || !checksum_ok(t, tlen) {
            continue;
        }
        if sdt_sig_is(t, want) {
            return Some(t);
        }
    }
    None
}

unsafe fn walk_xsdt(xsdt: usize, want: &[u8; 4]) -> Option<*const u8> {
    let p = xsdt as *const u8;
    let len = read_u32(p, 4) as usize;
    if len < 36 || !checksum_ok(p, len) || !sdt_sig_is(p, b"XSDT") {
        return None;
    }
    let mut off = 36usize;
    while off + 8 <= len {
        let entry = read_u64(p, off) as usize;
        off += 8;
        if entry == 0 {
            continue;
        }
        let t = entry as *const u8;
        let tlen = read_u32(t, 4) as usize;
        if tlen < 36 || !checksum_ok(t, tlen) {
            continue;
        }
        if sdt_sig_is(t, want) {
            return Some(t);
        }
    }
    None
}

/// Walk enabled local APICs (types 0 and 9). `want` = None counts; Some(i) returns that APIC id.
unsafe fn madt_cpu_walk(madt: *const u8, want: Option<u32>) -> Option<u32> {
    let len = read_u32(madt, 4) as usize;
    if len < 44 || !sdt_sig_is(madt, b"APIC") {
        return None;
    }
    let mut off = 44usize;
    let mut n = 0u32;
    while off + 2 <= len {
        let typ = *madt.add(off);
        let elen = *madt.add(off + 1) as usize;
        if elen < 2 || off + elen > len {
            break;
        }
        let apic = match typ {
            0 => {
                /* Processor Local APIC: id at +3, flags at +4 bit0 = enabled. */
                if elen >= 8 && (read_u32(madt, off + 4) & 1) != 0 {
                    Some(*madt.add(off + 3) as u32)
                } else {
                    None
                }
            }
            9 => {
                /* Processor Local x2APIC: id at +4, flags at +8. */
                if elen >= 16 && (read_u32(madt, off + 8) & 1) != 0 {
                    Some(read_u32(madt, off + 4))
                } else {
                    None
                }
            }
            _ => None,
        };
        if let Some(id) = apic {
            match want {
                None => n = n.saturating_add(1),
                Some(i) if i == n => return Some(id),
                Some(_) => n = n.saturating_add(1),
            }
        }
        off += elen;
    }
    if want.is_none() {
        Some(n)
    } else {
        None
    }
}

unsafe fn madt_cpu_count(madt: *const u8) -> u32 {
    madt_cpu_walk(madt, None).unwrap_or(0)
}

/// Inject RSDP from firmware (EFI config table). No-op if addr is 0.
#[no_mangle]
pub extern "C" fn pm_metal_dev_acpi_set_rsdp(addr: u64) {
    if addr == 0 {
        return;
    }
    let a = addr as usize;
    let p = a as *const u8;
    if !sig_ok(p) {
        return;
    }
    let _ = RSDP.compare_exchange(0, a, Ordering::Relaxed, Ordering::Relaxed);
}

/// Find RSDP; log and stash pointer. Returns 1 if found, 0 otherwise.
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
                return 1;
            }
        }
        if let Some(a) = scan_range(0xE0000, 0x20000) {
            RSDP.store(a, Ordering::Relaxed);
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

unsafe fn with_madt<F, R>(f: F) -> Option<R>
where
    F: FnOnce(*const u8) -> R,
{
    #[cfg(not(target_arch = "x86_64"))]
    {
        let _ = f;
        None
    }
    #[cfg(target_arch = "x86_64")]
    {
        if RSDP.load(Ordering::Relaxed) == 0 {
            let _ = pm_metal_dev_acpi_detect();
        }
        let rsdp = RSDP.load(Ordering::Relaxed);
        find_table(rsdp, b"APIC").map(f)
    }
}

/// Enabled CPU count from MADT; 0 if RSDP/MADT unavailable (caller falls back).
#[no_mangle]
pub extern "C" fn pm_metal_dev_acpi_cpu_count() -> u32 {
    unsafe { with_madt(|madt| madt_cpu_count(madt)).unwrap_or(0) }
}

/// APIC id for enabled CPU `index` (0 .. cpu_count-1). Returns -1 if missing.
#[no_mangle]
pub extern "C" fn pm_metal_dev_acpi_cpu_apic_id(index: u32) -> i32 {
    unsafe {
        match with_madt(|madt| madt_cpu_walk(madt, Some(index))) {
            Some(Some(id)) => id as i32,
            _ => -1,
        }
    }
}

/* Floor RegMod: publish exports for always-proxy faces (W10.1). */
use core::cell::Cell;
use core::ffi::c_void;
use pymergetic_metal_reg::{
    pm_metal_reg_mod_load, publish_entries, RegEntry, RegMod, RegModStatic,
};

static FLOOR_ENTRIES: RegModStatic<5, 0> = RegModStatic::new(
    [
        RegEntry::new("pm_metal_dev_acpi_set_rsdp"),
        RegEntry::new("pm_metal_dev_acpi_detect"),
        RegEntry::new("pm_metal_dev_acpi_rsdp"),
        RegEntry::new("pm_metal_dev_acpi_cpu_count"),
        RegEntry::new("pm_metal_dev_acpi_cpu_apic_id"),
    ],
    [],
);

extern "C" fn floor_register_symbols(_ctx: *mut c_void) -> i32 {
    publish_entries(
        &FLOOR_ENTRIES.entries,
        &[
            pm_metal_dev_acpi_set_rsdp as *const c_void,
            pm_metal_dev_acpi_detect as *const c_void,
            pm_metal_dev_acpi_rsdp as *const c_void,
            pm_metal_dev_acpi_cpu_count as *const c_void,
            pm_metal_dev_acpi_cpu_apic_id as *const c_void,
        ],
    )
}

static FLOOR_MOD: RegMod = RegMod {
    name: "pymergetic.metal.dev.acpi",
    unloadable: false,
    parent: None,
    ctx: core::ptr::null_mut(),
    on_load: None,
    register_symbols: Some(floor_register_symbols),
    connect_symbols: None,
    on_registrations_updated: None,
    deregister_symbols: None,
    on_unload: None,
    entries: &FLOOR_ENTRIES.entries,
    imports: &[],
    raw_next: Cell::new(core::ptr::null()),
    raw_prev: Cell::new(core::ptr::null()),
};

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_acpi_mod_load() -> i32 {
    pm_metal_reg_mod_load(&FLOOR_MOD)
}
