//! Metal py edge — alloc/GC-off/async bridge/reg bind (upy mirror comes later).
//!
//! Finished slices only: no hollow loop/VM. Link into firmware when bring-up
//! needs it; host smoke covers the edge today.
#![cfg_attr(any(target_os = "none", target_os = "uefi"), no_std)]
#![allow(dead_code, non_camel_case_types)]

#[path = "_alloc.rs"]
mod alloc;
#[path = "_async_bridge.rs"]
mod async_bridge;
#[path = "_bind.rs"]
mod bind;
#[path = "_gc_off.rs"]
mod gc_off;
#[path = "_libc_policy.rs"]
mod libc_policy;
#[path = "_loop.rs"]
mod repl_loop;
#[path = "_proof.rs"]
mod proof;
#[path = "_shell.rs"]
mod shell;

#[path = "upy/mod.rs"]
pub mod upy;

use pymergetic_metal_async as _;
use pymergetic_metal_dev_serial as _;
use pymergetic_metal_fs as _;
use pymergetic_metal_log as _;
use pymergetic_metal_mem as _;
use pymergetic_metal_reg as _;
use pymergetic_metal_rt as _;
use pymergetic_metal_util_ascii as _;

/// Edge + B0 upy faces ready (VM loop still omitted).
#[no_mangle]
pub extern "C" fn pm_metal_py_ready() -> i32 {
    if !upy::py::mpstate::ready() {
        upy::py::mpstate::init();
    }
    0
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_py_alloc(size: usize) -> *mut u8 {
    alloc::alloc(size)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_py_free(ptr: *mut u8) {
    alloc::free(ptr)
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_py_realloc(ptr: *mut u8, size: usize) -> *mut u8 {
    alloc::realloc(ptr, size)
}

/// 0 = GC disabled (always).
#[no_mangle]
pub extern "C" fn pm_metal_py_gc_enabled() -> i32 {
    if gc_off::enabled() {
        1
    } else {
        0
    }
}

#[no_mangle]
pub extern "C" fn pm_metal_py_gc_collect() -> i32 {
    gc_off::collect()
}

/// Metal libc policy id (`1` = Metal).
#[no_mangle]
pub extern "C" fn pm_metal_py_libc_policy() -> u32 {
    libc_policy::current() as u32
}

/// Park on Metal await (async-first).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_py_await(self_h: u32, child_h: u32) -> u32 {
    async_bridge::await_child(self_h, child_h) as u32
}

#[no_mangle]
pub extern "C" fn pm_metal_py_sleep_us(us: u64) -> u32 {
    async_bridge::sleep_us(us)
}

/// One short, non-blocking REPL tick. Drains available input (own feed
/// ring, then the mphal port's real stdin) into the session line buffer;
/// once a submitted line closes with `\n`, lexes/parses/compiles/runs the
/// accumulated buffer. Returns:
/// - `0` -- idle or progressed (nothing to run yet, or a line ran to
///   completion / auto-printed a value)
/// - `1` -- the submission is incomplete (PS2 continuation -- caller
///   should keep feeding lines)
/// - `-1` -- a line buffer overflow, or the submission raised/failed to
///   parse/compile (session line buffer is cleared either way)
#[no_mangle]
pub extern "C" fn pm_metal_py_loop_step() -> i32 {
    repl_loop::step()
}

/// Inject bytes for the loop to read on later `step()` calls (tests /
/// non-stream hosts). Returns the number of bytes queued, or `-1` for a
/// null `ptr`.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_py_loop_feed(ptr: *const u8, len: usize) -> i32 {
    repl_loop::feed(ptr, len)
}

/// Clear the loop's line buffer, feed ring, and session globals. Always 0.
#[no_mangle]
pub extern "C" fn pm_metal_py_loop_reset() -> i32 {
    repl_loop::reset()
}

/// Last auto-printed expression's small-int value -- only meaningful
/// when [`pm_metal_py_loop_last_result_valid`] returns nonzero.
#[no_mangle]
pub extern "C" fn pm_metal_py_loop_last_result_i32() -> i32 {
    repl_loop::last_result_i32()
}

/// `1` iff the last executed submission was an auto-printed small-int
/// value (i.e. `pm_metal_py_loop_last_result_i32` holds a real result).
#[no_mangle]
pub extern "C" fn pm_metal_py_loop_last_result_valid() -> i32 {
    repl_loop::last_result_valid()
}

/// Publish edge symbols onto `reg` (`pymergetic.metal.py.*`).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_py_bind_reg() -> i32 {
    bind::publish()
}

/// Spawn the REPL-as-boot-shell async task (see `_shell.rs` doc). `0` on
/// success; `-1` if already running or the spawn failed.
#[no_mangle]
pub extern "C" fn pm_metal_py_shell_start() -> i32 {
    shell::start()
}

/// `1` iff the boot shell task is running, else `0`.
#[no_mangle]
pub extern "C" fn pm_metal_py_shell_running() -> i32 {
    shell::running()
}

/// W4.1 proof: py ready (print path exercised by await note / tree). Silent.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_py_proof_print() -> i32 {
    if pm_metal_py_ready() != 0 {
        return -1;
    }
    0
}

/// W4.2 proof: park on Metal sleep (asyncio.sleep_ms). Silent — tree shows await.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_py_proof_await() -> i32 {
    let _ = pm_metal_py_ready();
    proof::proof_await()
}

/// W11.5 proof: concurrent asyncio tasks across runners; logs metrics on UART.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_py_proof_concurrency() -> i32 {
    let _ = pm_metal_py_ready();
    proof::proof_concurrency()
}
