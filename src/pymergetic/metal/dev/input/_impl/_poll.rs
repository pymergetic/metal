//! PS/2 drain into key/pointer rings + poll/lock surface for guests.

use core::ptr::{addr_of, addr_of_mut};

use super::{
    KBC_AUX_DATA, KBC_DATA, KBC_OUTPUT_FULL, KBC_STATUS, pm_metal_boot_io_ops,
    pm_metal_dev_input_key_event_t as KeyEvent,
    pm_metal_dev_input_pointer_t as PointerEvent,
};

const Q: usize = 64;
const KEY_NONE: u16 = 0;
const KEY_A: u16 = 0x04;
const KEY_ENTER: u16 = 0x28;
const KEY_ESCAPE: u16 = 0x29;
const KEY_BACKSPACE: u16 = 0x2a;
const KEY_TAB: u16 = 0x2b;
const KEY_SPACE: u16 = 0x2c;
const KEY_LEFT: u16 = 0x50;
const KEY_RIGHT: u16 = 0x4f;
const KEY_DOWN: u16 = 0x51;
const KEY_UP: u16 = 0x52;
const KEY_LCTRL: u16 = 0xe0;
const KEY_LSHIFT: u16 = 0xe1;
const KEY_LALT: u16 = 0xe2;
const KEY_RCTRL: u16 = 0xe4;
const KEY_RSHIFT: u16 = 0xe5;
const KEY_RALT: u16 = 0xe6;
const MOD_CTRL: u8 = 1;
const MOD_SHIFT: u8 = 2;
const MOD_ALT: u8 = 4;
const PTR_RELATIVE: u32 = 2;

struct Rings {
    key: [KeyEvent; Q],
    key_head: u32,
    key_tail: u32,
    ptr: [PointerEvent; Q],
    ptr_head: u32,
    ptr_tail: u32,
    mods: u8,
    ext: i32,
    ptr_locked: i32,
    ptr_lock_surf: u32,
}

static mut RINGS: Rings = Rings {
    key: [KeyEvent {
        code: 0,
        pressed: 0,
        mods: 0,
    }; Q],
    key_head: 0,
    key_tail: 0,
    ptr: [PointerEvent {
        x: 0,
        y: 0,
        dx: 0,
        dy: 0,
        buttons: 0,
        flags: 0,
    }; Q],
    ptr_head: 0,
    ptr_tail: 0,
    mods: 0,
    ext: 0,
    ptr_locked: 0,
    ptr_lock_surf: 0,
};

fn set1_hid(make: u8, ext: i32) -> u16 {
    let code = make & 0x7f;
    if ext != 0 {
        return match code {
            0x4b => KEY_LEFT,
            0x4d => KEY_RIGHT,
            0x48 => KEY_UP,
            0x50 => KEY_DOWN,
            0x1d => KEY_RCTRL,
            0x38 => KEY_RALT,
            _ => KEY_NONE,
        };
    }
    match code {
        0x01 => KEY_ESCAPE,
        0x0f => KEY_TAB,
        0x0e => KEY_BACKSPACE,
        0x1c => KEY_ENTER,
        0x39 => KEY_SPACE,
        0x2a => KEY_LSHIFT,
        0x36 => KEY_RSHIFT,
        0x1d => KEY_LCTRL,
        0x38 => KEY_LALT,
        0x1e => KEY_A,
        0x30 => KEY_A + 1,
        0x2e => KEY_A + 2,
        0x20 => KEY_A + 3,
        0x12 => KEY_A + 4,
        0x21 => KEY_A + 5,
        0x22 => KEY_A + 6,
        0x23 => KEY_A + 7,
        0x17 => KEY_A + 8,
        0x24 => KEY_A + 9,
        0x25 => KEY_A + 10,
        0x26 => KEY_A + 11,
        0x32 => KEY_A + 12,
        0x31 => KEY_A + 13,
        0x18 => KEY_A + 14,
        0x19 => KEY_A + 15,
        0x10 => KEY_A + 16,
        0x13 => KEY_A + 17,
        0x1f => KEY_A + 18,
        0x14 => KEY_A + 19,
        0x16 => KEY_A + 20,
        0x2f => KEY_A + 21,
        0x11 => KEY_A + 22,
        0x2d => KEY_A + 23,
        0x15 => KEY_A + 24,
        0x2c => KEY_A + 25,
        _ => KEY_NONE,
    }
}

