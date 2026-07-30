//! PCI config-space enumeration via boot platform IO ops -> DT.
//! Detect / identify only (no BAR program, no driver bind).
#![cfg_attr(target_os = "none", no_std)]

use pymergetic_metal_dt::{
    pm_metal_dt_add, pm_metal_dt_bus_t, pm_metal_dt_class_t, pm_metal_dt_count, pm_metal_dt_get,
    DtNode,
};
use pymergetic_metal_rt as _;

const PCI_CONFIG_ADDR: u16 = 0xCF8;
const PCI_CONFIG_DATA: u16 = 0xCFC;
const PCI_VENDOR_INVALID: u16 = 0xFFFF;
/* Scan buses 0..MAX_BUS inclusive (keep bounded for early bring-up). */
const MAX_BUS: u8 = 1;

static COMPAT_PCI: &[u8] = b"pci\0";

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

unsafe fn cfg_read32(bus: u8, dev: u8, func: u8, offset: u8) -> Option<u32> {
    let ops = pm_metal_boot_io_ops();
    if ops.is_null() {
        return None;
    }
    let out32 = (*ops).out32?;
    let in32 = (*ops).in32?;
    let addr = 0x8000_0000u32
        | ((bus as u32) << 16)
        | ((dev as u32) << 11)
        | ((func as u32) << 8)
        | ((offset as u32) & 0xFCu32);
    out32(PCI_CONFIG_ADDR, addr);
    Some(in32(PCI_CONFIG_DATA))
}

fn map_class(base: u8) -> Option<pm_metal_dt_class_t> {
    match base {
        0x01 => Some(pm_metal_dt_class_t::PM_METAL_DT_CLASS_BLK),
        0x02 => Some(pm_metal_dt_class_t::PM_METAL_DT_CLASS_NET),
        0x03 => Some(pm_metal_dt_class_t::PM_METAL_DT_CLASS_GFX),
        0x0c => Some(pm_metal_dt_class_t::PM_METAL_DT_CLASS_STREAM),
        _ => None,
    }
}

unsafe fn already_listed(bus: u8, dev: u8, func: u8) -> bool {
    let n = pm_metal_dt_count();
    for i in 0..n {
        let p = pm_metal_dt_get(i);
        if p.is_null() {
            continue;
        }
        let node = &*p;
        if node.bus != pm_metal_dt_bus_t::PM_METAL_DT_BUS_PCI {
            continue;
        }
        if node.loc[0] == bus as u32 && node.loc[1] == dev as u32 && node.loc[2] == func as u32 {
            return true;
        }
    }
    false
}

/// Scan PCI config space; add DT nodes for mapped base classes. Returns 0.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_bus_pci_detect() -> i32 {
    for bus in 0u8..=MAX_BUS {
        for dev in 0u8..32 {
            for func in 0u8..8 {
                let Some(id) = cfg_read32(bus, dev, func, 0) else {
                    return 0;
                };
                let vendor = (id & 0xFFFF) as u16;
                if vendor == PCI_VENDOR_INVALID {
                    if func == 0 {
                        break;
                    }
                    continue;
                }
                if already_listed(bus, dev, func) {
                    continue;
                }
                let Some(class_reg) = cfg_read32(bus, dev, func, 0x08) else {
                    return 0;
                };
                let base_class = ((class_reg >> 24) & 0xFF) as u8;
                let Some(class) = map_class(base_class) else {
                    continue;
                };
                let device = (id >> 16) & 0xFFFF;
                let node = DtNode {
                    class,
                    compat: COMPAT_PCI.as_ptr(),
                    unit: 0,
                    caps: 0, /* not CAP_BOUND — probe inventory only */
                    bus: pm_metal_dt_bus_t::PM_METAL_DT_BUS_PCI,
                    /* loc: bus, dev, func, vendor:device */
                    loc: [bus as u32, dev as u32, func as u32, (vendor as u32) << 16 | device],
                };
                let _ = pm_metal_dt_add(&node);
                if func == 0 {
                    if let Some(hdr) = cfg_read32(bus, dev, 0, 0x0C) {
                        if ((hdr >> 16) & 0x80) == 0 {
                            break;
                        }
                    }
                }
            }
        }
    }
    0
}
