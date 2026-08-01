//! Single entry: print every registered boot-tree section, then ready mark.

use super::api::{pm_metal_boot_tree_flush, pm_metal_boot_tree_reset};
use super::ready::pm_metal_boot_tree_notify_ready;
use super::registry::SECTIONS;

/// Print the registered boot tree. Returns 0, or -1 if a section reported FAIL.
/// On success also emits the machine ready mark for forge run.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_boot_tree_print() -> i32 {
    pm_metal_boot_tree_reset();
    let mut rc = 0i32;
    for s in SECTIONS {
        let r = (s.emit)();
        if r < 0 {
            rc = -1;
        }
    }
    pm_metal_boot_tree_flush();
    if rc == 0 {
        pm_metal_boot_tree_notify_ready();
    }
    rc
}
