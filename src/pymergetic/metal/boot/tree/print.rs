//! Single entry: print every registered boot-tree section.

use super::registry::SECTIONS;

/// Print the registered boot tree. Returns 0, or -1 if a section reported FAIL.
#[no_mangle]
pub unsafe extern "C" fn pm_metal_boot_tree_print() -> i32 {
    let mut rc = 0i32;
    for s in SECTIONS {
        let r = (s.emit)();
        if r < 0 {
            rc = -1;
        }
    }
    rc
}
