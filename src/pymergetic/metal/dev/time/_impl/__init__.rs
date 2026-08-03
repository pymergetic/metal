//! Time probe — seed DT TIME with compat "tsc" when RDTSC is usable.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]

use pymergetic_metal_dt::{
    pm_metal_dt_add, pm_metal_dt_bus_t, pm_metal_dt_cap_t, pm_metal_dt_class_t, pm_metal_dt_count,
    pm_metal_dt_get, DtNode,
};
use pymergetic_metal_rt as _;

static COMPAT_TSC: &[u8] = b"tsc\0";

unsafe fn already_tsc() -> bool {
    let n = pm_metal_dt_count();
    for i in 0..n {
        let p = pm_metal_dt_get(i);
        if p.is_null() {
            continue;
        }
        let node = &*p;
        if node.class != pm_metal_dt_class_t::PM_METAL_DT_CLASS_TIME {
            continue;
        }
        if (node.caps & (pm_metal_dt_cap_t::PM_METAL_DT_CAP_BOUND as u32)) != 0 {
            return true;
        }
        if !node.compat.is_null()
            && *node.compat == b't'
            && *node.compat.add(1) == b's'
            && *node.compat.add(2) == b'c'
            && *node.compat.add(3) == 0
        {
            return true;
        }
    }
    false
}

/// Probe TSC; add DT TIME node. Returns 0 (nothing fatal).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_time_detect() -> i32 {
    if already_tsc() {
        return 0;
    }
    #[cfg(target_arch = "x86_64")]
    {
        let mut lo: u32;
        let mut hi: u32;
        core::arch::asm!("rdtsc", out("eax") lo, out("edx") hi, options(nostack, nomem));
        let _ = ((hi as u64) << 32) | (lo as u64);
        let node = DtNode {
            class: pm_metal_dt_class_t::PM_METAL_DT_CLASS_TIME,
            compat: COMPAT_TSC.as_ptr(),
            unit: 0,
            caps: 0,
            bus: pm_metal_dt_bus_t::PM_METAL_DT_BUS_PLATFORM,
            loc: [0, 0, 0, 0],
        };
        let _ = pm_metal_dt_add(&node);
    }
    0
}

/* Floor RegMod: publish exports for always-proxy faces (W10.1). */
use core::cell::Cell;
use core::ffi::c_void;
use pymergetic_metal_reg::{
    pm_metal_reg_mod_load, publish_entries, RegEntry, RegMod, RegModStatic,
};

static FLOOR_ENTRIES: RegModStatic<1, 0> =
    RegModStatic::new([RegEntry::new("pm_metal_dev_time_detect")], []);

extern "C" fn floor_register_symbols(_ctx: *mut c_void) -> i32 {
    publish_entries(
        &FLOOR_ENTRIES.entries,
        &[pm_metal_dev_time_detect as *const c_void],
    )
}

static FLOOR_MOD: RegMod = RegMod {
    name: "pymergetic.metal.dev.time",
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

/// Load this floor module into the registry (idempotent fail if already loaded).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_time_mod_load() -> i32 {
    pm_metal_reg_mod_load(&FLOOR_MOD)
}
