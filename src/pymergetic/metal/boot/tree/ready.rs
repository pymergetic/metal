//! Boot-tree ready signal — machine mark for host forge (not human tree text).
//!
//! Firmware emits this after a successful tree print; forge `run` watches the
//! same bytes (keep the literal in `forge/_run.rs` identical).

use pymergetic_metal_log::pm_metal_log;

/// Stable ready mark (no trailing newline — serial may wrap with `\r\n`).
pub const READY_MARK: &[u8] = b"#pm-metal/boot-tree/ready";

/// Same bytes + NUL for C `const char *` callers.
static READY_CSTR: [u8; READY_MARK.len() + 1] = {
    let mut a = [0u8; READY_MARK.len() + 1];
    let mut i = 0usize;
    while i < READY_MARK.len() {
        a[i] = READY_MARK[i];
        i += 1;
    }
    a
};

/// NUL-terminated pointer to the ready mark (for C callers / tests).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_boot_tree_ready_mark() -> *const u8 {
    READY_CSTR.as_ptr()
}

/// Emit the machine-readable ready mark on the log path (no SGR).
#[no_mangle]
pub unsafe extern "C" fn pm_metal_boot_tree_notify_ready() {
    let mut buf = [0u8; 64];
    if READY_MARK.len() + 1 > buf.len() {
        return;
    }
    buf[..READY_MARK.len()].copy_from_slice(READY_MARK);
    buf[READY_MARK.len()] = 0;
    pm_metal_log(buf.as_ptr());
}
