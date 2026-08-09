//! Hardware tree print — walk DT, log class/compat/bus/loc (+ ACPI RSDP).
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]

use core::fmt::Write;

use pymergetic_metal_dt::{
    pm_metal_dt_bus_t, pm_metal_dt_class_t, pm_metal_dt_foreach, DtNode,
};
use pymergetic_metal_log::{pm_metal_log, pm_metal_log_style_t, pm_metal_log_styled};
use pymergetic_metal_rt as _;

#[cfg(any(target_os = "none", target_os = "uefi"))]
extern "C" {
    fn pm_metal_dev_acpi_rsdp() -> u64;
}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
fn pm_metal_dev_acpi_rsdp() -> u64 {
    0
}

struct LineBuf {
    buf: [u8; 160],
    pos: usize,
}

impl LineBuf {
    fn new() -> Self {
        Self {
            buf: [0; 160],
            pos: 0,
        }
    }

    fn clear(&mut self) {
        self.pos = 0;
        self.buf[0] = 0;
    }

    fn as_cstr(&mut self) -> *const u8 {
        if self.pos >= self.buf.len() {
            self.pos = self.buf.len() - 1;
        }
        self.buf[self.pos] = 0;
        self.buf.as_ptr()
    }
}

impl Write for LineBuf {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        for &b in s.as_bytes() {
            if self.pos + 1 >= self.buf.len() {
                break;
            }
            if b < 0x80 {
                self.buf[self.pos] = b;
                self.pos += 1;
            }
        }
        Ok(())
    }
}

fn class_name(c: pm_metal_dt_class_t) -> &'static str {
    match c {
        pm_metal_dt_class_t::PM_METAL_DT_CLASS_TIME => "TIME",
        pm_metal_dt_class_t::PM_METAL_DT_CLASS_GFX => "GFX",
        pm_metal_dt_class_t::PM_METAL_DT_CLASS_AUDIO => "AUDIO",
        pm_metal_dt_class_t::PM_METAL_DT_CLASS_INPUT => "INPUT",
        pm_metal_dt_class_t::PM_METAL_DT_CLASS_FS => "FS",
        pm_metal_dt_class_t::PM_METAL_DT_CLASS_STREAM => "STREAM",
        pm_metal_dt_class_t::PM_METAL_DT_CLASS_NET => "NET",
        pm_metal_dt_class_t::PM_METAL_DT_CLASS_RANDOM => "RANDOM",
        pm_metal_dt_class_t::PM_METAL_DT_CLASS_BLK => "BLK",
        pm_metal_dt_class_t::PM_METAL_DT_CLASS_MEM => "MEM",
        _ => "?",
    }
}

fn bus_name(b: pm_metal_dt_bus_t) -> &'static str {
    match b {
        pm_metal_dt_bus_t::PM_METAL_DT_BUS_PLATFORM => "plat",
        pm_metal_dt_bus_t::PM_METAL_DT_BUS_PCI => "pci",
        pm_metal_dt_bus_t::PM_METAL_DT_BUS_ISA => "isa",
    }
}

fn compat_str(p: *const u8) -> &'static str {
    if p.is_null() {
        return "-";
    }
    let mut n = 0usize;
    unsafe {
        while n < 32 && *p.add(n) != 0 {
            n += 1;
        }
        core::str::from_utf8_unchecked(core::slice::from_raw_parts(p, n))
    }
}

unsafe extern "C" fn on_node(node: *const DtNode, ctx: *mut core::ffi::c_void) -> i32 {
    if node.is_null() || ctx.is_null() {
        return 0;
    }
    let line = &mut *(ctx as *mut LineBuf);
    let n = &*node;
    line.clear();
    let _ = write!(
        line,
        "hw: {} compat={} bus={} loc={:x}:{:x}:{:x}:{:x}",
        class_name(n.class),
        compat_str(n.compat),
        bus_name(n.bus),
        n.loc[0],
        n.loc[1],
        n.loc[2],
        n.loc[3]
    );
    pm_metal_log(line.as_cstr());
    0
}

/// Print DT inventory (+ ACPI RSDP if known). Returns 0.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_hwtree_print() -> i32 {
    pm_metal_log_styled(
        pm_metal_log_style_t::PM_METAL_LOG_STYLE_DIM,
        b"hwtree:\0".as_ptr(),
    );
    let mut line = LineBuf::new();
    pm_metal_dt_foreach(Some(on_node), &mut line as *mut LineBuf as *mut core::ffi::c_void);
    let rsdp = pm_metal_dev_acpi_rsdp();
    if rsdp != 0 {
        line.clear();
        let _ = write!(line, "hw: acpi rsdp=0x{:x}", rsdp);
        pm_metal_log(line.as_cstr());
    }
    0
}


use core::ffi::c_void;

use pymergetic_metal_reg::{pm_metal_reg_mod_load, RegMod};

pymergetic_metal_reg::reg_mod! {
    mod hwtree = "pymergetic.metal.hwtree";
    exports: [print];
}

extern "C" fn hwtree_register_symbols(_ctx: *mut c_void) -> i32 {
    hwtree::print.publish(pm_metal_hwtree_print as *const c_void);
    0
}

static HWTREE_MOD: RegMod = RegMod::from_static(
    hwtree::NAME,
    &hwtree::STORAGE.exports,
    &hwtree::STORAGE.imports,
    Some(hwtree_register_symbols),
);

#[no_mangle]
pub extern "C" fn pm_metal_hwtree_reg_load() -> i32 {
    if pymergetic_metal_reg::find_mod(hwtree::NAME).is_some() {
        return 0;
    }
    unsafe { pm_metal_reg_mod_load(&HWTREE_MOD) }
}

#[inline]
pub fn reg_load() -> i32 {
    pm_metal_hwtree_reg_load()
}
