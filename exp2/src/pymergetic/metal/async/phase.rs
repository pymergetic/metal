//! Phase table helper — PC through ops[0..n).
//!
//! Phase fn return:
//!   0 .. n-1  goto that index
//!   n         DONE
//!   -1        SAME (park / waiting, pc unchanged)
//!   -2        FAIL -> table.fail, then ERROR

use crate::engine::Handle;
use crate::handle::pm_metal_async_status_t;

/// Phase body function (returns next pc / sentinel).
pub type pm_metal_async_phase_fn_t = Option<unsafe extern "C" fn(Handle) -> i32>;

/// Standardized phase script.
#[repr(C)]
pub struct pm_metal_async_phase_table_t {
    pub fail: pm_metal_async_phase_fn_t,
    pub end: pm_metal_async_phase_fn_t,
    pub n: u32,
    pub ops: *const pm_metal_async_phase_fn_t,
}

const PHASE_SAME: i32 = -1;
const PHASE_FAIL: i32 = -2;

/// Run one phase tick for `*pc`. Updates `*pc` on goto/done/fail.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_async_phase_step(
    self_h: Handle,
    table: *const pm_metal_async_phase_table_t,
    pc: *mut i32,
) -> pm_metal_async_status_t {
    if table.is_null() || pc.is_null() {
        return pm_metal_async_status_t::PM_METAL_ASYNC_ERROR;
    }
    let t = &*table;
    if t.n == 0 || t.ops.is_null() {
        return pm_metal_async_status_t::PM_METAL_ASYNC_ERROR;
    }
    let mut cur = *pc;
    if cur < 0 {
        cur = 0;
        *pc = 0;
    }
    let n = t.n as i32;
    if cur >= n {
        if let Some(end) = t.end {
            let _ = end(self_h);
        }
        return pm_metal_async_status_t::PM_METAL_ASYNC_DONE;
    }
    let op = *t.ops.add(cur as usize);
    let Some(op) = op else {
        return pm_metal_async_status_t::PM_METAL_ASYNC_ERROR;
    };
    let r = op(self_h);
    if r == PHASE_SAME {
        return pm_metal_async_status_t::PM_METAL_ASYNC_WAITING;
    }
    if r == PHASE_FAIL {
        if let Some(fail) = t.fail {
            let _ = fail(self_h);
        }
        *pc = n;
        return pm_metal_async_status_t::PM_METAL_ASYNC_ERROR;
    }
    if r < 0 {
        return pm_metal_async_status_t::PM_METAL_ASYNC_ERROR;
    }
    if r > n {
        return pm_metal_async_status_t::PM_METAL_ASYNC_ERROR;
    }
    *pc = r;
    if r == n {
        if let Some(end) = t.end {
            let _ = end(self_h);
        }
        return pm_metal_async_status_t::PM_METAL_ASYNC_DONE;
    }
    pm_metal_async_status_t::PM_METAL_ASYNC_PENDING
}
