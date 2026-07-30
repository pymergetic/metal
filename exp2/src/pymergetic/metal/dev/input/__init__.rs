//! Input probe — PS/2 controller at 0x64 -> DT INPUT compat "ps2".
#![cfg_attr(target_os = "none", no_std)]

use pymergetic_metal_dt::{
    pm_metal_dt_add, pm_metal_dt_bus_t, pm_metal_dt_cap_t, pm_metal_dt_class_t, pm_metal_dt_count,
    pm_metal_dt_get, DtNode,
};
use pymergetic_metal_rt as _;

const KBC_STATUS: u16 = 0x64;

static COMPAT_PS2: &[u8] = b"ps2\0";

#[repr(C)]
struct IoOps {
    outb: Option<unsafe extern "C" fn(u16, u8)>,
    inb: Option<unsafe extern "C" fn(u16) -> u8>,
    out32: Option<unsafe extern "C" fn(u16, u32)>,
    in32: Option<unsafe extern "C" fn(u16) -> u32>,
}

#[cfg(target_os = "none")]
extern "C" {
    fn pm_metal_boot_io_ops() -> *const IoOps;
}

#[cfg(not(target_os = "none"))]
fn pm_metal_boot_io_ops() -> *const IoOps {
    core::ptr::null()
}

unsafe fn already_ps2() -> bool {
    let n = pm_metal_dt_count();
    for i in 0..n {
        let p = pm_metal_dt_get(i);
        if p.is_null() {
            continue;
        }
        let node = &*p;
        if node.class != pm_metal_dt_class_t::PM_METAL_DT_CLASS_INPUT {
            continue;
        }
        if (node.caps & (pm_metal_dt_cap_t::PM_METAL_DT_CAP_BOUND as u32)) != 0
            && node.loc[0] == KBC_STATUS as u32
        {
            return true;
        }
        if node.bus == pm_metal_dt_bus_t::PM_METAL_DT_BUS_ISA && node.loc[0] == KBC_STATUS as u32 {
            return true;
        }
    }
    false
}

/// Probe PS/2 status port; add DT INPUT if controller looks present. Returns 0.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_input_detect() -> i32 {
    if already_ps2() {
        return 0;
    }
    let ops = pm_metal_boot_io_ops();
    if ops.is_null() {
        return 0;
    }
    let Some(inb) = (*ops).inb else {
        return 0;
    };
    let status = inb(KBC_STATUS);
    /* Floating bus often reads 0xFF — treat as absent. */
    if status == 0xFF {
        return 0;
    }
    let node = DtNode {
        class: pm_metal_dt_class_t::PM_METAL_DT_CLASS_INPUT,
        compat: COMPAT_PS2.as_ptr(),
        unit: 0,
        caps: 0,
        bus: pm_metal_dt_bus_t::PM_METAL_DT_BUS_ISA,
        loc: [KBC_STATUS as u32, 0, 0, 0],
    };
    let _ = pm_metal_dt_add(&node);
    0
}
