//! Bringup smoke: parent awaits sleep_us, then DONE.

use crate::engine::{self, Handle};
use crate::handle::pm_metal_async_status_t;

#[cfg(target_os = "none")]
extern "C" {
    fn pm_metal_log(line: *const u8);
}

#[cfg(not(target_os = "none"))]
unsafe fn pm_metal_log(_line: *const u8) {}

#[repr(C)]
struct ProofFrame {
    pc: i32,
    child: Handle,
}

unsafe extern "C" fn proof_step(self_h: Handle) -> u32 {
    let p = engine::coro_state(self_h) as *mut ProofFrame;
    if p.is_null() {
        return pm_metal_async_status_t::PM_METAL_ASYNC_ERROR as u32;
    }
    let s = &mut *p;
    if s.pc == 0 {
        s.child = crate::time::pm_metal_async_sleep_us(500);
        if s.child == engine::INVALID {
            return pm_metal_async_status_t::PM_METAL_ASYNC_ERROR as u32;
        }
        s.pc = 1;
    }
    if s.pc == 1 {
        let st = engine::await_child(self_h, s.child);
        match st {
            engine::Status::Waiting => {
                return pm_metal_async_status_t::PM_METAL_ASYNC_WAITING as u32;
            }
            engine::Status::Done => {
                s.pc = 2;
            }
            _ => return pm_metal_async_status_t::PM_METAL_ASYNC_ERROR as u32,
        }
    }
    pm_metal_async_status_t::PM_METAL_ASYNC_DONE as u32
}

/// Start runners (n=1), spawn proof task, poll until done. Returns 0 or -1.
#[no_mangle]
pub extern "C" fn pm_metal_async_boot_proof() -> i32 {
    if !engine::started() && engine::start(1) != 0 {
        return -1;
    }
    let h = engine::spawn(
        proof_step,
        core::mem::size_of::<ProofFrame>() as u32,
        engine::Prio::High,
    );
    if h == engine::INVALID {
        unsafe {
            pm_metal_log(b"async: spawn failed\0".as_ptr());
        }
        return -1;
    }
    let _ = engine::process_crown(h);
    let mut spins = 0u32;
    loop {
        let st = engine::status_of(h);
        if st == engine::Status::Done {
            unsafe {
                pm_metal_log(b"async: PASS\0".as_ptr());
            }
            return 0;
        }
        if st == engine::Status::Error || st == engine::Status::Cancelled {
            unsafe {
                pm_metal_log(b"async: FAIL\0".as_ptr());
            }
            return -1;
        }
        let _ = engine::run_poll_all();
        spins = spins.wrapping_add(1);
        if spins > 2_000_000 {
            unsafe {
                pm_metal_log(b"async: TIMEOUT\0".as_ptr());
            }
            return -1;
        }
    }
}
