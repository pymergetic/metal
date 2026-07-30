//! Metal DT — Rust impl, C ABI export (`pm_metal_dt_*`).
//! Not Linux FDT. Class numbers match product `pm_metal_io_class_t` / IO.md.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use pymergetic_metal_rt as _;

const DT_MAX: usize = 256;

/// IO classes (same order as product `PM_METAL_IO_*`). Sync emits a C enum.
#[repr(u32)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum pm_metal_dt_class_t {
    PM_METAL_DT_CLASS_TIME = 0,
    PM_METAL_DT_CLASS_GFX = 1,
    PM_METAL_DT_CLASS_AUDIO = 2,
    PM_METAL_DT_CLASS_INPUT = 3,
    PM_METAL_DT_CLASS_FS = 4,
    PM_METAL_DT_CLASS_STREAM = 5,
    PM_METAL_DT_CLASS_NET = 6,
    PM_METAL_DT_CLASS_RANDOM = 7,
    PM_METAL_DT_CLASS_BLK = 8,
    /// Memory partitioning intel only (lowmem / highmem / heap) — not an access path.
    PM_METAL_DT_CLASS_MEM = 9,
    PM_METAL_DT_CLASS_COUNT = 10,
}

#[repr(u32)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum pm_metal_dt_bus_t {
    PM_METAL_DT_BUS_PLATFORM = 0,
    PM_METAL_DT_BUS_PCI = 1,
    PM_METAL_DT_BUS_ISA = 2,
}

