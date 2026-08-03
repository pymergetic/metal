//! Input probe — PS/2 controller at 0x64 -> DT INPUT compat "ps2".
//! Poll rings (HID key events + pointer) for guest_surface.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(non_camel_case_types)]

use pymergetic_metal_dt::{
    pm_metal_dt_add, pm_metal_dt_bus_t, pm_metal_dt_cap_t, pm_metal_dt_class_t, pm_metal_dt_count,
    pm_metal_dt_get, DtNode,
};
use pymergetic_metal_rt as _;

// `async` is permanently linked: consume its generated fast-path faces
// (never a Cargo dep on async's _impl — see module.md "Consume foreign
// modules"). Faces live under gitignored `include/`; build.rs stages
// copies into OUT_DIR and emits #[path] mods (RA skips gitignored
// #[path] targets). Boot already links async's object code.
include!(concat!(env!("OUT_DIR"), "/async_face_mods.rs"));

#[path = "_poll.rs"]
mod poll;

use async_coro_face::pm_metal_async_coro_state;
use async_handle_face::{pm_metal_async_result_u32, pm_metal_async_set_result_u32};
use async_task_face::{pm_metal_async_spawn, pm_metal_async_prio_t};
use async_time_face::pm_metal_time_mono_us;

pub(crate) const KBC_DATA: u16 = 0x60;
pub(crate) const KBC_STATUS: u16 = 0x64;
pub(crate) const KBC_OUTPUT_FULL: u8 = 1;
pub(crate) const KBC_AUX_DATA: u8 = 1 << 5;

/// HID key event (USB usage id + press + mods).
#[repr(C)]
#[derive(Clone, Copy)]
#[allow(non_camel_case_types)]
pub struct pm_metal_dev_input_key_event_t {
    pub code: u16,
    pub pressed: u8,
    pub mods: u8,
}

/// Pointer sample (absolute/relative + buttons).
#[repr(C)]
#[derive(Clone, Copy)]
#[allow(non_camel_case_types)]
pub struct pm_metal_dev_input_pointer_t {
    pub x: i32,
    pub y: i32,
    pub dx: i32,
    pub dy: i32,
    pub buttons: u32,
    pub flags: u32,
}

const ASYNC_PENDING: u32 = 0;
const ASYNC_DONE: u32 = 2;
const ASYNC_ERROR: u32 = 4;

static COMPAT_PS2: &[u8] = b"ps2\0";

#[repr(C)]
pub(crate) struct IoOps {
    outb: Option<unsafe extern "C" fn(u16, u8)>,
    inb: Option<unsafe extern "C" fn(u16) -> u8>,
    out16: Option<unsafe extern "C" fn(u16, u16)>,
    in16: Option<unsafe extern "C" fn(u16) -> u16>,
    out32: Option<unsafe extern "C" fn(u16, u32)>,
    in32: Option<unsafe extern "C" fn(u16) -> u32>,
}

#[cfg(any(target_os = "none", target_os = "uefi"))]
extern "C" {
    pub(crate) fn pm_metal_boot_io_ops() -> *const IoOps;
}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
pub(crate) fn pm_metal_boot_io_ops() -> *const IoOps {
    core::ptr::null()
}

#[repr(C)]
struct WaitKeyFrame {
    deadline_us: u64,
}

unsafe extern "C" fn wait_key_step(self_h: u32) -> u32 {
    let frame = pm_metal_async_coro_state(self_h) as *mut WaitKeyFrame;
    if frame.is_null() {
        return ASYNC_ERROR;
    }
    let ops = pm_metal_boot_io_ops();
    if ops.is_null() {
        return ASYNC_ERROR;
    }
    let Some(inb) = (*ops).inb else {
        return ASYNC_ERROR;
    };
    let status = inb(KBC_STATUS);
    if (status & KBC_OUTPUT_FULL) != 0 {
        let scan = inb(KBC_DATA);
        if (status & KBC_AUX_DATA) == 0 {
            pm_metal_async_set_result_u32(self_h, scan as u32);
            return ASYNC_DONE;
        }
    }
    if pm_metal_time_mono_us() >= (*frame).deadline_us {
        /* Timeout is successful completion with result 0. */
        pm_metal_async_set_result_u32(self_h, 0);
        return ASYNC_DONE;
    }
    ASYNC_PENDING
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

/// Wait for one raw PS/2 set-1 byte, or complete with 0 at timeout.
/// Returned handle is already scheduled on a runner.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_input_wait_key_async(timeout_ms: u32) -> u32 {
    if timeout_ms == 0 {
        return 0;
    }
    let h = pm_metal_async_spawn(
        Some(wait_key_step),
        core::mem::size_of::<WaitKeyFrame>() as u32,
        pm_metal_async_prio_t::PM_METAL_ASYNC_PRIO_MED,
    );
    if h == 0 {
        return 0;
    }
    let frame = pm_metal_async_coro_state(h) as *mut WaitKeyFrame;
    if frame.is_null() {
        return 0;
    }
    (*frame).deadline_us =
        pm_metal_time_mono_us().saturating_add((timeout_ms as u64).saturating_mul(1000));
    h
}

