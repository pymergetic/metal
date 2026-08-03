//! tests.wasm_guest_input — W16.3b: shell_log + input poll/lock natives.
#![no_std]

#[repr(C)]
struct KeyEvent {
    code: u16,
    pressed: u8,
    mods: u8,
}

#[link(wasm_import_module = "pymergetic.metal.shell")]
extern "C" {
    fn pm_metal_shell_log(line: *const u8);
}

#[link(wasm_import_module = "pymergetic.metal.dev.input")]
extern "C" {
    fn pm_metal_dev_input_push_key(pressed: i32, code: u32);
    fn pm_metal_dev_input_poll_key_event(dest: u32) -> i32;
    fn pm_metal_dev_input_pointer_lock(surface: u32) -> i32;
    fn pm_metal_dev_input_pointer_locked() -> i32;
    fn pm_metal_dev_input_pointer_unlock();
}

#[no_mangle]
pub extern "C" fn ready() -> i32 {
    unsafe {
        pm_metal_shell_log(b"guest shell ok\0".as_ptr());

        /* Inject Escape press/release and poll it back. */
        pm_metal_dev_input_push_key(1, 0x29);
        let mut ev = KeyEvent {
            code: 0,
            pressed: 0,
            mods: 0,
        };
        if pm_metal_dev_input_poll_key_event(&mut ev as *mut _ as u32) == 0 {
            return -1;
        }
        if ev.code != 0x29 || ev.pressed != 1 {
            return -2;
        }
        pm_metal_dev_input_push_key(0, 0x29);
        if pm_metal_dev_input_poll_key_event(&mut ev as *mut _ as u32) == 0 {
            return -3;
        }
        if ev.code != 0x29 || ev.pressed != 0 {
            return -4;
        }

        if pm_metal_dev_input_pointer_lock(1) != 0 {
            return -5;
        }
        if pm_metal_dev_input_pointer_locked() == 0 {
            return -6;
        }
        pm_metal_dev_input_pointer_unlock();
        if pm_metal_dev_input_pointer_locked() != 0 {
            return -7;
        }

        pm_metal_shell_log(b"guest input ok\0".as_ptr());
    }
    0
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