/// Capability bits (not mutually exclusive).
#[repr(u32)]
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum pm_metal_dt_cap_t {
    /// Node already live / bound — harvest must not re-init this device.
    PM_METAL_DT_CAP_BOUND = 1,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct DtNode {
    pub class: pm_metal_dt_class_t,
    pub compat: *const u8,
    pub unit: u32,
    pub caps: u32,
    pub bus: pm_metal_dt_bus_t,
    pub loc: [u32; 4],
}

static mut NODES: [DtNode; DT_MAX] = [DtNode {
    class: pm_metal_dt_class_t::PM_METAL_DT_CLASS_TIME,
    compat: core::ptr::null(),
    unit: 0,
    caps: 0,
    bus: pm_metal_dt_bus_t::PM_METAL_DT_BUS_PLATFORM,
    loc: [0; 4],
}; DT_MAX];
static mut COUNT: u32 = 0;

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dt_reset() {
    COUNT = 0;
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dt_add(node: *const DtNode) -> i32 {
    if node.is_null() || COUNT as usize >= DT_MAX {
        return -1;
    }
    let src = &*node;
    if (src.class as u32) >= (pm_metal_dt_class_t::PM_METAL_DT_CLASS_COUNT as u32) {
        return -1;
    }
    let mut unit = 0u32;
    for i in 0..COUNT as usize {
        if NODES[i].class == src.class {
            unit += 1;
        }
    }
    let id = COUNT as usize;
    NODES[id] = *src;
    NODES[id].unit = unit;
    COUNT += 1;
    id as i32
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dt_get(id: u32) -> *const DtNode {
    if id >= COUNT {
        return core::ptr::null();
    }
    &NODES[id as usize] as *const DtNode
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dt_count() -> u32 {
    COUNT
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dt_count_class(class: pm_metal_dt_class_t) -> u32 {
    let mut n = 0u32;
    for i in 0..COUNT as usize {
        if NODES[i].class == class {
            n += 1;
        }
    }
    n
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dt_by_class(
    class: pm_metal_dt_class_t,
    index: u32,
) -> *const DtNode {
    let mut n = 0u32;
    for i in 0..COUNT as usize {
        if NODES[i].class != class {
            continue;
        }
        if n == index {
            return &NODES[i] as *const DtNode;
        }
        n += 1;
    }
    core::ptr::null()
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dt_lookup(class: pm_metal_dt_class_t) -> *const DtNode {
    pm_metal_dt_by_class(class, 0)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dt_set_compat(
    class: pm_metal_dt_class_t,
    index: u32,
    compat: *const u8,
) -> i32 {
    let mut n = 0u32;
    for i in 0..COUNT as usize {
        if NODES[i].class != class {
            continue;
        }
        if n == index {
            NODES[i].compat = compat;
            return 0;
        }
        n += 1;
    }
    -1
}

/// OR bits into `caps` for the indexed node of `class`. Returns 0 or -1.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dt_or_caps(
    class: pm_metal_dt_class_t,
    index: u32,
    caps: u32,
) -> i32 {
    let mut n = 0u32;
    for i in 0..COUNT as usize {
        if NODES[i].class != class {
            continue;
        }
        if n == index {
            NODES[i].caps |= caps;
            return 0;
        }
        n += 1;
    }
    -1
}

pub type DtIterFn = Option<unsafe extern "C" fn(node: *const DtNode, ctx: *mut core::ffi::c_void) -> i32>;

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dt_foreach(fn_: DtIterFn, ctx: *mut core::ffi::c_void) {
    let Some(cb) = fn_ else {
        return;
    };
    for i in 0..COUNT as usize {
        if cb(&NODES[i] as *const DtNode, ctx) != 0 {
            return;
        }
    }
}

fn loc_u64(base: u64, size: u64) -> [u32; 4] {
    [
        base as u32,
        (base >> 32) as u32,
        size as u32,
        (size >> 32) as u32,
    ]
}

/// Memory partitioning node (intel only — no driver / no access path).
/// `compat` outlives the table (`"lowmem"`, `"highmem"`, `"heap"`, …).
/// `loc` = `[base_lo, base_hi, size_lo, size_hi]`.
/// Heap claim uses `caps | CAP_BOUND` after `mem_init` (alloc already up).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dt_seed_mem(
    compat: *const u8,
    caps: u32,
    base: u64,
    size: u64,
) -> i32 {
    if compat.is_null() || size == 0 {
        return -1;
    }
    let node = DtNode {
        class: pm_metal_dt_class_t::PM_METAL_DT_CLASS_MEM,
        compat,
        unit: 0,
        caps,
        bus: pm_metal_dt_bus_t::PM_METAL_DT_BUS_PLATFORM,
        loc: loc_u64(base, size),
    };
    pm_metal_dt_add(&node)
}

/// Register a floor UART that is already live (COM1 / EFI serial).
/// Call after `pm_metal_dt_reset` (and typically after mem seed).
/// Harvest must skip re-init when `caps & CAP_BOUND` and same bus/loc match.
/// `compat` is NUL-terminated ASCII (e.g. "com1") that outlives the table.
/// `iobase` is ISA I/O base from platform uart floor ops; stored in `loc[0]`.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dt_seed_bound_uart(
    compat: *const u8,
    bus: pm_metal_dt_bus_t,
    iobase: u32,
) -> i32 {
    if compat.is_null() {
        return -1;
    }
    let node = DtNode {
        class: pm_metal_dt_class_t::PM_METAL_DT_CLASS_STREAM,
        compat,
        unit: 0,
        caps: pm_metal_dt_cap_t::PM_METAL_DT_CAP_BOUND as u32,
        bus,
        loc: [iobase, 0, 0, 0],
    };
    pm_metal_dt_add(&node)
}

/// True if a bound stream UART already owns this ISA/platform iobase.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dt_uart_bound(iobase: u32) -> i32 {
    for i in 0..COUNT as usize {
        let n = &NODES[i];
        if n.class != pm_metal_dt_class_t::PM_METAL_DT_CLASS_STREAM {
            continue;
        }
        if (n.caps & (pm_metal_dt_cap_t::PM_METAL_DT_CAP_BOUND as u32)) == 0 {
            continue;
        }
        if n.loc[0] == iobase {
            return 1;
        }
    }
    0
}

/// Rust-side helpers (same table; prefer these inside Rust).
pub mod api {
    use super::*;

    pub fn reset() {
        unsafe { pm_metal_dt_reset() }
    }

    pub fn count() -> u32 {
        unsafe { pm_metal_dt_count() }
    }

    pub fn seed_bound_uart(compat: &'static [u8], bus: pm_metal_dt_bus_t, iobase: u32) -> i32 {
        unsafe { pm_metal_dt_seed_bound_uart(compat.as_ptr(), bus, iobase) }
    }
}
