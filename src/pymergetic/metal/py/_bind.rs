//! Publish py edge entrypoints as a RegMod (not dyn `register_rows`).

use core::ffi::c_void;

use pymergetic_metal_reg::{find_mod, pm_metal_reg_mod_load, RegMod};

use crate::{
    pm_metal_py_alloc, pm_metal_py_await, pm_metal_py_free, pm_metal_py_gc_collect,
    pm_metal_py_gc_enabled, pm_metal_py_loop_feed, pm_metal_py_loop_last_result_i32,
    pm_metal_py_loop_last_result_valid, pm_metal_py_loop_reset, pm_metal_py_loop_step,
    pm_metal_py_proof_await, pm_metal_py_proof_concurrency, pm_metal_py_proof_print,
    pm_metal_py_ready, pm_metal_py_shell_running, pm_metal_py_shell_start,
    pm_metal_py_sleep_us,
};

pymergetic_metal_reg::reg_mod! {
    mod py = "pymergetic.metal.py";
    exports: [
        ready,
        alloc,
        free,
        gc_enabled,
        gc_collect,
        sleep_us,
        proof_print,
        proof_await,
        proof_concurrency,
        loop_step,
        loop_feed,
        loop_reset,
        loop_last_result_i32,
        loop_last_result_valid,
        shell_start,
        shell_running,
        await_ = "await",
    ];
}

extern "C" fn py_register_symbols(_ctx: *mut c_void) -> i32 {
    py::ready.publish(pm_metal_py_ready as *const c_void);
    py::alloc.publish(pm_metal_py_alloc as *const c_void);
    py::free.publish(pm_metal_py_free as *const c_void);
    py::gc_enabled.publish(pm_metal_py_gc_enabled as *const c_void);
    py::gc_collect.publish(pm_metal_py_gc_collect as *const c_void);
    py::sleep_us.publish(pm_metal_py_sleep_us as *const c_void);
    py::proof_print.publish(pm_metal_py_proof_print as *const c_void);
    py::proof_await.publish(pm_metal_py_proof_await as *const c_void);
    py::proof_concurrency.publish(pm_metal_py_proof_concurrency as *const c_void);
    py::loop_step.publish(pm_metal_py_loop_step as *const c_void);
    py::loop_feed.publish(pm_metal_py_loop_feed as *const c_void);
    py::loop_reset.publish(pm_metal_py_loop_reset as *const c_void);
    py::loop_last_result_i32.publish(pm_metal_py_loop_last_result_i32 as *const c_void);
    py::loop_last_result_valid.publish(pm_metal_py_loop_last_result_valid as *const c_void);
    py::shell_start.publish(pm_metal_py_shell_start as *const c_void);
    py::shell_running.publish(pm_metal_py_shell_running as *const c_void);
    py::await_.publish(pm_metal_py_await as *const c_void);
    0
}

static PY_MOD: RegMod = RegMod::from_static(
    py::NAME,
    &py::STORAGE.exports,
    &py::STORAGE.imports,
    Some(py_register_symbols),
);

/// Load `pymergetic.metal.py` into the kernel ring (idempotent).
pub fn reg_load() -> i32 {
    if find_mod(py::NAME).is_some() {
        return 0;
    }
    unsafe { pm_metal_reg_mod_load(&PY_MOD) }
}

/// Compat name for older callers.
pub unsafe fn publish() -> i32 {
    reg_load()
}
