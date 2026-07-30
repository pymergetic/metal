//! Input probe — PS/2 controller at 0x64 -> DT INPUT compat "ps2".
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]

use pymergetic_metal_dt::{
    pm_metal_dt_add, pm_metal_dt_bus_t, pm_metal_dt_cap_t, pm_metal_dt_class_t, pm_metal_dt_count,
    pm_metal_dt_get, DtNode,
};
use pymergetic_metal_async as _;
use pymergetic_metal_rt as _;

const KBC_DATA: u16 = 0x60;
const KBC_STATUS: u16 = 0x64;
const KBC_OUTPUT_FULL: u8 = 1;
const KBC_AUX_DATA: u8 = 1 << 5;
const ASYNC_PENDING: u32 = 0;
const ASYNC_DONE: u32 = 2;
const ASYNC_ERROR: u32 = 4;
const ASYNC_PRIO_MED: u32 = 1;

static COMPAT_PS2: &[u8] = b"ps2\0";

#[repr(C)]
struct IoOps {
    outb: Option<unsafe extern "C" fn(u16, u8)>,
    inb: Option<unsafe extern "C" fn(u16) -> u8>,
    out32: Option<unsafe extern "C" fn(u16, u32)>,
    in32: Option<unsafe extern "C" fn(u16) -> u32>,
}

#[cfg(any(target_os = "none", target_os = "uefi"))]
extern "C" {
    fn pm_metal_boot_io_ops() -> *const IoOps;
    fn pm_metal_async_spawn(
        step: Option<unsafe extern "C" fn(u32) -> u32>,
        state_bytes: u32,
        prio: u32,
    ) -> u32;
    fn pm_metal_async_coro_state(h: u32) -> *mut u8;
    fn pm_metal_async_set_result_u32(h: u32, value: u32);
    fn pm_metal_async_result_u32(h: u32) -> u32;
    fn pm_metal_time_mono_us() -> u64;
}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
fn pm_metal_boot_io_ops() -> *const IoOps {
    core::ptr::null()
}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_async_spawn(
    _step: Option<unsafe extern "C" fn(u32) -> u32>,
    _state_bytes: u32,
    _prio: u32,
) -> u32 {
    0
}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_async_coro_state(_h: u32) -> *mut u8 {
    core::ptr::null_mut()
}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_async_set_result_u32(_h: u32, _value: u32) {}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_async_result_u32(_h: u32) -> u32 {
    0
}

#[cfg(not(any(target_os = "none", target_os = "uefi")))]
unsafe fn pm_metal_time_mono_us() -> u64 {
    0
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
        ASYNC_PRIO_MED,
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
