//! Boot-tree proof notes — set before `tree_print`, shown in emitters only.

use core::sync::atomic::{AtomicI8, Ordering};

const UNSET: i8 = 0;
const OK: i8 = 1;
const FAIL: i8 = -1;

static NOTE_AWAIT: AtomicI8 = AtomicI8::new(UNSET);
static NOTE_WASM: AtomicI8 = AtomicI8::new(UNSET);
static NOTE_HTTP: AtomicI8 = AtomicI8::new(UNSET);
static NOTE_PY: AtomicI8 = AtomicI8::new(UNSET);

fn store(slot: &AtomicI8, ok: i32) {
    slot.store(if ok != 0 { OK } else { FAIL }, Ordering::Relaxed);
}

fn is_ok(slot: &AtomicI8) -> bool {
    slot.load(Ordering::Relaxed) == OK
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_boot_tree_note_await(ok: i32) {
    store(&NOTE_AWAIT, ok);
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_boot_tree_note_wasm(ok: i32) {
    store(&NOTE_WASM, ok);
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_boot_tree_note_http(ok: i32) {
    store(&NOTE_HTTP, ok);
}

#[no_mangle]
pub unsafe extern "C" fn pm_metal_boot_tree_note_py(ok: i32) {
    store(&NOTE_PY, ok);
}

pub fn await_ok() -> bool {
    is_ok(&NOTE_AWAIT)
}

pub fn wasm_ok() -> bool {
    is_ok(&NOTE_WASM)
}

pub fn http_ok() -> bool {
    is_ok(&NOTE_HTTP)
}
