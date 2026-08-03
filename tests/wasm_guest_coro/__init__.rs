//! tests.wasm_guest_coro — W16.3c: guest coro pin + process crown.
#![no_std]

const DONE: u32 = 2;
const ERROR: u32 = 4;

#[link(wasm_import_module = "pymergetic.metal.async.coro")]
extern "C" {
    fn pm_metal_async_coro_create(state_bytes: u32) -> u32;
    fn pm_metal_async_coro_state(h: u32) -> u32;
    fn pm_metal_async_coro_alloc(h: u32, n: u32) -> u32;
    fn pm_metal_async_coro_close(h: u32);
    fn pm_metal_async_guest_coro_smoke() -> i32;
}

#[link(wasm_import_module = "pymergetic.metal.async.task")]
extern "C" {
    fn pm_metal_async_create_task(h: u32) -> u32;
}

#[link(wasm_import_module = "pymergetic.metal.async.process")]
extern "C" {
    fn pm_metal_async_process_crown(task_h: u32) -> u32;
    fn pm_metal_async_process_handle(pid: u32) -> u32;
    fn pm_metal_async_process_kill(pid: u32) -> i32;
}

#[link(wasm_import_module = "pymergetic.metal.shell")]
extern "C" {
    fn pm_metal_shell_log(line: *const u8);
}

/// Scheduler-invoked step: pin frame, write marker, DONE.
#[no_mangle]
pub extern "C" fn step(self_h: u32) -> u32 {
    unsafe {
        let mut off = pm_metal_async_coro_state(self_h);
        if off == 0 {
            off = pm_metal_async_coro_alloc(self_h, 8);
        }
        if off == 0 {
            return ERROR;
        }
        let p = off as *mut u32;
        *p = 0xC0_FF_EE_u32;
        if *p != 0xC0_FF_EE_u32 {
            return ERROR;
        }
        if pm_metal_async_coro_state(self_h) != off {
            return ERROR;
        }
    }
    DONE
}

#[no_mangle]
pub extern "C" fn ready() -> i32 {
    unsafe {
        if pm_metal_async_guest_coro_smoke() != 0 {
            return -1;
        }
        pm_metal_shell_log(b"guest coro ok\0".as_ptr());

        /* Process crown over a fresh scheduled coro (killed before poll). */
        let h = pm_metal_async_coro_create(8);
        if h == 0 {
            return -2;
        }
        let th = pm_metal_async_create_task(h);
        if th == 0 {
            pm_metal_async_coro_close(h);
            return -3;
        }
        let pid = pm_metal_async_process_crown(th);
        if pid == 0 {
            pm_metal_async_coro_close(h);
            return -4;
        }
        if pm_metal_async_process_handle(pid) != th {
            let _ = pm_metal_async_process_kill(pid);
            return -5;
        }
        if pm_metal_async_process_kill(pid) != 0 {
            return -6;
        }
        pm_metal_shell_log(b"guest process ok\0".as_ptr());
    }
    0
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
