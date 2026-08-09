//! mem umbrella — pulls nested lock/arena/tlsf faces into one crate name
//! for dependents (`wamr_host`). Host heap remains C `mem/port/mem.c`.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]

use pymergetic_metal_mem_arena as _;
use pymergetic_metal_mem_lock as _;
use pymergetic_metal_mem_tlsf as _;
use pymergetic_metal_rt as _;


use pymergetic_metal_reg::{pm_metal_reg_mod_load, RegMod};

static MEM_MOD: RegMod = RegMod::py_inventory("pymergetic.metal.mem");

#[no_mangle]
pub extern "C" fn pm_metal_mem_reg_load() -> i32 {
    if pymergetic_metal_reg::find_mod("pymergetic.metal.mem").is_some() {
        return 0;
    }
    unsafe { pm_metal_reg_mod_load(&MEM_MOD) }
}

#[inline]
pub fn reg_load() -> i32 {
    pm_metal_mem_reg_load()
}