fn note_mod(hid: u16, pressed: u8) {
    unsafe {
        let bit = match hid {
            KEY_LCTRL | KEY_RCTRL => MOD_CTRL,
            KEY_LSHIFT | KEY_RSHIFT => MOD_SHIFT,
            KEY_LALT | KEY_RALT => MOD_ALT,
            _ => 0,
        };
        if bit == 0 {
            return;
        }
        if pressed != 0 {
            RINGS.mods |= bit;
        } else {
            RINGS.mods &= !bit;
        }
    }
}

fn enqueue_key(ev: KeyEvent) {
    unsafe {
        let next = (RINGS.key_head + 1) % (Q as u32);
        if next == RINGS.key_tail {
            return;
        }
        RINGS.key[RINGS.key_head as usize] = ev;
        RINGS.key_head = next;
    }
}

fn enqueue_ptr(ev: PointerEvent) {
    unsafe {
        let next = (RINGS.ptr_head + 1) % (Q as u32);
        if next == RINGS.ptr_tail {
            return;
        }
        RINGS.ptr[RINGS.ptr_head as usize] = ev;
        RINGS.ptr_head = next;
    }
}

/// Drain i8042 keyboard bytes into the HID key ring (ignore aux/mouse).
pub unsafe fn poll_port() {
    let ops = pm_metal_boot_io_ops();
    if ops.is_null() {
        return;
    }
    let Some(inb) = (*ops).inb else {
        return;
    };
    let mut n = 0u32;
    while n < 32 {
        let status = inb(KBC_STATUS);
        if (status & KBC_OUTPUT_FULL) == 0 {
            break;
        }
        let b = inb(KBC_DATA);
        n += 1;
        if (status & KBC_AUX_DATA) != 0 {
            continue;
        }
        if b == 0xe0 {
            RINGS.ext = 1;
            continue;
        }
        if b == 0xe1 {
            continue;
        }
        let pressed = if (b & 0x80) != 0 { 0u8 } else { 1u8 };
        let hid = set1_hid(b, RINGS.ext);
        RINGS.ext = 0;
        if hid == KEY_NONE {
            continue;
        }
        note_mod(hid, pressed);
        enqueue_key(KeyEvent {
            code: hid,
            pressed,
            mods: RINGS.mods,
        });
    }
}

pub unsafe fn push_key(pressed: i32, code: u16) {
    if code == KEY_NONE {
        return;
    }
    let p = if pressed != 0 { 1u8 } else { 0u8 };
    note_mod(code, p);
    enqueue_key(KeyEvent {
        code,
        pressed: p,
        mods: (*addr_of!(RINGS)).mods,
    });
}

pub unsafe fn poll_key_event(out: *mut KeyEvent) -> i32 {
    poll_port();
    if out.is_null() {
        return 0;
    }
    let r = &mut *addr_of_mut!(RINGS);
    if r.key_head == r.key_tail {
        return 0;
    }
    *out = r.key[r.key_tail as usize];
    r.key_tail = (r.key_tail + 1) % (Q as u32);
    1
}

pub unsafe fn poll_pointer(out: *mut PointerEvent) -> i32 {
    poll_port();
    if out.is_null() {
        return 0;
    }
    let r = &mut *addr_of_mut!(RINGS);
    if r.ptr_head == r.ptr_tail {
        return 0;
    }
    *out = r.ptr[r.ptr_tail as usize];
    r.ptr_tail = (r.ptr_tail + 1) % (Q as u32);
    1
}

pub unsafe fn pointer_enqueue(ev: *const PointerEvent) {
    if ev.is_null() {
        return;
    }
    let mut e = *ev;
    if (*addr_of!(RINGS)).ptr_locked != 0 {
        e.flags |= PTR_RELATIVE;
        e.x = -1;
        e.y = -1;
    }
    enqueue_ptr(e);
}

pub unsafe fn pointer_lock(surface: u32) -> i32 {
    RINGS.ptr_locked = 1;
    RINGS.ptr_lock_surf = if surface == 0 { 1 } else { surface };
    0
}

pub unsafe fn pointer_unlock() {
    RINGS.ptr_locked = 0;
    RINGS.ptr_lock_surf = 0;
}

pub unsafe fn pointer_locked() -> i32 {
    (*addr_of!(RINGS)).ptr_locked
}
