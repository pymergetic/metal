//! Block probe — classic IDE primary at 0x1F0 -> DT BLK compat "ide".
//! Virtio-blk enumeration is via bus/pci (base class 0x01).
#![cfg_attr(target_os = "none", no_std)]

use pymergetic_metal_dt::{
    pm_metal_dt_add, pm_metal_dt_bus_t, pm_metal_dt_cap_t, pm_metal_dt_class_t, pm_metal_dt_count,
    pm_metal_dt_get, DtNode,
};
use pymergetic_metal_rt as _;

const IDE_PRIMARY: u16 = 0x1F0;
const IDE_STATUS: u16 = 0x1F7;

static COMPAT_IDE: &[u8] = b"ide\0";

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

unsafe fn already_ide() -> bool {
    let n = pm_metal_dt_count();
    for i in 0..n {
        let p = pm_metal_dt_get(i);
        if p.is_null() {
            continue;
        }
        let node = &*p;
        if node.class != pm_metal_dt_class_t::PM_METAL_DT_CLASS_BLK {
            continue;
        }
        if node.bus == pm_metal_dt_bus_t::PM_METAL_DT_BUS_ISA && node.loc[0] == IDE_PRIMARY as u32 {
            return true;
        }
        if (node.caps & (pm_metal_dt_cap_t::PM_METAL_DT_CAP_BOUND as u32)) != 0
            && node.loc[0] == IDE_PRIMARY as u32
        {
            return true;
        }
    }
    false
}

/// Probe IDE primary; add DT BLK if status looks live. Returns 0.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_blk_detect() -> i32 {
    if already_ide() {
        return 0;
    }
    let ops = pm_metal_boot_io_ops();
    if ops.is_null() {
        return 0;
    }
    let Some(inb) = (*ops).inb else {
        return 0;
    };
    let st = inb(IDE_STATUS);
    if st == 0xFF {
        return 0;
    }
    let node = DtNode {
        class: pm_metal_dt_class_t::PM_METAL_DT_CLASS_BLK,
        compat: COMPAT_IDE.as_ptr(),
        unit: 0,
        caps: 0,
        bus: pm_metal_dt_bus_t::PM_METAL_DT_BUS_ISA,
        loc: [IDE_PRIMARY as u32, 0, 0, 0],
    };
    let _ = pm_metal_dt_add(&node);
    0
}