/// Raw set-1 byte, or 0 when the wait timed out.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_input_wait_key_result(h: u32) -> u32 {
    pm_metal_async_result_u32(h)
}

/// Drain i8042 into rings (also called from poll_*).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_input_poll() {
    poll::poll_port();
}

/// Pop one HID key event; 1=ok, 0=empty.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_input_poll_key_event(
    out: *mut pm_metal_dev_input_key_event_t,
) -> i32 {
    poll::poll_key_event(out)
}

/// Pop one pointer sample; 1=ok, 0=empty.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_input_poll_pointer(
    out: *mut pm_metal_dev_input_pointer_t,
) -> i32 {
    poll::poll_pointer(out)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_input_pointer_lock(surface: u32) -> i32 {
    poll::pointer_lock(surface)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_input_pointer_unlock() {
    poll::pointer_unlock();
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_input_pointer_locked() -> i32 {
    poll::pointer_locked()
}

/// Host/test inject — enqueue a HID key event without HW.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_input_push_key(pressed: i32, code: u16) {
    poll::push_key(pressed, code);
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_dev_input_pointer_enqueue(
    ev: *const pm_metal_dev_input_pointer_t,
) {
    poll::pointer_enqueue(ev);
}

/* Floor RegMod: publish exports for always-proxy faces (W10.1). */
use core::cell::Cell;
use core::ffi::c_void;
use pymergetic_metal_reg::{
    pm_metal_reg_mod_load, publish_entries, RegEntry, RegMod, RegModStatic,
};

static FLOOR_ENTRIES: RegModStatic<10, 0> = RegModStatic::new(
    [
        RegEntry::new("pm_metal_dev_input_detect"),
        RegEntry::new("pm_metal_dev_input_wait_key_async"),
        RegEntry::new("pm_metal_dev_input_wait_key_result"),
        RegEntry::new("pm_metal_dev_input_poll"),
        RegEntry::new("pm_metal_dev_input_poll_key_event"),
        RegEntry::new("pm_metal_dev_input_poll_pointer"),
        RegEntry::new("pm_metal_dev_input_pointer_lock"),
        RegEntry::new("pm_metal_dev_input_pointer_unlock"),
        RegEntry::new("pm_metal_dev_input_pointer_locked"),
        RegEntry::new("pm_metal_dev_input_push_key"),
    ],
    [],
);

extern "C" fn floor_register_symbols(_ctx: *mut c_void) -> i32 {
    publish_entries(
        &FLOOR_ENTRIES.entries,
        &[
            pm_metal_dev_input_detect as *const c_void,
            pm_metal_dev_input_wait_key_async as *const c_void,
            pm_metal_dev_input_wait_key_result as *const c_void,
            pm_metal_dev_input_poll as *const c_void,
            pm_metal_dev_input_poll_key_event as *const c_void,
            pm_metal_dev_input_poll_pointer as *const c_void,
            pm_metal_dev_input_pointer_lock as *const c_void,
            pm_metal_dev_input_pointer_unlock as *const c_void,
            pm_metal_dev_input_pointer_locked as *const c_void,
            pm_metal_dev_input_push_key as *const c_void,
        ],
    )
}

static FLOOR_MOD: RegMod = RegMod {
    name: "pymergetic.metal.dev.input",
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
pub unsafe extern "C" fn pm_metal_dev_input_mod_load() -> i32 {
    pm_metal_reg_mod_load(&FLOOR_MOD)
}
